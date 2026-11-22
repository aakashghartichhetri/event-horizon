#include "event_horizon/exchange.hpp"
#include "event_horizon/replay.hpp"

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace eh = event_horizon;

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

const eh::AccountView& find_account(const std::vector<eh::AccountView>& accounts,
                                    const std::string& name) {
    for (const auto& account : accounts) {
        if (account.name == name) return account;
    }
    throw std::runtime_error("account not found in result: " + name);
}

void test_baseline() {
    std::ostringstream log;
    eh::Exchange exchange = eh::replay_file("data/baseline.csv", log);
    exchange.verify_invariants();
    check(exchange.metrics().trades_executed == 3, "baseline should execute three trades");
    check(exchange.metrics().trades_settled == 3, "baseline should settle three trades");
    check(exchange.metrics().rejected_orders == 0, "baseline should reject no orders");
    check(exchange.clearing_cash_cents() == 0, "baseline clearing must be empty");
    const auto accounts = exchange.accounts();
    check(find_account(accounts, "NOVA").cash_cents == 60000, "NOVA cash mismatch");
    check(find_account(accounts, "NOVA").shares == 4, "NOVA shares mismatch");
    check(find_account(accounts, "BEACON").cash_cents == 9000, "BEACON cash mismatch");
    check(find_account(accounts, "BEACON").shares == 4, "BEACON shares mismatch");
    check(find_account(accounts, "CIRRUS").cash_cents == 9000, "CIRRUS cash mismatch");
    check(find_account(accounts, "CIRRUS").shares == 4, "CIRRUS shares mismatch");
    check(find_account(accounts, "DELTA").cash_cents == 32500, "DELTA cash mismatch");
    check(find_account(accounts, "DELTA").shares == 3, "DELTA shares mismatch");
}

void test_outage_propagates() {
    std::ostringstream log;
    eh::Exchange exchange = eh::replay_file("data/payments_outage.csv", log);
    check(exchange.metrics().trades_executed == 1, "outage should execute only first trade");
    check(exchange.metrics().trades_settled == 0, "outage must prevent settlement");
    check(exchange.metrics().rejected_orders == 2, "outage should block two buys");
    check(exchange.metrics().blocked_settlements == 3, "blocked settlement count wrong");
    check(exchange.pending_trade_count() == 1, "first trade should be pending");
    check(exchange.clearing_cash_cents() == 40000, "cash escrow should hold $400");
    check(exchange.clearing_shares() == 4, "share escrow should hold four shares");
    check(log.str().find("insufficient available cash") != std::string::npos,
          "replay should explain liquidity failure");
    exchange.verify_invariants();
}

void test_recovery() {
    std::ostringstream log;
    eh::Exchange exchange = eh::replay_file("data/recovery.csv", log);
    check(exchange.metrics().trades_executed == 3, "recovery should execute all trades");
    check(exchange.metrics().trades_settled == 3, "recovery should settle all trades");
    check(exchange.metrics().blocked_settlements == 1, "recovery should have one block");
    check(exchange.metrics().rejected_orders == 0, "recovery should reject none");
    check(exchange.pending_trade_count() == 0, "recovery escrow should be empty");
}

void test_exchange_outage() {
    std::ostringstream log;
    eh::Exchange exchange = eh::replay_file("data/exchange_outage.csv", log);
    check(exchange.metrics().accepted_orders == 2, "exchange outage accepted wrong count");
    check(exchange.metrics().rejected_orders == 1, "exchange outage should reject one order");
    check(exchange.metrics().trades_settled == 1, "exchange recovery should settle one trade");
}

void test_price_priority_and_resting_price() {
    eh::Exchange exchange;
    check(exchange.add_account("B", 20000, 0).ok, "setup buyer");
    check(exchange.add_account("A", 0, 1).ok, "setup seller A");
    check(exchange.add_account("C", 0, 1).ok, "setup seller C");
    check(exchange.place_order("ask90", "A", eh::Side::sell, 1, 9000).ok, "ask90");
    check(exchange.place_order("ask80", "C", eh::Side::sell, 1, 8000).ok, "ask80");
    check(exchange.place_order("bid100", "B", eh::Side::buy, 1, 10000).ok, "bid100");
    check(exchange.clearing_cash_cents() == 8000, "best price should be $80");
    const auto before = exchange.accounts();
    check(find_account(before, "B").available_cash_cents() == 12000,
          "buyer should receive price improvement refund");
    check(exchange.active_order_count() == 1, "ask90 should remain open");
    exchange.settle_all();
    check(find_account(exchange.accounts(), "C").cash_cents == 8000,
          "lower-price seller should be paid");
    exchange.verify_invariants();
}

