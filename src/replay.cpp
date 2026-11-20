#include "event_horizon/replay.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace event_horizon {
namespace {

struct Agent {
    std::string id;
    std::string owner;
    Side side;
    std::int64_t quantity;
    std::int64_t limit_cents;
    bool submitted = false;
};

struct Outcome {
    std::string status;
    std::string message;
};

std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r");
    if (start == std::string::npos) return "";
    const auto end = value.find_last_not_of(" \t\r");
    return value.substr(start, end - start + 1);
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (true) {
        const auto comma = line.find(',', begin);
        fields.push_back(trim(line.substr(begin, comma - begin)));
        if (comma == std::string::npos) break;
        begin = comma + 1;
    }
    return fields;
}

std::int64_t parse_int(const std::string& value, std::string_view name) {
    std::int64_t result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        throw std::runtime_error("invalid integer for " + std::string(name) + ": '" + value + "'");
    }
    return result;
}

void require_fields(const std::vector<std::string>& fields, std::size_t expected) {
    if (fields.size() != expected) {
        throw std::runtime_error(fields.at(0) + " expects " +
                                 std::to_string(expected - 1) + " argument(s)");
    }
}

Side parse_side(const std::string& value) {
    if (value == "BUY") return Side::buy;
    if (value == "SELL") return Side::sell;
    throw std::runtime_error("side must be BUY or SELL");
}

Outcome from_result(const Result& result) {
    return {result.ok ? "OK" : "REJECTED", result.message};
}

Outcome execute_event(const std::vector<std::string>& fields, Exchange& exchange,
                      std::vector<Agent>& agents, std::size_t& ticks) {
    const std::string& command = fields.at(0);
    if (command == "ACCOUNT") {
        require_fields(fields, 4);
        const Result result = exchange.add_account(fields[1], parse_int(fields[2], "cash_cents"),
                                                    parse_int(fields[3], "shares"));
        if (!result.ok) throw std::runtime_error(result.message);
        return from_result(result);
    }
    if (command == "PLACE") {
        require_fields(fields, 6);
        return from_result(exchange.place_order(fields[1], fields[2], parse_side(fields[3]),
            parse_int(fields[4], "quantity"), parse_int(fields[5], "price_cents")));
    }
    if (command == "CANCEL") {
        require_fields(fields, 3);
        return from_result(exchange.cancel_order(fields[2], fields[1]));
    }
    if (command == "SETTLE") {
        if (fields.size() != 1 && fields.size() != 2) {
            throw std::runtime_error("SETTLE expects zero or one argument");
        }
        SettlementResult result{};
        if (fields.size() == 1) result = exchange.settle_all();
        else {
            const auto count = parse_int(fields[1], "settlement_capacity");
            if (count <= 0 || count > 10000) {
                throw std::runtime_error("settlement_capacity must be between 1 and 10000");
            }
            result = exchange.settle_batch(static_cast<std::size_t>(count));
        }
        return result.blocked ? Outcome{"BLOCKED", "payments offline; escrow unchanged"}
                              : Outcome{"OK", "settled " + std::to_string(result.settled) + " trade(s)"};
    }
    if (command == "EXCHANGE_DOWN" || command == "EXCHANGE_UP") {
        require_fields(fields, 1);
        exchange.set_exchange_online(command == "EXCHANGE_UP");
        return {"OK", exchange.exchange_online() ? "exchange online" : "exchange offline"};
    }
    if (command == "PAYMENTS_DOWN" || command == "PAYMENTS_UP") {
        require_fields(fields, 1);
        exchange.set_payments_online(command == "PAYMENTS_UP");
        return {"OK", exchange.payments_online() ? "payments online" : "payments offline"};
    }
    if (command == "LEND" || command == "REPAY") {
        require_fields(fields, 3);
        const auto cents = parse_int(fields[2], "cash_cents");
        return from_result(command == "LEND" ? exchange.lend(fields[1], cents)
                                              : exchange.repay(fields[1], cents));
    }
    if (command == "MAX_NOTIONAL") {
        require_fields(fields, 2);
        const auto result = exchange.set_max_order_notional(parse_int(fields[1], "max_cents"));
        if (!result.ok) throw std::runtime_error(result.message);
        return from_result(result);
    }
    if (command == "PRICE_BAND") {
        require_fields(fields, 3);
        const auto result = exchange.set_price_band(parse_int(fields[1], "reference_cents"),
                                                    parse_int(fields[2], "deviation_bps"));
        if (!result.ok) throw std::runtime_error(result.message);
        return from_result(result);
    }
    if (command == "AGENT") {
        require_fields(fields, 6);
        const std::string& id = fields[1];
        if (id.empty() || id.size() > 16 || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_';
        })) throw std::runtime_error("agent ID must be 1..16 letters, digits or underscores");
        if (agents.size() >= 16) throw std::runtime_error("maximum 16 agents");
        if (std::any_of(agents.begin(), agents.end(), [&](const Agent& agent) { return agent.id == id; })) {
            throw std::runtime_error("duplicate agent ID");
        }
        const auto side = parse_side(fields[3]);
        const auto quantity = parse_int(fields[4], "quantity");
        const auto price = parse_int(fields[5], "price_cents");
        if (quantity <= 0 || quantity > 1000000 || price <= 0 || price > 100000000 ||
            price * quantity > 100000000000000LL) {
            throw std::runtime_error("agent quantity/price outside supported range");
        }
        const auto accounts = exchange.accounts();
        if (std::none_of(accounts.begin(), accounts.end(), [&](const AccountView& a) { return a.name == fields[2]; })) {
            throw std::runtime_error("agent owner account does not exist");
        }
        agents.push_back({id, fields[2], side, quantity, price, false});
        return {"OK", "strategy " + id + " registered"};
    }
    if (command == "TICK") {
        require_fields(fields, 1);
        ++ticks;
        std::size_t placed = 0;
        std::size_t skipped = 0;
        std::size_t rejected = 0;
        for (std::size_t i = 0; i < agents.size(); ++i) {
            Agent& agent = agents[i];
            if (agent.submitted) { ++skipped; continue; }
            const auto accounts = exchange.accounts();
            const auto it = std::find_if(accounts.begin(), accounts.end(),
                [&](const AccountView& a) { return a.name == agent.owner; });
            // Agents do not borrow or short sell. If funding is unavailable they wait.
            const bool funded = agent.side == Side::buy
                ? it->available_cash_cents() >= agent.limit_cents * agent.quantity
                : it->available_shares() >= agent.quantity;
            if (!funded) { ++skipped; continue; }
            const auto id = "auto" + std::to_string(ticks) + "_" + std::to_string(i);
            const auto result = exchange.place_order(id, agent.owner, agent.side,
                                                     agent.quantity, agent.limit_cents);
            if (result.ok) { ++placed; agent.submitted = true; }
            else ++rejected;
        }
        return {"OK", "agents: " + std::to_string(placed) + " placed, " +
            std::to_string(skipped) + " skipped (funds/already sent), " +
            std::to_string(rejected) + " rejected"};
    }
    throw std::runtime_error("unknown command: '" + command + "'");
}

}  // namespace

