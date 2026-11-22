#include "event_horizon/report.hpp"
#include "event_horizon/resilience.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <csignal>
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;
namespace eh = event_horizon;

namespace {

// Lock-free atomic operations can be used in a signal handler on the supported
// Linux/macOS targets. Unlike sig_atomic_t, this flag is also read by a different
// thread when the signal is delivered to a worker.
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool> shutdown_requested{false};

void request_shutdown(int) { shutdown_requested.store(true, std::memory_order_relaxed); }

struct RequestStats {
    std::atomic<std::uint64_t> total{0};
    std::atomic<std::uint64_t> client_errors{0};
    std::atomic<std::uint64_t> server_errors{0};
    std::atomic<std::uint64_t> in_flight{0};
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
};

std::string stats_json(const RequestStats& stats) {
    const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - stats.started).count();
    return "{\"uptime_seconds\":" + std::to_string(uptime)
        + ",\"requests_total\":" + std::to_string(stats.total.load())
        + ",\"client_errors_total\":" + std::to_string(stats.client_errors.load())
        + ",\"server_errors_total\":" + std::to_string(stats.server_errors.load())
        + ",\"in_flight\":" + std::to_string(stats.in_flight.load()) + "}";
}

struct StaticAssets {
    std::string index;
    std::string script;
    std::string analysis;
    std::string stylesheet;
    std::string favicon;
};

struct Configuration {
    std::string host = "127.0.0.1";
    unsigned short port = 8080;
    int workers = 4;
    std::string data_directory = "data";
    std::string web_directory = "web";
};

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read server asset: " + path);
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

int number(const std::string& s, int low, int high) {
    std::size_t consumed = 0;
    int value = 0;
    try { value = std::stoi(s, &consumed); }
    catch (const std::exception&) { throw std::invalid_argument("invalid numeric option: " + s); }
    if (consumed != s.size() || value < low || value > high) {
        throw std::invalid_argument("numeric option outside allowed range: " + s);
    }
    return value;
}

// Only accept a two-field, integer-only query string. No general-purpose
// URL decoder is needed for these bounded, numeric experiment parameters.
std::pair<int, int> resilience_params(const std::string& path) {
    constexpr std::string_view prefix = "/api/resilience/case?";
    if (!path.starts_with(prefix)) throw std::invalid_argument("expected recovery and funding parameters");
    const auto query = path.substr(prefix.size());
    const auto separator = query.find('&');
    if (separator == std::string::npos || separator != query.rfind('&')) {
        throw std::invalid_argument("expected recovery and funding parameters");
    }
    const auto first = query.substr(0, separator);
    const auto second = query.substr(separator + 1);
    constexpr std::string_view recovery_key = "recovery=";
    constexpr std::string_view funding_key = "funding=";
    if (!first.starts_with(recovery_key) || !second.starts_with(funding_key)) {
        throw std::invalid_argument("expected recovery and funding parameters");
    }
    return {number(first.substr(recovery_key.size()), 0, 3),
            number(second.substr(funding_key.size()), 0, 2)};
}

Configuration parse_args(int argc, char** argv) {
    Configuration c;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) throw std::invalid_argument("server options require values");
        const std::string key = argv[i];
        const std::string value = argv[i + 1];
        if (key == "--host") c.host = value;
        else if (key == "--port") c.port = static_cast<unsigned short>(number(value, 1, 65535));
        else if (key == "--workers") c.workers = number(value, 1, 8);
        else if (key == "--data-dir") c.data_directory = value;
        else if (key == "--web-dir") c.web_directory = value;
        else throw std::invalid_argument("unknown option: " + key);
    }
    return c;
}

http::response<http::string_body> response(http::status code, std::string body,
                                          std::string content_type = "application/json; charset=utf-8") {
    http::response<http::string_body> out{code, 11};
    out.set(http::field::server, "EventHorizon/2.1");
    out.set(http::field::content_type, content_type);
    out.set(http::field::cache_control, "no-store");
    out.set("X-Content-Type-Options", "nosniff");
    out.set("X-Frame-Options", "DENY");
    out.set("Referrer-Policy", "no-referrer");
    out.set("Content-Security-Policy", "default-src 'self'; script-src 'self'; "
            "style-src 'self'; img-src 'self' data:; connect-src 'self'; object-src 'none'; "
            "base-uri 'none'; frame-ancestors 'none'");
    out.body() = std::move(body);
    out.keep_alive(false);
    out.prepare_payload();
    return out;
}

http::response<http::string_body> error_response(http::status code, const std::string& text) {
    return response(code, "{\"error\":" + eh::json_escape(text) + "}");
}