void test_fifo_same_price() {
    eh::Exchange exchange;
    exchange.add_account("A", 0, 1);
    exchange.add_account("B", 0, 1);
    exchange.add_account("C", 10000, 0);
    exchange.place_order("first", "A", eh::Side::sell, 1, 10000);
    exchange.place_order("second", "B", eh::Side::sell, 1, 10000);
    exchange.place_order("buy", "C", eh::Side::buy, 1, 10000);
    exchange.settle_all();
    check(find_account(exchange.accounts(), "A").cash_cents == 10000,
          "oldest ask should fill first");
    check(find_account(exchange.accounts(), "B").cash_cents == 0,
          "newer ask should remain unfilled");
    exchange.verify_invariants();
}

void test_reservations_and_cancellation() {
    eh::Exchange exchange;
    exchange.add_account("A", 12000, 2);
    check(exchange.place_order("bid", "A", eh::Side::buy, 2, 5000).ok, "place bid");
    check(!exchange.place_order("bid2", "A", eh::Side::buy, 1, 3000).ok,
          "cannot spend reserved cash twice");
    check(find_account(exchange.accounts(), "A").available_cash_cents() == 2000,
          "available cash mismatch");
    check(!exchange.cancel_order("bid", "B").ok, "other owner cannot cancel");
    check(exchange.cancel_order("bid", "A").ok, "cancel unlocks cash");
    check(find_account(exchange.accounts(), "A").available_cash_cents() == 12000,
          "cash should unlock on cancel");
    check(exchange.place_order("ask", "A", eh::Side::sell, 2, 5000).ok,
          "seller may reserve shares");
    check(!exchange.place_order("ask2", "A", eh::Side::sell, 1, 5000).ok,
          "cannot sell reserved shares twice");
    check(exchange.cancel_order("ask", "A").ok, "sell cancellation");
    check(find_account(exchange.accounts(), "A").available_shares() == 2,
          "shares should unlock on cancel");
    exchange.verify_invariants();
}

void test_partial_fill_accounting() {
    eh::Exchange exchange;
    exchange.add_account("B", 10000, 0);
    exchange.add_account("S", 0, 2);
    exchange.place_order("sell", "S", eh::Side::sell, 2, 1000);
    exchange.place_order("buy", "B", eh::Side::buy, 5, 1500);
    const auto accounts = exchange.accounts();
    check(find_account(accounts, "B").cash_cents == 8000, "filled part debited $20");
    check(find_account(accounts, "B").reserved_cash_cents == 4500,
          "three shares remain reserved at bid limit");
    check(find_account(accounts, "B").available_cash_cents() == 3500,
          "price improvement must be available");
    check(exchange.cancel_order("buy", "B").ok, "cancel unfilled remainder");
    check(find_account(exchange.accounts(), "B").available_cash_cents() == 8000,
          "cancel should not refund already-executed cash");
    exchange.settle_all();
    check(find_account(exchange.accounts(), "B").shares == 2,
          "shares credited only after clearing");
    exchange.verify_invariants();
}

void test_payment_block_preserves_escrow() {
    eh::Exchange exchange;
    exchange.add_account("B", 10000, 0);
    exchange.add_account("S", 0, 1);
    exchange.place_order("s", "S", eh::Side::sell, 1, 5000);
    exchange.place_order("b", "B", eh::Side::buy, 1, 5000);
    exchange.set_payments_online(false);
    const auto result = exchange.settle_all();
    check(result.blocked && result.settled == 0, "settle should be blocked");
    check(find_account(exchange.accounts(), "B").shares == 0, "no premature share credit");
    check(find_account(exchange.accounts(), "S").cash_cents == 0, "no premature cash credit");
    exchange.verify_invariants();
    exchange.set_payments_online(true);
    check(exchange.settle_all().settled == 1, "recovery should settle pending trade");
    exchange.verify_invariants();
}

