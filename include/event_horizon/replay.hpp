#pragma once

#include "event_horizon/exchange.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace event_horizon {

struct EventRecord {
    std::size_t index;
    std::size_t line;
    std::string command;
    std::string status;
    std::string message;
    std::int64_t clearing_cash_cents;
    std::size_t pending_trades;
    std::int64_t trades_executed;
    std::int64_t trades_settled;
};

// Strict, unquoted CSV: all dollar values use integer cents.
// Malformed input throws with a line number. Insufficient resources produce
// explicit REJECTED events, not fatal replay errors.
void replay_events(std::istream& input, Exchange& exchange, std::ostream& log,
                   std::vector<EventRecord>* trace = nullptr);
Exchange replay_file(const std::string& path, std::ostream& log,
                     std::vector<EventRecord>* trace = nullptr);
void print_summary(const Exchange& exchange, std::ostream& output);

}  // namespace event_horizon
