#include "event_horizon/report.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace event_horizon {
namespace {

const std::vector<Scenario> kCatalog = {
    {"baseline", "Healthy network", "All three trades settle; incoming proceeds fund subsequent purchases."},
    {"payments_outage", "Payments outage", "Settlement stalls, trapping cash and cascading into rejected orders."},
    {"recovery", "Early recovery", "Restoring settlement before the next order interrupts the cascade."},
    {"liquidity_bridge", "Liquidity backstop", "A funded liquidity facility bridges participants until payments recover."},
    {"liquidity_shock", "Cash reservation shock", "An unrelated order locks buyer cash and prevents downstream trades."},
    {"circuit_breaker", "Price-band circuit breaker", "Rejects an execution outside the allowed reference-price band."},
    {"exchange_outage", "Exchange outage", "The matching service rejects incoming orders during downtime."},
    {"settlement_throttle", "Settlement capacity", "Only two of three pending trades settle under a per-cycle capacity limit."},
    {"agents", "Autonomous strategies", "Three deterministic single-shot agents wait for funding, then place orders."},
};

std::string bool_json(bool value) { return value ? "true" : "false"; }

}  // namespace

std::string json_escape(std::string_view text) {
    std::ostringstream out;
    out << '"';
    for (char byte : text) {
        const auto ch = static_cast<unsigned char>(byte);
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<unsigned>(ch) << std::dec;
            } else out << static_cast<char>(ch);
        }
    }
    return out.str() + '"';
}

const std::vector<Scenario>& catalog() { return kCatalog; }

Simulation run_scenario(const std::string& data_directory, const std::string& id) {
    bool known = false;
    for (const Scenario& entry : kCatalog) if (entry.id == id) known = true;
    if (!known) throw std::invalid_argument("unknown scenario: " + id);
    const std::string path = data_directory + "/" + id + ".csv";
    std::ifstream input(path);
    if (!input) throw std::runtime_error("scenario is unavailable: " + path);
    std::ostringstream source;
    source << input.rdbuf();
    if (!input && !input.eof()) throw std::runtime_error("failed reading scenario");
    return run_text(source.str());
}

Simulation run_text(const std::string& csv) {
    if (csv.empty() || csv.size() > 65'536) {
        throw std::invalid_argument("scenario must contain 1..65536 bytes");
    }
    // The replay grammar is ASCII-only. Reject unvalidated bytes rather than
    // emitting invalid UTF-8 in an API JSON response.
    for (char byte : csv) {
        const auto ch = static_cast<unsigned char>(byte);
        if ((ch < 0x20 && ch != '\n' && ch != '\r' && ch != '\t') || ch >= 0x7f) {
            throw std::invalid_argument("scenario requires printable ASCII and line breaks");
        }
    }
    Simulation result;
    result.source = csv;
    std::istringstream input(csv);
    std::ostringstream log;
    replay_events(input, result.exchange, log, &result.timeline);
    return result;
}