void test_invalid_and_self_trading() {
    eh::Exchange exchange;
    check(!exchange.add_account("bad name", 1, 0).ok, "whitespace in account name");
    check(!exchange.add_account("A", -1, 0).ok, "negative starting cash");
    check(exchange.add_account("A", 10000, 2).ok, "valid account");
    check(!exchange.add_account("A", 0, 0).ok, "duplicate account");
    check(!exchange.place_order("x", "A", eh::Side::buy, 0, 10).ok, "zero quantity");
    check(!exchange.place_order("x", "A", eh::Side::buy, 1, 0).ok, "zero price");
    check(!exchange.place_order("x", "A", eh::Side::buy, 1, 100000001).ok,
          "price beyond safe limit");
    check(exchange.place_order("sell", "A", eh::Side::sell, 1, 100).ok,
          "place self-owned ask");
    check(!exchange.place_order("buy", "A", eh::Side::buy, 1, 100).ok,
          "self-trade should be rejected");
    check(!exchange.place_order("sell", "A", eh::Side::sell, 1, 100).ok,
          "duplicate order ID should be rejected");
    exchange.set_exchange_online(false);
    check(!exchange.place_order("offline", "A", eh::Side::buy, 1, 100).ok,
          "offline exchange rejects orders");
    check(exchange.cancel_order("sell", "A").ok, "can cancel in exchange outage");
    exchange.verify_invariants();
}

void test_replay_errors_have_line_numbers() {
    eh::Exchange exchange;
    std::istringstream input("# comment\nACCOUNT,A,100,0\nPLACE,x,A,LONG,1,10\n");
    std::ostringstream output;
    try {
        eh::replay_events(input, exchange, output);
        throw std::runtime_error("invalid side unexpectedly accepted");
    } catch (const std::runtime_error& error) {
        check(std::string(error.what()).find("line 3: side must be BUY or SELL") !=
                  std::string::npos,
              "error should identify malformed line");
    }
    exchange.verify_invariants();
}

void test_replay_is_deterministic() {
    const std::string script =
        "ACCOUNT,A,10000,0\nACCOUNT,B,0,2\n"
        "PLACE,s,B,SELL,2,3000\nPLACE,b,A,BUY,2,4000\nSETTLE\n";
    eh::Exchange first;
    eh::Exchange second;
    std::istringstream input1(script);
    std::istringstream input2(script);
    std::ostringstream log1;
    std::ostringstream log2;
    eh::replay_events(input1, first, log1);
    eh::replay_events(input2, second, log2);
    std::ostringstream output1;
    std::ostringstream output2;
    eh::print_summary(first, output1);
    eh::print_summary(second, output2);
    check(log1.str() == log2.str(), "event logs should reproduce exactly");
    check(output1.str() == output2.str(), "balances should reproduce exactly");
    check(find_account(first.accounts(), "A").cash_cents == 4000,
          "trade must use resting sell price");
}

void test_order_id_cannot_be_reused_after_cancel() {
    eh::Exchange exchange;
    exchange.add_account("A", 10000, 0);
    exchange.place_order("id", "A", eh::Side::buy, 1, 100);
    exchange.cancel_order("id", "A");
    check(!exchange.place_order("id", "A", eh::Side::buy, 1, 100).ok,
          "order IDs should be unique throughout a replay");
    exchange.verify_invariants();
}

void test_unreachable_self_trade_does_not_reject() {
    eh::Exchange e;
    check(e.add_account("BUYER", 2000, 1).ok, "buyer account");
    check(e.add_account("SELLER", 0, 1).ok, "seller account");
    check(e.place_order("cheap", "SELLER", eh::Side::sell, 1, 90).ok, "first ask");
    check(e.place_order("own", "BUYER", eh::Side::sell, 1, 100).ok, "own worse ask");
    check(e.place_order("bid", "BUYER", eh::Side::buy, 1, 110).ok,
          "fully filled before reaching own quote must be accepted");
    check(e.trades().size() == 1 && e.trades()[0].seller == "SELLER",
          "execution should use best eligible seller");
    check(e.open_orders().size() == 1 && e.open_orders()[0].id == "own",
          "worse own ask remains on book");
    e.verify_invariants();
}