http::response<http::string_body> route(const http::request<http::string_body>& req,
                                        const Configuration& config, const RequestStats& stats,
                                        const StaticAssets& assets) {
    const std::string path(req.target());
    if (req.method() == http::verb::get) {
        if (path == "/api/health") return response(http::status::ok,
            "{\"status\":\"ok\",\"service\":\"event-horizon\",\"version\":\"2.1.0\"}");
        if (path == "/api/metrics") return response(http::status::ok, stats_json(stats));
        if (path == "/api/resilience") return response(http::status::ok, eh::resilience_matrix_json());
        if (path == "/api/resilience/case" || path.starts_with("/api/resilience/case?")) {
            try {
                const auto [recovery, funding] = resilience_params(path);
                return response(http::status::ok, eh::to_json(
                    eh::run_resilience_case(recovery, funding), "resilience"));
            } catch (const std::invalid_argument& e) {
                return error_response(http::status::bad_request, e.what());
            }
        }
        if (path == "/api/scenarios") return response(http::status::ok, eh::catalog_json());
        if (path == "/api/compare") return response(http::status::ok,
            eh::comparison_json(config.data_directory));
        const std::string prefix = "/api/scenarios/";
        if (path.starts_with(prefix)) {
            const std::string id = path.substr(prefix.size());
            try {
                return response(http::status::ok,
                    eh::to_json(eh::run_scenario(config.data_directory, id), id));
            } catch (const std::invalid_argument& e) {
                return error_response(http::status::not_found, e.what());
            }
        }
        if (path == "/" || path == "/index.html") {
            return response(http::status::ok, assets.index,
                            "text/html; charset=utf-8");
        }
        if (path == "/app.js") {
            return response(http::status::ok, assets.script,
                            "text/javascript; charset=utf-8");
        }
        if (path == "/insights.mjs") {
            return response(http::status::ok, assets.analysis,
                            "text/javascript; charset=utf-8");
        }
        if (path == "/style.css") {
            return response(http::status::ok, assets.stylesheet,
                            "text/css; charset=utf-8");
        }
        if (path == "/favicon.svg") {
            return response(http::status::ok, assets.favicon,
                            "image/svg+xml");
        }
        return error_response(http::status::not_found, "route not found");
    }
    if (req.method() == http::verb::post && path == "/api/replay") {
        const std::string type(req[http::field::content_type]);
        if (type != "text/plain" && !type.starts_with("text/plain;")) {
            return error_response(http::status::unsupported_media_type, "use Content-Type: text/plain");
        }
        try {
            return response(http::status::ok, eh::to_json(eh::run_text(req.body()), "custom"));
        } catch (const std::invalid_argument& e) {
            return error_response(http::status::bad_request, e.what());
        } catch (const std::runtime_error& e) {
            return error_response(http::status::bad_request, e.what());
        }
    }
    return error_response(http::status::method_not_allowed, "method not allowed");
}

void handle_connection(beast::tcp_stream& stream, const Configuration& config,
                       RequestStats& stats, const StaticAssets& assets) {
    // Beast expires_after governs asynchronous operations; this server uses
    // synchronous reads/writes. The OS socket timeouts also cover stalled
    // clients, preventing worker starvation and indefinite shutdown waits.
    timeval read_timeout{5, 0};
    timeval write_timeout{10, 0};
    const auto socket = stream.socket().native_handle();
    if (::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                     &read_timeout, sizeof(read_timeout)) != 0 ||
        ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                     &write_timeout, sizeof(write_timeout)) != 0) {
        throw std::runtime_error("failed configuring client socket timeouts");
    }
    stream.expires_after(std::chrono::seconds(5));
    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.body_limit(65'536);
    parser.header_limit(8'192);
    beast::error_code ec;
    http::read(stream, buffer, parser, ec);
    if (ec) {
        if (ec == http::error::body_limit || ec == http::error::header_limit) {
            auto out = error_response(http::status::payload_too_large, "request limit exceeded");
            http::write(stream, out, ec);
        }
        return;
    }
    stats.in_flight.fetch_add(1);
    http::response<http::string_body> out;
    try {
        out = route(parser.release(), config, stats, assets);
    } catch (const std::exception& e) {
        // Path and system errors are not printed into the response.
        std::cerr << "server request failed: " << e.what() << '\n';
        out = error_response(http::status::internal_server_error, "internal server error");
    }
    stats.total.fetch_add(1);
    const auto status = out.result_int();
    if (status >= 400 && status < 500) stats.client_errors.fetch_add(1);
    if (status >= 500) stats.server_errors.fetch_add(1);
    stream.expires_after(std::chrono::seconds(10));
    http::write(stream, out, ec);
    stats.in_flight.fetch_sub(1);
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
}

class WorkerQueue {
public:
    WorkerQueue() { active_.reserve(8); } // Maximum configured worker count.

    void submit(std::unique_ptr<beast::tcp_stream> stream) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stopping_ || queue_.size() >= 64) {
            lock.unlock();
            // Never write a busy response on the accept thread: a client that
            // stops reading could otherwise block new connections indefinitely.
            beast::error_code ec;
            stream->socket().shutdown(tcp::socket::shutdown_both, ec);
            stream->socket().close(ec);
            return;
        }
        queue_.push_back(std::move(stream));
        ready_.notify_one();
    }

    std::unique_ptr<beast::tcp_stream> take() {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        if (stopping_) return nullptr;
        auto stream = std::move(queue_.front());
        queue_.pop_front();
        active_.push_back({stream.get(), std::chrono::steady_clock::now(), false});
        return stream;
    }

    void complete(beast::tcp_stream* stream) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::erase_if(active_, [stream](const Active& item) { return item.stream == stream; });
    }

    // SO_RCVTIMEO alone does not bound every library-level synchronous read.
    // Interrupt stalled sockets from the accept thread after five seconds.
    void expire_active() {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        for (auto& item : active_) {
            if (!item.closed && now - item.started >= std::chrono::seconds(5)) {
                ::shutdown(item.stream->socket().native_handle(), SHUT_RDWR);
                item.closed = true;
            }
        }
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        queue_.clear();
        for (auto& item : active_) {
            if (!item.closed) {
                ::shutdown(item.stream->socket().native_handle(), SHUT_RDWR);
                item.closed = true;
            }
        }
        ready_.notify_all();
    }