void replay_events(std::istream& input, Exchange& exchange, std::ostream& log,
                   std::vector<EventRecord>* trace) {
    std::string line;
    std::size_t line_number = 0;
    std::size_t events = 0;
    std::size_t ticks = 0;
    std::vector<Agent> agents;
    bool setup_complete = false;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.size() > 4096) throw std::runtime_error("line " + std::to_string(line_number) +
                                                       ": line exceeds 4096 characters");
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#') continue;
        if (++events > 500) throw std::runtime_error("replay exceeds 500 events");
        try {
            const auto fields = split_csv(trimmed);
            if (fields[0] == "ACCOUNT" && setup_complete) {
                throw std::runtime_error("accounts must be declared before simulation events");
            }
            if (fields[0] != "ACCOUNT") setup_complete = true;
            const auto outcome = execute_event(fields, exchange, agents, ticks);
            exchange.verify_invariants();
            log << "line " << line_number << ": " << outcome.status << " " << outcome.message << '\n';
            if (trace != nullptr) {
                trace->push_back({events, line_number, trimmed, outcome.status, outcome.message,
                    exchange.clearing_cash_cents(), exchange.pending_trade_count(),
                    exchange.metrics().trades_executed, exchange.metrics().trades_settled,
                    exchange.metrics().rejected_orders, exchange.exchange_online(),
                    exchange.payments_online(), exchange.accounts()});
            }
        } catch (const std::exception& error) {
            throw std::runtime_error("line " + std::to_string(line_number) + ": " + error.what());
        }
    }
    if (input.bad()) throw std::runtime_error("error while reading replay input");
    if (events == 0) throw std::runtime_error("scenario has no events");
}

Exchange replay_file(const std::string& path, std::ostream& log,
                     std::vector<EventRecord>* trace) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open replay file: " + path);
    Exchange exchange;
    replay_events(input, exchange, log, trace);
    return exchange;
}

void print_summary(const Exchange& exchange, std::ostream& output) {
    const Metrics& m = exchange.metrics();
    output << "\nMETRICS\n"
           << "  accepted_orders:    " << m.accepted_orders << '\n'
           << "  rejected_orders:    " << m.rejected_orders << '\n'
           << "  canceled_orders:    " << m.canceled_orders << '\n'
           << "  trades_executed:    " << m.trades_executed << '\n'
           << "  trades_settled:     " << m.trades_settled << '\n'
           << "  risk_rejections:    " << m.risk_rejections << '\n'
           << "  blocked_settlements:" << m.blocked_settlements << '\n'
           << "  pending_trades:     " << exchange.pending_trade_count() << '\n'
           << "  open_orders:        " << exchange.active_order_count() << '\n'
           << "  clearing_cash:      " << format_money(exchange.clearing_cash_cents()) << '\n'
           << "  clearing_shares:    " << exchange.clearing_shares() << '\n'
           << "  exchange_online:    " << (exchange.exchange_online() ? "yes" : "no") << '\n'
           << "  payments_online:    " << (exchange.payments_online() ? "yes" : "no") << '\n'
           << "\nACCOUNTS\n";
    for (const auto& a : exchange.accounts()) {
        output << "  " << std::left << std::setw(9) << a.name
               << " cash=" << std::setw(11) << format_money(a.cash_cents)
               << " free_cash=" << std::setw(11) << format_money(a.available_cash_cents())
               << " shares=" << std::setw(4) << a.shares
               << " free_shares=" << a.available_shares() << '\n';
    }
    output << "\nOUTSTANDING FACILITY LOANS\n";
    for (const auto& [borrower, cents] : exchange.outstanding_loans()) {
        if (cents > 0) output << "  " << borrower << " -> " << format_money(cents) << '\n';
    }
    output << "\nINVARIANTS: PASS (cash, shares, reservations and escrow conserved)\n";
}

}  // namespace event_horizon