void test_unreachable_price_band_does_not_reject() {
    eh::Exchange e;
    check(e.add_account("BUYER", 2000, 0).ok, "buyer");
    check(e.add_account("FIRST", 0, 1).ok, "first seller");
    check(e.add_account("SECOND", 0, 1).ok, "second seller");
    check(e.set_price_band(100, 1000).ok, "10 percent fixed band");
    check(e.place_order("in-band", "FIRST", eh::Side::sell, 1, 100).ok, "first ask");
    check(e.place_order("out-band", "SECOND", eh::Side::sell, 1, 120).ok, "second ask");
    check(e.place_order("bid", "BUYER", eh::Side::buy, 1, 120).ok,
          "better in-band fill should not see unreachable out-of-band ask");
    check(e.trades().size() == 1 && e.trades()[0].price_cents == 100,
          "only the in-band trade should execute");
    e.verify_invariants();
}

void test_multifill_rejection_is_atomic() {
    eh::Exchange e;
    check(e.add_account("BUYER", 3000, 0).ok, "buyer");
    check(e.add_account("SELLER", 0, 1).ok, "seller");
    check(e.add_account("OWN", 0, 1).ok, "second seller");
    check(e.place_order("one", "SELLER", eh::Side::sell, 1, 100).ok, "first ask");
    check(e.place_order("two", "OWN", eh::Side::sell, 1, 120).ok, "second ask");
    check(e.set_price_band(100, 1000).ok, "set band");
    check(!e.place_order("buy", "BUYER", eh::Side::buy, 2, 120).ok,
          "second out-of-band fill must reject whole order");
    check(e.trades().empty(), "no partial trade should escape rejected incoming order");
    check(e.open_orders().size() == 2, "book stays unchanged");
    check(find_account(e.accounts(), "BUYER").reserved_cash_cents == 0,
          "rejected bid cannot reserve cash");
    e.verify_invariants();
}

void test_trades_limit_covers_all_fills() {
    eh::Exchange e;
    check(e.add_account("B", 2'000'000, 0).ok, "buyer");
    check(e.add_account("S", 0, 10'001).ok, "seller");
    for (int i = 0; i < 9'999; ++i) {
        check(e.place_order("s" + std::to_string(i), "S", eh::Side::sell, 1, 100).ok,
              "resting sell");
        check(e.place_order("b" + std::to_string(i), "B", eh::Side::buy, 1, 100).ok,
              "matching buy");
    }
    check(e.place_order("last1", "S", eh::Side::sell, 1, 100).ok, "first pending ask");
    check(e.place_order("last2", "S", eh::Side::sell, 1, 100).ok, "second pending ask");
    check(!e.place_order("over", "B", eh::Side::buy, 2, 100).ok,
          "one order cannot exceed trade-history cap with multiple fills");
    check(e.trades().size() == 9'999, "trade count cannot overshoot cap");
    check(e.open_orders().size() == 2, "rejected order leaves remaining asks");
    e.verify_invariants();
}

}  // namespace

void register_system_tests(std::vector<std::pair<std::string, std::function<void()>>>& tests);

int main() {
    std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"baseline", test_baseline},
        {"payments outage propagation", test_outage_propagates},
        {"early recovery", test_recovery},
        {"exchange outage", test_exchange_outage},
        {"price priority and refund", test_price_priority_and_resting_price},
        {"FIFO", test_fifo_same_price},
        {"reservations and cancel", test_reservations_and_cancellation},
        {"partial fill", test_partial_fill_accounting},
        {"payment escrow", test_payment_block_preserves_escrow},
        {"invalid input and self-trade", test_invalid_and_self_trading},
        {"line errors", test_replay_errors_have_line_numbers},
        {"deterministic replay", test_replay_is_deterministic},
        {"unique order IDs", test_order_id_cannot_be_reused_after_cancel},
        {"unreachable self-trade", test_unreachable_self_trade_does_not_reject},
        {"unreachable price band", test_unreachable_price_band_does_not_reject},
        {"multi-fill admission atomicity", test_multifill_rejection_is_atomic},
        {"trade-history limit on multi-fill", test_trades_limit_covers_all_fills},
    };
    register_system_tests(tests);
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << "\n" << tests.size() - failures << '/' << tests.size()
              << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
