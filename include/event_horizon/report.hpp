#pragma once

#include "event_horizon/replay.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace event_horizon {

struct Scenario {
    std::string id;
    std::string title;
    std::string description;
};

struct Simulation {
    Exchange exchange;
    std::vector<EventRecord> timeline;
    std::string source;
};

const std::vector<Scenario>& catalog();
Simulation run_scenario(const std::string& data_directory, const std::string& id);
Simulation run_text(const std::string& csv);
std::string to_json(const Simulation& simulation, std::string_view scenario_id);
std::string catalog_json();
std::string comparison_json(const std::string& data_directory);
std::string json_escape(std::string_view text);

}  // namespace event_horizon
