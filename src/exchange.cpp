#include "event_horizon/exchange.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace event_horizon {
namespace {

constexpr std::int64_t kMaxAccountCash = 100'000'000'000'000;
constexpr std::int64_t kMaxAccountShares = 1'000'000'000;
constexpr std::int64_t kMaxOrderQuantity = 1'000'000;
constexpr std::int64_t kMaxPriceCents = 100'000'000;
constexpr std::size_t kMaxAccounts = 100;
constexpr std::size_t kMaxOrders = 10'000;

bool valid_identifier(const std::string& value) {
    if (value.empty() || value.size() > 32) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_' || ch == '-';
    });
}

bool can_add(std::int64_t a, std::int64_t b) {
    return b >= 0 && a <= std::numeric_limits<std::int64_t>::max() - b;
}

}  // namespace

std::string format_money(std::int64_t cents) {
    std::ostringstream out;
    const auto amount = cents < 0
        ? static_cast<std::uint64_t>(-(cents + 1)) + 1
        : static_cast<std::uint64_t>(cents);
    if (cents < 0) out << '-';
    out << '$' << amount / 100 << '.' << std::setw(2) << std::setfill('0') << amount % 100;
    return out.str();
}

std::string side_to_string(Side side) { return side == Side::buy ? "BUY" : "SELL"; }

Result Exchange::add_account(const std::string& name, std::int64_t cash_cents,
                             std::int64_t shares) {
    if (!valid_identifier(name)) return {false, "invalid account name"};
    if (accounts_.contains(name)) return {false, "duplicate account: " + name};
    if (accounts_.size() >= kMaxAccounts) return {false, "account limit reached"};
    if (cash_cents < 0 || cash_cents > kMaxAccountCash ||
        shares < 0 || shares > kMaxAccountShares) {
        return {false, "account balance outside supported range"};
    }
    if (!can_add(initial_cash_cents_, cash_cents) || !can_add(initial_shares_, shares)) {
        return {false, "total starting balances would overflow"};
    }
    accounts_.emplace(name, Account{cash_cents, 0, shares, 0});
    initial_cash_cents_ += cash_cents;
    initial_shares_ += shares;
    return {true, "account " + name + " created"};
}

Result Exchange::reject_order(const std::string& reason, bool risk) {
    ++metrics_.rejected_orders;
    if (risk) ++metrics_.risk_rejections;
    return {false, reason};
}

Result Exchange::set_max_order_notional(std::int64_t cents) {
    if (cents <= 0 || cents > kMaxAccountCash) return {false, "invalid notional limit"};
    max_order_notional_cents_ = cents;
    return {true, "risk notional limit updated"};
}

Result Exchange::set_price_band(std::int64_t reference, std::int64_t bps) {
    if (reference < 0 || reference > kMaxPriceCents || bps < 0 || bps > 10000 ||
        (reference == 0 && bps != 0) || (reference > 0 && bps == 0)) {
        return {false, "price band needs reference 1..100000000 and bps 1..10000, or 0,0 to disable"};
    }
    reference_price_cents_ = reference;
    deviation_bps_ = bps;
    return {true, reference == 0 ? "price band disabled" : "price band enabled"};
}

bool Exchange::price_outside_band(std::int64_t price) const {
    if (reference_price_cents_ == 0) return false;
    // Inputs are bounded; scaling to basis points remains inside int64.
    const auto difference = price > reference_price_cents_
        ? price - reference_price_cents_ : reference_price_cents_ - price;
    return difference * 10000 > reference_price_cents_ * deviation_bps_;
}

bool Exchange::crosses(const Order& incoming, const Order& resting) const {
    if (resting.remaining == 0 || incoming.side == resting.side) return false;
    return incoming.side == Side::buy ? incoming.price_cents >= resting.price_cents
                                     : incoming.price_cents <= resting.price_cents;
}

void Exchange::execute_fill(Order& incoming, Order& resting, std::int64_t qty,
                            TradeView&& prepared_trade) {
    Order& buy = incoming.side == Side::buy ? incoming : resting;
    Order& sell = incoming.side == Side::sell ? incoming : resting;
    const auto cash = resting.price_cents * qty;

    // All allocations happened before the first balance change. Both pushes
    // move prepared records into reserved vectors and cannot allocate here.
    pending_.push_back({trades_.size()});
    trades_.push_back(std::move(prepared_trade));

    Account& buyer = accounts_.at(buy.owner);
    Account& seller = accounts_.at(sell.owner);
    buyer.reserved_cash_cents -= buy.price_cents * qty;
    buyer.cash_cents -= cash;
    seller.reserved_shares -= qty;
    seller.shares -= qty;
    clearing_cash_cents_ += cash;
    clearing_shares_ += qty;
    buy.remaining -= qty;
    sell.remaining -= qty;
    ++metrics_.trades_executed;
    metrics_.traded_notional_cents += cash;
    metrics_.peak_clearing_cash_cents = std::max(
        metrics_.peak_clearing_cash_cents, clearing_cash_cents_);
}

