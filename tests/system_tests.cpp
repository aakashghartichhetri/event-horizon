#include "event_horizon/report.hpp"
#include "event_horizon/resilience.hpp"

#include <cstdint>
#include <functional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace eh = event_horizon;
namespace {

void require(bool yes, const std::string& why) {
    if (!yes) throw std::runtime_error(why);
}

eh::AccountView account(const eh::Exchange& e, const std::string& name) {
    for (const auto& a : e.accounts()) if (a.name == name) return a;
    throw std::runtime_error("missing account " + name);
}

void test_bridge_repays_without_minting_money() {
    const auto baseline = eh::run_scenario("data", "baseline");
    const auto bridge = eh::run_scenario("data", "liquidity_bridge");
    require(bridge.exchange.metrics().trades_executed == 3, "facility should bridge all three executions");
    require(bridge.exchange.metrics().trades_settled == 3, "facility recovery should settle all trades");
    require(bridge.exchange.metrics().loan_disbursements == 2, "two bridge loans expected");
    require(bridge.exchange.metrics().loan_repayments == 2, "two repayments expected");
    require(bridge.exchange.clearing_cash_cents() == 0, "clearing balance must zero after recovery");
    require(account(bridge.exchange, "FACILITY").cash_cents == 100000, "facility must recover principal");
    require(bridge.exchange.initial_cash_cents() == baseline.exchange.initial_cash_cents(),
            "baseline and bridge must have identical starting cash");
    for (const auto& [name, cents] : bridge.exchange.outstanding_loans()) {
        require(cents == 0, "no unrepaid principal for " + name);
    }
    bridge.exchange.verify_invariants();
}

void test_liquidity_lock_propagates() {
    const auto simulation = eh::run_scenario("data", "liquidity_shock");
    const auto& m = simulation.exchange.metrics();
    require(m.trades_executed == 0, "locked buyer cannot execute first trade");
    require(m.rejected_orders == 3, "each intended buyer should fail");
    require(account(simulation.exchange, "NOVA").available_cash_cents() == 100000,
            "canceling the cash lock should restore the balance");
    simulation.exchange.verify_invariants();
}

void test_circuit_breaker_rejects_before_trading() {
    const auto simulation = eh::run_scenario("data", "circuit_breaker");
    require(simulation.exchange.metrics().trades_executed == 1, "only within-band trade can execute");
    require(simulation.exchange.metrics().risk_rejections == 2, "two executions are outside band");
    require(simulation.exchange.pending_trade_count() == 0, "first trade should settle");
    eh::Exchange e;
    require(!e.set_price_band(0, 100).ok, "invalid band configuration must fail");
    require(e.set_price_band(10000, 1000).ok, "valid band must apply");
    require(e.add_account("B", 100000, 0).ok, "buyer");
    require(e.add_account("S", 0, 10).ok, "seller");
    require(e.place_order("sell", "S", eh::Side::sell, 1, 12000).ok, "resting ask");
    require(!e.place_order("buy", "B", eh::Side::buy, 1, 13000).ok,
            "out-of-band trade must be rejected before debit");
    require(account(e, "B").reserved_cash_cents == 0, "rejected order cannot lock funds");
    e.verify_invariants();
}

void test_notional_limit() {
    eh::Exchange e;
    e.add_account("B", 10000, 0);
    e.add_account("S", 0, 10);
    require(e.set_max_order_notional(5000).ok, "risk limit");
    require(!e.place_order("too_big", "B", eh::Side::buy, 6, 1000).ok, "risk rejects large order");
    require(e.place_order("ok", "B", eh::Side::buy, 5, 1000).ok, "border is inclusive");
    require(e.metrics().risk_rejections == 1, "risk metric");
    e.verify_invariants();
}

void test_batch_settlement_fifo() {
    auto simulation = eh::run_scenario("data", "settlement_throttle");
    auto& e = simulation.exchange;
    require(e.metrics().trades_executed == 3, "three executions");
    require(e.metrics().trades_settled == 2, "capacity two cycles settles two");
    require(e.pending_trade_count() == 1, "one trade remains queued");
    require(e.clearing_cash_cents() == 7000, "third trade $70 still in escrow");
    require(e.trades()[0].settled && e.trades()[1].settled && !e.trades()[2].settled,
            "FIFO settlement order");
    require(e.settle_batch(1).settled == 1, "remaining trade settles on next cycle");
    require(e.pending_trade_count() == 0, "clearing emptied");
    e.verify_invariants();
}

void test_agents_wait_for_cash() {
    auto simulation = eh::run_scenario("data", "agents");
    require(simulation.exchange.metrics().trades_executed == 3, "agents should buy in sequence");
    require(simulation.exchange.metrics().rejected_orders == 0, "unfunded agents wait, not submit");
    require(simulation.exchange.metrics().trades_settled == 3, "all agent trades settle");
    require(simulation.exchange.active_order_count() == 0, "one-shot agents do not flood book");
    simulation.exchange.verify_invariants();
}

void test_loan_cannot_spend_reserved_cash() {
    eh::Exchange e;
    e.add_account("FACILITY", 20000, 0);
    e.add_account("A", 1000, 0);
    e.add_account("B", 0, 0);
    require(e.lend("A", 5000).ok, "loan disbursed");
    require(e.place_order("bid", "A", eh::Side::buy, 1, 6000).ok, "lock all borrower cash");
    require(!e.repay("A", 1000).ok, "reserved cash cannot repay debt");
    require(e.cancel_order("bid", "A").ok, "unlock");
    require(e.repay("A", 5000).ok, "can repay after unlocking");
    require(account(e, "FACILITY").cash_cents == 20000, "facility principal conserved");
    e.verify_invariants();
}

void test_json_is_reproducible_and_escaped() {
    const auto first = eh::to_json(eh::run_scenario("data", "baseline"), "baseline");
    const auto second = eh::to_json(eh::run_scenario("data", "baseline"), "baseline");
    require(first == second, "replay JSON must be byte-identical");
    require(first.find("\"passed\":true") != std::string::npos, "invariants flag must appear");
    require(eh::json_escape("a\"b\\c\n") == "\"a\\\"b\\\\c\\n\"", "escape JSON controls");
    require(eh::catalog_json().find("liquidity_bridge") != std::string::npos, "catalog contains bridge");
}

void test_strict_errors_and_size_limits() {
    try {
        (void)eh::run_text("ACCOUNT,B,100,0\nPLACE,x,B,WHAT,1,100\n");
        throw std::runtime_error("invalid CSV must throw");
    } catch (const std::runtime_error& e) {
        require(std::string(e.what()).find("line 2: side must be BUY or SELL") != std::string::npos,
                "invalid syntax must include source line");
    }
    try {
        (void)eh::run_text("ACCOUNT,B,100,0\nSETTLE\nACCOUNT,A,1,1\n");
        throw std::runtime_error("late account must throw");
    } catch (const std::runtime_error& e) {
        require(std::string(e.what()).find("line 3") != std::string::npos,
                "late account needs line number");
    }
    try {
        (void)eh::run_text(std::string("# comment ") + "\xff" + "\nACCOUNT,A,1,0\n");
        throw std::runtime_error("non-ASCII scenario must fail before JSON generation");
    } catch (const std::invalid_argument&) { }
    try {
        (void)eh::run_text(std::string(65537, 'X'));
        throw std::runtime_error("oversized scenario must fail");
    } catch (const std::invalid_argument&) { }
    try {
        (void)eh::run_scenario("data", "../../etc/passwd");
        throw std::runtime_error("unlisted scenario must fail");
    } catch (const std::invalid_argument&) { }
}

void test_randomized_accounting_invariants() {
    std::mt19937 random(20261118);
    for (int round = 0; round < 40; ++round) {
        eh::Exchange e;
        for (const char* name : {"A", "B", "C", "D"}) {
            require(e.add_account(name, 1000000, 1000).ok, "create participant");
        }
        const std::vector<std::string> names = {"A", "B", "C", "D"};
        for (int i = 0; i < 100; ++i) {
            const auto owner = names[random() % names.size()];
            const auto side = random() % 2 ? eh::Side::buy : eh::Side::sell;
            const auto qty = static_cast<std::int64_t>(random() % 7 + 1);
            const auto price = static_cast<std::int64_t>((random() % 50 + 75) * 100);
            (void)e.place_order("order" + std::to_string(i), owner, side, qty, price);
            if (i % 9 == 0) (void)e.settle_batch(2);
            if (i % 20 == 0) (void)e.cancel_order("order" + std::to_string(i), owner);
            if (i % 15 == 0) e.set_payments_online(!e.payments_online());
            e.verify_invariants();
        }
        e.set_payments_online(true);
        e.settle_all();
        e.verify_invariants();
    }
}

void test_parallel_replays_independent() {
    const auto baseline = eh::to_json(eh::run_scenario("data", "baseline"), "baseline");
    std::vector<std::thread> threads;
    std::vector<std::uint8_t> results(12, 0);
    for (std::size_t i = 0; i < results.size(); ++i) {
        threads.emplace_back([&results, &baseline, i] {
            results[i] = eh::to_json(eh::run_scenario("data", "baseline"), "baseline") == baseline;
        });
    }
    for (auto& thread : threads) thread.join();
    for (const bool result : results) require(result, "parallel replay changed output");
}


void test_recovery_matrix_and_snapshots() {
    const auto baseline = eh::run_scenario("data", "baseline");
    const auto initial_cash = baseline.exchange.initial_cash_cents();
    const auto initial_shares = baseline.exchange.initial_shares();
    for (int recovery = 0; recovery < 4; ++recovery) {
        for (int funding = 0; funding < 3; ++funding) {
            const auto simulation = eh::run_resilience_case(recovery, funding);
            const auto& e = simulation.exchange;
            e.verify_invariants();
            require(e.initial_cash_cents() == initial_cash, "stress cases start with same cash");
            require(e.initial_shares() == initial_shares, "stress cases start with same shares");
            require(!simulation.timeline.empty(), "stress case must have events");
            require(simulation.timeline.back().accounts.size() == e.accounts().size(),
                    "final snapshot must contain all accounts");
            if (recovery == 0) {
                require(e.metrics().trades_settled == 3, "early recovery settles three trades");
                require(e.metrics().rejected_orders == 0, "early recovery prevents rejects");
            }
            if (recovery == 2 && funding == 0) {
                require(e.metrics().trades_executed == 1, "unfunded late recovery has cascade");
                require(e.metrics().rejected_orders == 2, "unfunded late recovery rejects two orders");
            }
            if (recovery == 2 && funding == 2) {
                require(e.metrics().trades_settled == 3, "fully funded late recovery settles all");
                require(e.metrics().rejected_orders == 0, "full facility prevents rejects");
                for (const auto& [name, cents] : e.outstanding_loans()) {
                    require(cents == 0, "funded late recovery must repay " + name);
                }
            }
            if (recovery == 3) {
                require(e.metrics().trades_settled == 0, "permanent outage cannot settle");
                require(e.metrics().trades_executed == funding + 1,
                        "loans support one extra matched trade per funded participant");
            }
        }
    }
    require(eh::resilience_matrix_json() == eh::resilience_matrix_json(),
            "controlled matrix is deterministic");
    try {
        (void)eh::run_resilience_case(9, 0);
        throw std::runtime_error("invalid recovery stage must be rejected");
    } catch (const std::invalid_argument&) { }

    const auto recovered = eh::run_scenario("data", "recovery");
    bool saw_before = false, saw_after = false;
    for (const auto& event : recovered.timeline) {
        if (event.command == "PAYMENTS_DOWN") {
            require(account(recovered.exchange, "BEACON").cash_cents == 9000,
                    "final recovery position unchanged");
            for (const auto& entry : event.accounts) {
                if (entry.name == "BEACON") {
                    saw_before = entry.available_cash_cents() == 5000;
                }
            }
        }
        if (event.command == "SETTLE" && event.trades_settled == 1) {
            for (const auto& entry : event.accounts) {
                if (entry.name == "BEACON") {
                    saw_after = entry.available_cash_cents() == 45000;
                }
            }
        }
    }
    require(saw_before && saw_after, "event snapshots must preserve historical funding");
    const auto json = eh::to_json(recovered, "recovery");
    require(json.find("\"accounts\":[") != std::string::npos,
            "historical snapshots must be serialized");
}
}  // namespace

void register_system_tests(std::vector<std::pair<std::string, std::function<void()>>>& tests) {
    tests.insert(tests.end(), {
        {"funded bridge and repayment", test_bridge_repays_without_minting_money},
        {"liquidity reservation cascade", test_liquidity_lock_propagates},
        {"circuit breaker", test_circuit_breaker_rejects_before_trading},
        {"notional risk limit", test_notional_limit},
        {"batch settlement FIFO", test_batch_settlement_fifo},
        {"deterministic agent execution", test_agents_wait_for_cash},
        {"loans cannot spend reservations", test_loan_cannot_spend_reserved_cash},
        {"deterministic JSON escaping", test_json_is_reproducible_and_escaped},
        {"strict input parsing", test_strict_errors_and_size_limits},
        {"randomized conservation (40 x 100)", test_randomized_accounting_invariants},
        {"parallel independent scenarios", test_parallel_replays_independent},
        {"resilience matrix and historical balances", test_recovery_matrix_and_snapshots},
    });
}
