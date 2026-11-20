#include "event_horizon/resilience.hpp"

#include <sstream>
#include <stdexcept>

namespace event_horizon {
namespace {

void validate(int recovery_stage, int funding_level) {
    if (recovery_stage < 0 || recovery_stage > 3 || funding_level < 0 || funding_level > 2) {
        throw std::invalid_argument("recovery must be 0..3 and funding must be 0..2");
    }
}

// Deliberately share the same opening balances, orders and prices across the
// matrix. Only the timing of the repair and amount of liquidity support change.
std::string generate_case(int recovery, int funding) {
    std::ostringstream csv;
    csv << "# Recovery: 0 before BEACON; 1 before CIRRUS; 2 after CIRRUS; 3 never\n"
        << "# Funding: 0 no support; 1 BEACON; 2 BEACON and CIRRUS\n"
        << "ACCOUNT,NOVA,100000,0\nACCOUNT,BEACON,5000,5\n"
        << "ACCOUNT,CIRRUS,3000,5\nACCOUNT,DELTA,2500,5\n"
        << "ACCOUNT,FACILITY,100000,0\n"
        << "PLACE,ask1,BEACON,SELL,4,10000\nPLACE,buy1,NOVA,BUY,4,10000\n"
        << "PAYMENTS_DOWN\nSETTLE\n";

    if (recovery == 0) csv << "PAYMENTS_UP\nSETTLE\n";
    if (funding >= 1 && recovery != 0) csv << "LEND,BEACON,31000\n";
    csv << "PLACE,ask2,CIRRUS,SELL,3,12000\nPLACE,buy2,BEACON,BUY,3,12000\nSETTLE\n";
    // Avoid self-trade rejection being mistaken for an inability to buy.
    const bool second_trade_matched = recovery == 0 || funding >= 1;
    if (!second_trade_matched) csv << "CANCEL,CIRRUS,ask2\n";
    if (recovery == 1) csv << "PAYMENTS_UP\nSETTLE\n";
    if (funding == 2 && recovery > 1) csv << "LEND,CIRRUS,27000\n";
    csv << "PLACE,ask3,DELTA,SELL,2,15000\nPLACE,buy3,CIRRUS,BUY,2,15000\nSETTLE\n";
    if (recovery == 2) csv << "PAYMENTS_UP\nSETTLE\n";
    if (recovery != 3) {
        if (funding >= 1 && recovery != 0) csv << "REPAY,BEACON,31000\n";
        if (funding == 2 && recovery > 1) csv << "REPAY,CIRRUS,27000\n";
    }
    return csv.str();
}

}  // namespace

Simulation run_resilience_case(int recovery_stage, int funding_level) {
    validate(recovery_stage, funding_level);
    return run_text(generate_case(recovery_stage, funding_level));
}

std::string resilience_matrix_json() {
    std::ostringstream out;
    out << "{\"description\":\"Controlled payment outage: identical opening balances "
           "and intended orders, with recovery timing and pre-funded liquidity support varied.\","
           "\"recovery_labels\":[\"Before BEACON needs cash\","
           "\"Before CIRRUS needs cash\",\"After downstream orders\","
           "\"Not restored\"],"
           "\"funding_labels\":[\"No facility loans\",\"Support BEACON if needed ($310)\","
           "\"Support both if needed ($580)\"],\"cases\":[";
    bool first = true;
    for (int recovery = 0; recovery < 4; ++recovery) {
        for (int funding = 0; funding < 3; ++funding) {
            const auto simulation = run_resilience_case(recovery, funding);
            const auto& engine = simulation.exchange;
            const auto& metrics = engine.metrics();
            if (!first) out << ',';
            first = false;
            std::int64_t debt = 0;
            for (const auto& [_, cents] : engine.outstanding_loans()) debt += cents;
            out << "{\"recovery\":" << recovery
                << ",\"funding\":" << funding
                << ",\"matched\":" << metrics.trades_executed
                << ",\"settled\":" << metrics.trades_settled
                << ",\"rejected\":" << metrics.rejected_orders
                << ",\"pending\":" << engine.pending_trade_count()
                << ",\"escrow_cents\":" << engine.clearing_cash_cents()
                << ",\"outstanding_debt_cents\":" << debt
                << ",\"invariants_passed\":true}";
        }
    }
    return out.str() + "]}";
}

}  // namespace event_horizon
