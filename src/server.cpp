#include "event_horizon/report.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <chrono>
#include <condition_variable>
#include <csignal>
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
    out.set(http::field::server, "EventHorizon/1.0");
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
                                        const Configuration& config) {
    const std::string path(req.target());
    if (req.method() == http::verb::get) {
        if (path == "/api/health") return response(http::status::ok,
            "{\"status\":\"ok\",\"service\":\"event-horizon\",\"version\":\"1.0.0\"}");
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
            return response(http::status::ok, read_file(config.web_directory + "/index.html"),
                            "text/html; charset=utf-8");
        }
        if (path == "/app.js") {
            return response(http::status::ok, read_file(config.web_directory + "/app.js"),
                            "text/javascript; charset=utf-8");
        }
        if (path == "/style.css") {
            return response(http::status::ok, read_file(config.web_directory + "/style.css"),
                            "text/css; charset=utf-8");
        }
        if (path == "/favicon.svg") {
            return response(http::status::ok, read_file(config.web_directory + "/favicon.svg"),
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

void handle_connection(beast::tcp_stream& stream, const Configuration& config) {
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
    http::response<http::string_body> out;
    try {
        out = route(parser.release(), config);
    } catch (const std::exception& e) {
        // Path and system errors are not printed into the response.
        std::cerr << "server request failed: " << e.what() << '\n';
        out = error_response(http::status::internal_server_error, "internal server error");
    }
    stream.expires_after(std::chrono::seconds(10));
    http::write(stream, out, ec);
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
}

class WorkerQueue {
public:
    void submit(std::unique_ptr<beast::tcp_stream> stream) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.size() >= 64) {
            lock.unlock();
            const auto out = error_response(http::status::service_unavailable, "server is busy");
            beast::error_code ec;
            http::write(*stream, out, ec);
            return;
        }
        queue_.push_back(std::move(stream));
        ready_.notify_one();
    }

    std::unique_ptr<beast::tcp_stream> take() {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return !queue_.empty(); });
        auto stream = std::move(queue_.front());
        queue_.pop_front();
        return stream;
    }

private:
    std::deque<std::unique_ptr<beast::tcp_stream>> queue_;
    std::mutex mutex_;
    std::condition_variable ready_;
};

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto config = parse_args(argc, argv);
        // Fail at startup if the configured paths are unusable.
        (void)read_file(config.web_directory + "/index.html");
        (void)eh::run_scenario(config.data_directory, "baseline");
#ifdef SIGPIPE
        std::signal(SIGPIPE, SIG_IGN);
#endif
        net::io_context io;
        const auto address = net::ip::make_address(config.host);
        tcp::acceptor acceptor(io);
        acceptor.open(address.is_v4() ? tcp::v4() : tcp::v6());
        acceptor.set_option(net::socket_base::reuse_address(true));
        acceptor.bind(tcp::endpoint(address, config.port));
        acceptor.listen(net::socket_base::max_listen_connections);
        WorkerQueue queue;
        std::vector<std::thread> workers;
        for (int i = 0; i < config.workers; ++i) {
            workers.emplace_back([&queue, &config] {
                while (true) {
                    auto stream = queue.take();
                    try { handle_connection(*stream, config); }
                    catch (const std::exception& e) {
                        std::cerr << "connection failed: " << e.what() << '\n';
                    }
                }
            });
        }
        for (auto& worker : workers) worker.detach();
        std::cout << "EVENT HORIZON | http://" << config.host << ':' << config.port
                  << " | workers=" << config.workers << std::endl;
        while (true) {
            auto stream = std::make_unique<beast::tcp_stream>(io);
            beast::error_code ec;
            acceptor.accept(stream->socket(), ec);
            if (ec) { std::cerr << "accept error: " << ec.message() << '\n'; continue; }
            queue.submit(std::move(stream));
        }
    } catch (const std::exception& e) {
        std::cerr << "SERVER ERROR: " << e.what() << '\n';
        return 1;
    }
}