Result Exchange::place_order(const std::string& id, const std::string& owner,
                             Side side, std::int64_t quantity, std::int64_t price_cents) {
    if (!exchange_online_) return reject_order("exchange offline");
    if (!valid_identifier(id)) return reject_order("invalid order ID");
    if (used_order_ids_.contains(id)) return reject_order("order ID already used");
    if (!accounts_.contains(owner)) return reject_order("unknown account: " + owner);
    if (quantity <= 0 || quantity > kMaxOrderQuantity ||
        price_cents <= 0 || price_cents > kMaxPriceCents) {
        return reject_order("quantity or price outside supported range");
    }
    if (active_order_count() >= kMaxOrders) return reject_order("active order limit reached");
    if (trades_.size() >= kMaxOrders) return reject_order("trade history limit reached");
    if (price_cents * quantity > max_order_notional_cents_) {
        return reject_order("order exceeds risk notional limit", true);
    }

    // Plan executions before touching balances. A worse-priced self-order or
    // out-of-band quote must not reject an order filled entirely at better prices.
    // Sorting once also avoids rescanning the whole book after every partial fill.
    Order incoming{id, owner, side, quantity, price_cents, next_sequence_};
    std::vector<std::size_t> crossing;
    for (std::size_t i = 0; i < orders_.size(); ++i) {
        if (crosses(incoming, orders_[i])) crossing.push_back(i);
    }
    std::sort(crossing.begin(), crossing.end(), [&](std::size_t a, std::size_t b) {
        const Order& left = orders_[a];
        const Order& right = orders_[b];
        if (left.price_cents != right.price_cents) {
            return side == Side::buy ? left.price_cents < right.price_cents
                                     : left.price_cents > right.price_cents;
        }
        return left.sequence < right.sequence;
    });
    struct PlannedFill { std::size_t index; std::int64_t quantity; };
    std::vector<PlannedFill> plan;
    std::int64_t unfilled = quantity;
    for (const auto index : crossing) {
        if (unfilled == 0) break;
        const Order& resting = orders_[index];
        if (resting.owner == owner) return reject_order("self-trade prevented", true);
        if (price_outside_band(resting.price_cents)) {
            return reject_order("circuit breaker: execution outside price band", true);
        }
        // Limit applies to *all* fills, not just the trade count at admission.
        if (trades_.size() + plan.size() >= kMaxOrders) {
            return reject_order("trade history limit reached");
        }
        const auto filled = std::min(unfilled, resting.remaining);
        plan.push_back({index, filled});
        unfilled -= filled;
    }

    Account& account = accounts_.at(owner);
    const auto cash_required = price_cents * quantity;
    if (side == Side::buy && cash_required > account.cash_cents - account.reserved_cash_cents) {
        return reject_order("insufficient available cash");
    }
    if (side == Side::sell && quantity > account.shares - account.reserved_shares) {
        return reject_order("insufficient available shares");
    }

    // A multi-fill order is one accounting transaction: build every record and
    // reserve vector capacity before any cash, shares or reservations change.
    // Allocation failure here leaves the engine exactly as it was before admission.
    std::vector<TradeView> prepared_trades;
    prepared_trades.reserve(plan.size());
    for (const auto& fill : plan) {
        const Order& resting = orders_[fill.index];
        const Order& buy = side == Side::buy ? incoming : resting;
        const Order& sell = side == Side::sell ? incoming : resting;
        prepared_trades.push_back(TradeView{
            static_cast<std::uint64_t>(trades_.size() + prepared_trades.size() + 1),
            buy.owner, sell.owner, buy.id, sell.id, fill.quantity,
            resting.price_cents, resting.price_cents * fill.quantity, false});
    }
    // Grow geometrically instead of reserving exactly one extra slot on each
    // order. That avoids quadratic copying during large, repeated workloads.
    const auto prepare_capacity = [](auto& items, std::size_t need) {
        if (need > items.capacity()) {
            items.reserve(std::max(need, std::max(std::size_t{16}, items.capacity() * 2)));
        }
    };
    prepare_capacity(trades_, trades_.size() + plan.size());
    prepare_capacity(pending_, pending_.size() + plan.size());
    if (unfilled > 0) prepare_capacity(orders_, orders_.size() + 1);
    // Insert the ID last: even map node allocation cannot fail after mutation.
    used_order_ids_.emplace(id, true);

    // Reserve the incoming limit price, then execute only the prechecked fills.
    if (side == Side::buy) account.reserved_cash_cents += cash_required;
    else account.reserved_shares += quantity;
    ++next_sequence_;
    for (std::size_t i = 0; i < plan.size(); ++i) {
        const auto& fill = plan[i];
        execute_fill(incoming, orders_[fill.index], fill.quantity,
                     std::move(prepared_trades[i]));
    }
    if (incoming.remaining > 0) orders_.push_back(std::move(incoming));
    std::erase_if(orders_, [](const Order& order) { return order.remaining == 0; });
    ++metrics_.accepted_orders;
    return {true, "order " + id + " accepted"};
}

