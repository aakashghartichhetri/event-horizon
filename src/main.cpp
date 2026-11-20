#include "event_horizon/exchange.hpp"
#include "event_horizon/replay.hpp"
#include "event_horizon/report.hpp"
#include "event_horizon/resilience.hpp"

#include <chrono>
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace eh = event_horizon;

namespace {

void usage(std::ostream& out) {
    out << "EVENT HORIZON - deterministic financial system laboratory\n\n"
        << "Usage:\n"
        << "  event-horizon demo                 Compare baseline, outage, recovery\n"
        << "  event-horizon replay <file.csv>    Replay a scenario and inspect balances\n"
        << "  event-horizon benchmark [count]    Measure repeated matching (default 10000)\n"
        << "  event-horizon list                 List built-in experiments\n"
        << "  event-horizon scenario <name>      Replay a built-in experiment\n"
        << "  event-horizon scenario-json <name> Export a built-in experiment as JSON\n"
        << "  event-horizon compare-json         Output comparison JSON\n"
        << "  event-horizon resilience-json      Run all 12 controlled recovery cases\n"
        << "  event-horizon resilience-case R F  Replay one case (R=0..3, F=0..2)\n"
        << "  event-horizon help                 Show this help\n\n"
        << "Run 'demo' from the project root so data/*.csv can be found.\n";
}

int demo() {
    struct Scenario {
        std::string title;
        std::string path;
    };
    const std::vector<Scenario> scenarios = {
        {"baseline", "data/baseline.csv"},
        {"outage", "data/payments_outage.csv"},
        {"recovery", "data/recovery.csv"},
        {"bridge", "data/liquidity_bridge.csv"},
        {"agents", "data/agents.csv"},
    };
    std::cout << "SCENARIO COMPARISON (same starting accounts and intended trades)\n"
              << std::left << std::setw(12) << "scenario"
              << std::right << std::setw(10) << "fills"
              << std::setw(10) << "settled"
              << std::setw(11) << "rejected"
              << std::setw(11) << "pending"
              << std::setw(16) << "escrow cash" << '\n';
    for (const auto& scenario : scenarios) {
        std::ostringstream ignored_detail;
        eh::Exchange exchange = eh::replay_file(scenario.path, ignored_detail);
        const eh::Metrics& metrics = exchange.metrics();
        std::cout << std::left << std::setw(12) << scenario.title
                  << std::right << std::setw(10) << metrics.trades_executed
                  << std::setw(10) << metrics.trades_settled
                  << std::setw(11) << metrics.rejected_orders
                  << std::setw(11) << exchange.pending_trade_count()
                  << std::setw(16) << eh::format_money(exchange.clearing_cash_cents())
                  << '\n';
    }
    std::cout << "\nThe payment outage leaves the first trade in escrow. BEACON then lacks "
                 "cash to buy from CIRRUS; CIRRUS cannot buy from DELTA. "
                 "Restoring payments and settling before the next order breaks the chain.\n";
    return 0;
}

int parse_case_argument(const char* raw, int min, int max) {
    const std::string value = raw;
    int number = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (error != std::errc{} || end != value.data() + value.size() ||
        number < min || number > max) {
        throw std::invalid_argument("resilience coordinate outside supported range");
    }
    return number;
}

int benchmark(int argc, char** argv) {
    std::int64_t count = 10'000;
    if (argc == 3) {
        const std::string value = argv[2];
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
        if (error != std::errc{} || end != value.data() + value.size() ||
            count < 1 || count > 10'000) {
            throw std::runtime_error("benchmark count must be between 1 and 10000");
        }
    } else if (argc != 2) {
        throw std::runtime_error("usage: event-horizon benchmark [count]");
    }

    eh::Exchange exchange;
    if (!exchange.add_account("MAKER", 0, count).ok ||
        !exchange.add_account("TAKER", count * 100, 0).ok) {
        throw std::logic_error("benchmark setup failed");
    }
    const auto started = std::chrono::steady_clock::now();
    for (std::int64_t i = 0; i < count; ++i) {
        if (!exchange.place_order("s" + std::to_string(i), "MAKER", eh::Side::sell,
                                  1, 100).ok ||
            !exchange.place_order("b" + std::to_string(i), "TAKER", eh::Side::buy,
                                  1, 100).ok) {
            throw std::logic_error("benchmark order unexpectedly rejected");
        }
    }
    exchange.settle_all();
    exchange.verify_invariants();
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << "Matched + settled " << count << " one-share trades ("
              << count * 2 << " orders) in " << std::fixed << std::setprecision(4)
              << elapsed << " s; " << std::setprecision(0)
              << (count * 2 / elapsed) << " orders/s.\n"
              << "This synthetic benchmark is NOT a throughput/SLA claim.\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "demo") return demo();
        if (argc == 3 && std::string(argv[1]) == "replay") {
            eh::Exchange exchange = eh::replay_file(argv[2], std::cout);
            eh::print_summary(exchange, std::cout);
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "list") {
            for (const auto& scenario : eh::catalog()) {
                std::cout << scenario.id << " — " << scenario.title << '\n';
            }
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "scenario") {
            std::ostringstream log;
            auto simulation = eh::run_scenario("data", argv[2]);
            std::cout << "SCENARIO: " << argv[2] << "\n";
            for (const auto& event : simulation.timeline) {
                std::cout << "line " << event.line << ": " << event.status << " " << event.message << '\n';
            }
            eh::print_summary(simulation.exchange, std::cout);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "scenario-json") {
            std::cout << eh::to_json(eh::run_scenario("data", argv[2]), argv[2]) << '\n';
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "compare-json") {
            std::cout << eh::comparison_json("data") << '\n';
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "resilience-json") {
            std::cout << eh::resilience_matrix_json() << '\n';
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "resilience-case") {
            const auto recovery = parse_case_argument(argv[2], 0, 3);
            const auto funding = parse_case_argument(argv[3], 0, 2);
            std::cout << eh::to_json(eh::run_resilience_case(recovery, funding), "resilience") << '\n';
            return 0;
        }
        if (argc >= 2 && std::string(argv[1]) == "benchmark") return benchmark(argc, argv);
        if (argc == 1 || (argc == 2 && std::string(argv[1]) == "help")) {
            usage(std::cout);
            return 0;
        }
        usage(std::cerr);
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