std::string to_json(const Simulation& s, std::string_view scenario_id) {
    const Exchange& e = s.exchange;
    const auto& m = e.metrics();
    std::ostringstream out;
    out << "{\"scenario\":" << json_escape(scenario_id)
        << ",\"source\":" << json_escape(s.source)
        << ",\"exchange_online\":" << bool_json(e.exchange_online())
        << ",\"payments_online\":" << bool_json(e.payments_online())
        << ",\"metrics\":{"
        << "\"accepted_orders\":" << m.accepted_orders
        << ",\"rejected_orders\":" << m.rejected_orders
        << ",\"canceled_orders\":" << m.canceled_orders
        << ",\"trades_executed\":" << m.trades_executed
        << ",\"trades_settled\":" << m.trades_settled
        << ",\"blocked_settlements\":" << m.blocked_settlements
        << ",\"settlement_cycles\":" << m.settlement_cycles
        << ",\"loan_disbursements\":" << m.loan_disbursements
        << ",\"loan_repayments\":" << m.loan_repayments
        << ",\"risk_rejections\":" << m.risk_rejections
        << ",\"traded_notional_cents\":" << m.traded_notional_cents
        << ",\"peak_clearing_cash_cents\":" << m.peak_clearing_cash_cents
        << ",\"pending_trades\":" << e.pending_trade_count()
        << ",\"open_orders\":" << e.active_order_count()
        << ",\"clearing_cash_cents\":" << e.clearing_cash_cents()
        << ",\"clearing_shares\":" << e.clearing_shares()
        << "},\"invariants\":{\"passed\":true"
        << ",\"cash_initial_cents\":" << e.initial_cash_cents()
        << ",\"shares_initial\":" << e.initial_shares()
        << "},\"risk\":{\"max_order_notional_cents\":" << e.max_order_notional_cents()
        << ",\"reference_price_cents\":" << e.reference_price_cents()
        << ",\"deviation_bps\":" << e.deviation_bps() << "},\"accounts\":[";
    bool first = true;
    for (const auto& a : e.accounts()) {
        if (!first) out << ',';
        first = false;
        out << "{\"name\":" << json_escape(a.name)
            << ",\"cash_cents\":" << a.cash_cents
            << ",\"reserved_cash_cents\":" << a.reserved_cash_cents
            << ",\"available_cash_cents\":" << a.available_cash_cents()
            << ",\"shares\":" << a.shares
            << ",\"reserved_shares\":" << a.reserved_shares
            << ",\"available_shares\":" << a.available_shares() << '}';
    }
    out << "],\"open_orders\":[";
    first = true;
    for (const auto& order : e.open_orders()) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << json_escape(order.id)
            << ",\"owner\":" << json_escape(order.owner)
            << ",\"side\":" << json_escape(side_to_string(order.side))
            << ",\"remaining\":" << order.remaining
            << ",\"price_cents\":" << order.price_cents
            << ",\"sequence\":" << order.sequence << '}';
    }
    out << "],\"trades\":[";
    first = true;
    for (const auto& trade : e.trades()) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << trade.id
            << ",\"buyer\":" << json_escape(trade.buyer)
            << ",\"seller\":" << json_escape(trade.seller)
            << ",\"buy_order_id\":" << json_escape(trade.buy_order_id)
            << ",\"sell_order_id\":" << json_escape(trade.sell_order_id)
            << ",\"quantity\":" << trade.quantity
            << ",\"price_cents\":" << trade.price_cents
            << ",\"cash_cents\":" << trade.cash_cents
            << ",\"settled\":" << bool_json(trade.settled) << '}';
    }
    out << "],\"loans\":[";
    first = true;
    for (const auto& [borrower, cents] : e.outstanding_loans()) {
        if (!first) out << ',';
        first = false;
        out << "{\"borrower\":" << json_escape(borrower)
            << ",\"outstanding_cents\":" << cents << '}';
    }
    out << "],\"timeline\":[";
    first = true;
    for (const auto& ev : s.timeline) {
        if (!first) out << ',';
        first = false;
        out << "{\"index\":" << ev.index
            << ",\"line\":" << ev.line
            << ",\"command\":" << json_escape(ev.command)
            << ",\"status\":" << json_escape(ev.status)
            << ",\"message\":" << json_escape(ev.message)
            << ",\"clearing_cash_cents\":" << ev.clearing_cash_cents
            << ",\"pending_trades\":" << ev.pending_trades
            << ",\"trades_executed\":" << ev.trades_executed
            << ",\"trades_settled\":" << ev.trades_settled
            << ",\"rejected_orders\":" << ev.rejected_orders
            << ",\"exchange_online\":" << bool_json(ev.exchange_online)
            << ",\"payments_online\":" << bool_json(ev.payments_online)
            << ",\"accounts\":[";
        bool first_account = true;
        for (const auto& account : ev.accounts) {
            if (!first_account) out << ',';
            first_account = false;
            out << "{\"name\":" << json_escape(account.name)
                << ",\"cash_cents\":" << account.cash_cents
                << ",\"available_cash_cents\":" << account.available_cash_cents()
                << ",\"reserved_cash_cents\":" << account.reserved_cash_cents
                << ",\"shares\":" << account.shares
                << ",\"available_shares\":" << account.available_shares() << '}';
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

std::string catalog_json() {
    std::ostringstream out;
    out << "{\"default\":\"baseline\",\"scenarios\":[";
    bool first = true;
    for (const auto& item : kCatalog) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << json_escape(item.id)
            << ",\"title\":" << json_escape(item.title)
            << ",\"description\":" << json_escape(item.description) << '}';
    }
    return out.str() + "]}";
}

std::string comparison_json(const std::string& directory) {
    std::ostringstream out;
    out << "{\"comparison\":[";
    bool first = true;
    for (const auto& scenario : kCatalog) {
        const auto result = run_scenario(directory, scenario.id);
        const auto& e = result.exchange;
        const auto& m = e.metrics();
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << json_escape(scenario.id)
            << ",\"title\":" << json_escape(scenario.title)
            << ",\"executed\":" << m.trades_executed
            << ",\"settled\":" << m.trades_settled
            << ",\"rejected\":" << m.rejected_orders
            << ",\"pending\":" << e.pending_trade_count()
            << ",\"escrow_cents\":" << e.clearing_cash_cents()
            << ",\"risk_rejections\":" << m.risk_rejections
            << ",\"cash_initial_cents\":" << e.initial_cash_cents()
            << '}';
    }
    return out.str() + "]}";
}

}  // namespace event_horizon
