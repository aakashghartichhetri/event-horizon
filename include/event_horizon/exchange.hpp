#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace event_horizon {

enum class Side { buy, sell };

struct Result {
    bool ok;
    std::string message;
};

struct SettlementResult {
    std::size_t settled;
    bool blocked;
};

struct Metrics {
    std::int64_t accepted_orders = 0;
    std::int64_t rejected_orders = 0;
    std::int64_t canceled_orders = 0;
    std::int64_t trades_executed = 0;
    std::int64_t trades_settled = 0;
    std::int64_t blocked_settlements = 0;
    std::int64_t settlement_cycles = 0;
    std::int64_t loan_disbursements = 0;
    std::int64_t loan_repayments = 0;
    std::int64_t risk_rejections = 0;
    std::int64_t traded_notional_cents = 0;
    std::int64_t peak_clearing_cash_cents = 0;
};

struct AccountView {
    std::string name;
    std::int64_t cash_cents;
    std::int64_t reserved_cash_cents;
    std::int64_t shares;
    std::int64_t reserved_shares;
    std::int64_t available_cash_cents() const { return cash_cents - reserved_cash_cents; }
    std::int64_t available_shares() const { return shares - reserved_shares; }
};

struct OrderView {
    std::string id;
    std::string owner;
    Side side;
    std::int64_t remaining;
    std::int64_t price_cents;
    std::uint64_t sequence;
};

struct TradeView {
    std::uint64_t id;
    std::string buyer;
    std::string seller;
    std::string buy_order_id;
    std::string sell_order_id;
    std::int64_t quantity;
    std::int64_t price_cents;
    std::int64_t cash_cents;
    bool settled;
};

class Exchange {
public:
    Result add_account(const std::string& name, std::int64_t cash_cents,
                       std::int64_t shares);
    Result place_order(const std::string& id, const std::string& owner, Side side,
                       std::int64_t quantity, std::int64_t price_cents);
    Result cancel_order(const std::string& id, const std::string& owner);

    // Loans transfer existing cash from FACILITY: no money is created.
    Result lend(const std::string& borrower, std::int64_t cents);
    Result repay(const std::string& borrower, std::int64_t cents);
    std::map<std::string, std::int64_t> outstanding_loans() const { return loans_; }

    Result set_max_order_notional(std::int64_t cents);
    Result set_price_band(std::int64_t reference_cents, std::int64_t deviation_bps);
    std::int64_t max_order_notional_cents() const { return max_order_notional_cents_; }
    std::int64_t reference_price_cents() const { return reference_price_cents_; }
    std::int64_t deviation_bps() const { return deviation_bps_; }

    void set_exchange_online(bool online) { exchange_online_ = online; }
    void set_payments_online(bool online) { payments_online_ = online; }
    bool exchange_online() const { return exchange_online_; }
    bool payments_online() const { return payments_online_; }

    SettlementResult settle_all();
    SettlementResult settle_batch(std::size_t limit);
    std::vector<AccountView> accounts() const;
    std::vector<OrderView> open_orders() const;
    const std::vector<TradeView>& trades() const { return trades_; }
    const Metrics& metrics() const { return metrics_; }
    std::size_t pending_trade_count() const { return pending_.size(); }
    std::int64_t clearing_cash_cents() const { return clearing_cash_cents_; }
    std::int64_t clearing_shares() const { return clearing_shares_; }
    std::int64_t initial_cash_cents() const { return initial_cash_cents_; }
    std::int64_t initial_shares() const { return initial_shares_; }
    std::size_t active_order_count() const;

    // Called after every replay event; throws if any conservation rule fails.
    void verify_invariants() const;

private:
    struct Account {
        std::int64_t cash_cents;
        std::int64_t reserved_cash_cents = 0;
        std::int64_t shares;
        std::int64_t reserved_shares = 0;
    };

    struct Order {
        std::string id;
        std::string owner;
        Side side;
        std::int64_t remaining;
        std::int64_t price_cents;
        std::uint64_t sequence;
    };

    struct PendingTrade {
        std::size_t trade_index;
    };

    bool crosses(const Order& incoming, const Order& resting) const;
    Order* best_match(const Order& incoming);
    void execute_fill(Order& incoming, Order& resting, std::int64_t quantity);
    Result reject_order(const std::string& reason, bool risk = false);
    bool price_outside_band(std::int64_t price) const;

    std::map<std::string, Account> accounts_;
    std::vector<Order> orders_;
    std::map<std::string, bool> used_order_ids_;
    std::deque<PendingTrade> pending_;
    std::vector<TradeView> trades_;
    std::map<std::string, std::int64_t> loans_;
    Metrics metrics_;
    std::uint64_t next_sequence_ = 0;
    std::int64_t initial_cash_cents_ = 0;
    std::int64_t initial_shares_ = 0;
    std::int64_t clearing_cash_cents_ = 0;
    std::int64_t clearing_shares_ = 0;
    std::int64_t max_order_notional_cents_ = 100'000'000'000'000;
    std::int64_t reference_price_cents_ = 0; // 0 disables price circuit breaker.
    std::int64_t deviation_bps_ = 0;
    bool exchange_online_ = true;
    bool payments_online_ = true;
};

std::string format_money(std::int64_t cents);
std::string side_to_string(Side side);

}  // namespace event_horizon
