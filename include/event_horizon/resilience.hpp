#pragma once

#include "event_horizon/report.hpp"

#include <string>

namespace event_horizon {

// Recovery happens at one of three event boundaries, or not at all.
// Funding is pre-existing facility cash: 0 = none, 1 = first firm, 2 = both.
Simulation run_resilience_case(int recovery_stage, int funding_level);
std::string resilience_matrix_json();

}  // namespace event_horizon