Result Exchange::cancel_order(const std::string& id, const std::string& owner) {
    for (auto& order : orders_) {
        if (order.id != id || order.remaining == 0) continue;
        if (order.owner != owner) return {false, "order belongs to another account"};
        Account& account = accounts_.at(owner);
        if (order.side == Side::buy) account.reserved_cash_cents -= order.price_cents * order.remaining;
        else account.reserved_shares -= order.remaining;
        order.remaining = 0;
        std::erase_if(orders_, [](const Order& item) { return item.remaining == 0; });
        ++metrics_.canceled_orders;
        return {true, "order " + id + " canceled"};
    }
    return {false, "open order not found: " + id};
}

Result Exchange::lend(const std::string& borrower, std::int64_t cents) {
    if (borrower == "FACILITY") return {false, "facility cannot lend to itself"};
    if (!accounts_.contains("FACILITY")) return {false, "FACILITY account is missing"};
    if (!accounts_.contains(borrower)) return {false, "unknown borrower"};
    if (cents <= 0 || cents > kMaxAccountCash) return {false, "invalid loan amount"};
    Account& source = accounts_.at("FACILITY");
    Account& target = accounts_.at(borrower);
    if (source.cash_cents - source.reserved_cash_cents < cents) {
        return {false, "facility has insufficient available cash"};
    }
    const auto existing = loans_.find(borrower);
    const auto outstanding = existing == loans_.end() ? 0 : existing->second;
    if (!can_add(target.cash_cents, cents) || !can_add(outstanding, cents)) {
        return {false, "loan would overflow"};
    }
    // Rejections must not create empty debt entries in the facility ledger.
    loans_[borrower] = outstanding + cents;
    source.cash_cents -= cents;
    target.cash_cents += cents;
    ++metrics_.loan_disbursements;
    return {true, "loan disbursed to " + borrower};
}

Result Exchange::repay(const std::string& borrower, std::int64_t cents) {
    if (borrower == "FACILITY" || !accounts_.contains(borrower) ||
        !accounts_.contains("FACILITY")) return {false, "unknown borrower or facility"};
    const auto debt = loans_.find(borrower);
    if (cents <= 0 || debt == loans_.end() || debt->second < cents) {
        return {false, "repayment exceeds outstanding loan"};
    }
    Account& account = accounts_.at(borrower);
    Account& facility = accounts_.at("FACILITY");
    if (account.cash_cents - account.reserved_cash_cents < cents) {
        return {false, "insufficient available cash for repayment"};
    }
    if (!can_add(facility.cash_cents, cents)) return {false, "facility cash would overflow"};
    account.cash_cents -= cents;
    facility.cash_cents += cents;
    debt->second -= cents;
    if (debt->second == 0) loans_.erase(debt);
    ++metrics_.loan_repayments;
    return {true, "loan repaid by " + borrower};
}

SettlementResult Exchange::settle_all() { return settle_batch(pending_trade_count()); }