private:
    struct Active {
        beast::tcp_stream* stream;
        std::chrono::steady_clock::time_point started;
        bool closed;
    };
    std::vector<Active> active_;
    bool stopping_ = false;
    std::deque<std::unique_ptr<beast::tcp_stream>> queue_;
    std::mutex mutex_;
    std::condition_variable ready_;
};

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto config = parse_args(argc, argv);
        // Assets are immutable after startup; avoid per-request filesystem I/O.
        // Load all five files up front so missing assets fail fast.
        const StaticAssets assets{
            read_file(config.web_directory + "/index.html"),
            read_file(config.web_directory + "/app.js"),
            read_file(config.web_directory + "/insights.mjs"),
            read_file(config.web_directory + "/style.css"),
            read_file(config.web_directory + "/favicon.svg")};
        (void)eh::run_scenario(config.data_directory, "baseline");
#ifdef SIGPIPE
        std::signal(SIGPIPE, SIG_IGN);
#endif
        std::signal(SIGINT, request_shutdown);
        std::signal(SIGTERM, request_shutdown);
        net::io_context io;
        const auto address = net::ip::make_address(config.host);
        tcp::acceptor acceptor(io);
        acceptor.open(address.is_v4() ? tcp::v4() : tcp::v6());
        acceptor.set_option(net::socket_base::reuse_address(true));
        acceptor.bind(tcp::endpoint(address, config.port));
        acceptor.listen(net::socket_base::max_listen_connections);
        // A short nonblocking poll makes SIGTERM portable across native and
        // container builds without detached threads or unsafe signal IO.
        acceptor.non_blocking(true);
        WorkerQueue queue;
        RequestStats stats;
        std::vector<std::thread> workers;
        for (int i = 0; i < config.workers; ++i) {
            workers.emplace_back([&queue, &config, &stats, &assets] {
                while (auto stream = queue.take()) {
                    try { handle_connection(*stream, config, stats, assets); }
                    catch (const std::exception& e) {
                        std::cerr << "connection failed: " << e.what() << '\n';
                    }
                    queue.complete(stream.get());
                }
            });
        }

        std::cout << "EVENT HORIZON | http://" << config.host << ':' << config.port
                  << " | workers=" << config.workers << std::endl;
        while (!shutdown_requested.load(std::memory_order_relaxed)) {
            queue.expire_active();
            // Poll the listening socket, not a fixed sleep: accept requests
            // immediately while still checking the stop flag every 100 ms.
            pollfd listening{acceptor.native_handle(), POLLIN, 0};
            const int ready = ::poll(&listening, 1, 100);
            if (ready == 0) continue;
            if (ready < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error("listener poll failed");
            }
            auto stream = std::make_unique<beast::tcp_stream>(io);
            beast::error_code ec;
            acceptor.accept(stream->socket(), ec);
            if (ec == net::error::would_block || ec == net::error::try_again) continue;
            if (ec) {
                if (!shutdown_requested.load(std::memory_order_relaxed)) std::cerr << "accept error: " << ec.message() << '\n';
                continue;
            }
            queue.submit(std::move(stream));
        }
        beast::error_code ec;
        acceptor.close(ec);
        queue.stop();
        for (auto& worker : workers) worker.join();
        std::cout << "EVENT HORIZON | shutdown complete" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "SERVER ERROR: " << e.what() << '\n';
        return 1;
    }
}
