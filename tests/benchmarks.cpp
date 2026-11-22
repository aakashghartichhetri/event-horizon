#include "event_horizon/exchange.hpp"
#include "event_horizon/report.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace eh = event_horizon;
namespace {
using Clock = std::chrono::steady_clock;

void require(bool ok) {
    if (!ok) throw std::runtime_error("benchmark setup or trade rejected");
}

int count_arg(char* raw, int upper) {
    int result = 0;
    const std::string text = raw;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size() || result < 1 || result > upper) {
        throw std::invalid_argument("count outside supported benchmark range");
    }
    return result;
}

double milliseconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void print(std::string mode, int count, double setup_ms, double operation_ms,
           std::int64_t settlements) {
    std::cout << std::fixed << std::setprecision(3)
              << "mode=" << mode << " count=" << count
              << " setup_ms=" << setup_ms
              << " operation_ms=" << operation_ms
              << " settled=" << settlements << '\n';
}

void book(int depth) {
    eh::Exchange exchange;
    require(exchange.add_account("BUYER", 400'000'000, 0).ok);
    require(exchange.add_account("SELLER", 0, depth).ok);
    const auto before_setup = Clock::now();
    for (int i = 0; i < depth; ++i) {
        // Worst-case for repeated best-match scans: prices arrive backwards.
        require(exchange.place_order("s" + std::to_string(i), "SELLER", eh::Side::sell,
                                     1, 10'000 + depth - i).ok);
    }
    const auto before_sweep = Clock::now();
    require(exchange.place_order("sweep", "BUYER", eh::Side::buy, depth, 20'000).ok);
    const auto after_sweep = Clock::now();
    require(exchange.settle_all().settled == static_cast<std::size_t>(depth));
    exchange.verify_invariants();
    print("deep-book-sweep", depth, milliseconds(before_setup, before_sweep),
          milliseconds(before_sweep, after_sweep), exchange.metrics().trades_settled);
}

void single(int count) {
    eh::Exchange exchange;
    require(exchange.add_account("BUYER", static_cast<std::int64_t>(count) * 100, 0).ok);
    require(exchange.add_account("SELLER", 0, count).ok);
    const auto before = Clock::now();
    for (int i = 0; i < count; ++i) {
        require(exchange.place_order("s" + std::to_string(i), "SELLER", eh::Side::sell, 1, 100).ok);
        require(exchange.place_order("b" + std::to_string(i), "BUYER", eh::Side::buy, 1, 100).ok);
    }
    const auto after = Clock::now();
    require(exchange.settle_all().settled == static_cast<std::size_t>(count));
    exchange.verify_invariants();
    print("single-fill", count, 0, milliseconds(before, after), exchange.metrics().trades_settled);
}

void replay(int iterations) {
    const auto before = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        const auto simulation = eh::run_scenario("data", "baseline");
        require(simulation.exchange.metrics().trades_settled == 3);
    }
    const auto after = Clock::now();
    print("scenario-replay", iterations, 0, milliseconds(before, after), 3 * iterations);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::invalid_argument("usage: event-horizon-bench MODE COUNT: book 1..8000, single 1..9000, replay 1..1000");
        const std::string mode = argv[1];
        if (mode == "book") book(count_arg(argv[2], 8000));
        else if (mode == "single") single(count_arg(argv[2], 9000));
        else if (mode == "replay") replay(count_arg(argv[2], 1000));
        else throw std::invalid_argument("unknown benchmark mode: " + mode);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "BENCHMARK ERROR: " << e.what() << '\n';
        return 1;
    }
}