SettlementResult Exchange::settle_batch(std::size_t limit) {
    if (!payments_online_) {
        ++metrics_.blocked_settlements;
        return {0, true};
    }
    ++metrics_.settlement_cycles;
    std::size_t settled = 0;
    while (pending_head_ < pending_.size() && settled < limit) {
        TradeView& trade = trades_.at(pending_[pending_head_].trade_index);
        // Delivery-versus-payment: both transfers happen from escrow in one step.
        accounts_.at(trade.seller).cash_cents += trade.cash_cents;
        accounts_.at(trade.buyer).shares += trade.quantity;
        clearing_cash_cents_ -= trade.cash_cents;
        clearing_shares_ -= trade.quantity;
        trade.settled = true;
        ++pending_head_;
        ++metrics_.trades_settled;
        ++settled;
    }
    // Once all obligations are discharged, reclaim the queue slots for reuse.
    if (pending_head_ == pending_.size()) {
        pending_.clear();
        pending_head_ = 0;
    }
    return {settled, false};
}

std::vector<AccountView> Exchange::accounts() const {
    std::vector<AccountView> result;
    for (const auto& [name, a] : accounts_) {
        result.push_back({name, a.cash_cents, a.reserved_cash_cents, a.shares, a.reserved_shares});
    }
    return result;
}

std::vector<OrderView> Exchange::open_orders() const {
    std::vector<OrderView> result;
    for (const Order& order : orders_) {
        if (order.remaining == 0) continue;
        result.push_back({order.id, order.owner, order.side, order.remaining,
                          order.price_cents, order.sequence});
    }
    return result;
}

std::size_t Exchange::active_order_count() const {
    return static_cast<std::size_t>(std::count_if(orders_.begin(), orders_.end(),
        [](const Order& order) { return order.remaining > 0; }));
}

void Exchange::verify_invariants() const {
    std::int64_t total_cash = clearing_cash_cents_;
    std::int64_t total_shares = clearing_shares_;
    std::map<std::string, std::int64_t> reserved_cash;
    std::map<std::string, std::int64_t> reserved_shares;
    for (const Order& order : orders_) {
        if (order.remaining <= 0) throw std::logic_error("inactive order retained in book");
        if (order.side == Side::buy) reserved_cash[order.owner] += order.price_cents * order.remaining;
        else reserved_shares[order.owner] += order.remaining;
    }
    for (const auto& [name, account] : accounts_) {
        if (account.cash_cents < 0 || account.shares < 0 ||
            account.reserved_cash_cents < 0 || account.reserved_shares < 0 ||
            account.reserved_cash_cents > account.cash_cents ||
            account.reserved_shares > account.shares ||
            account.reserved_cash_cents != reserved_cash[name] ||
            account.reserved_shares != reserved_shares[name]) {
            throw std::logic_error("invalid balances or reservations for " + name);
        }
        total_cash += account.cash_cents;
        total_shares += account.shares;
    }
    std::int64_t pending_cash = 0;
    std::int64_t pending_shares = 0;
    for (std::size_t i = pending_head_; i < pending_.size(); ++i) {
        const auto& item = pending_[i];
        const auto& trade = trades_.at(item.trade_index);
        if (trade.settled) throw std::logic_error("settled trade still pending");
        pending_cash += trade.cash_cents;
        pending_shares += trade.quantity;
    }
    std::int64_t recorded_notional = 0;
    std::size_t settled_count = 0;
    for (const auto& trade : trades_) {
        if (trade.quantity <= 0 || trade.price_cents <= 0 ||
            trade.cash_cents != trade.price_cents * trade.quantity) {
            throw std::logic_error("invalid executed trade");
        }
        recorded_notional += trade.cash_cents;
        if (trade.settled) ++settled_count;
    }
    if (clearing_cash_cents_ != pending_cash || clearing_shares_ != pending_shares ||
        total_cash != initial_cash_cents_ || total_shares != initial_shares_) {
        throw std::logic_error("cash/shares are not conserved across clearing");
    }
    if (metrics_.trades_executed != static_cast<std::int64_t>(trades_.size()) ||
        metrics_.trades_settled != static_cast<std::int64_t>(settled_count) ||
        trades_.size() - settled_count != pending_trade_count() ||
        metrics_.traded_notional_cents != recorded_notional) {
        throw std::logic_error("trade ledger does not match settlement counters");
    }
    std::int64_t debt = 0;
    for (const auto& [borrower, balance] : loans_) {
        if (borrower == "FACILITY" || !accounts_.contains(borrower) || balance < 0) {
            throw std::logic_error("invalid facility loan");
        }
        debt += balance;
    }
    if (debt > initial_cash_cents_) throw std::logic_error("facility debt exceeds total money");
}

}  // namespace event_horizon
