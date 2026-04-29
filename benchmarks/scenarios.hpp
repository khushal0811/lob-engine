#pragma once
#include "core/config.hpp"
#include <string>
#include <unordered_map>

namespace lob {

// Returns an EngineConfig pre-configured for the named benchmark scenario.
inline EngineConfig get_scenario_config(const std::string& name) {
    EngineConfig cfg;

    if (name == "small") {
        cfg.arrival_rate = 100;
        cfg.cancel_probability = 0.1;
        cfg.price_std_dev = 5;
    } else if (name == "medium") {
        cfg.arrival_rate = 1000;
        cfg.cancel_probability = 0.3;
        cfg.price_std_dev = 20;
    } else if (name == "large") {
        cfg.arrival_rate = 10000;
        cfg.cancel_probability = 0.3;
        cfg.price_std_dev = 20;
    } else if (name == "high_cancel") {
        cfg.arrival_rate = 5000;
        cfg.cancel_probability = 0.7;
        cfg.price_std_dev = 20;
    } else if (name == "market_heavy") {
        cfg.arrival_rate = 2000;
        cfg.cancel_probability = 0.1;
        cfg.price_std_dev = 20;
    } else if (name == "iceberg_stop") {
        cfg.arrival_rate = 1000;
        cfg.cancel_probability = 0.2;
        cfg.price_std_dev = 20;
    } else {
        cfg.arrival_rate = 1000;
        cfg.cancel_probability = 0.3;
        cfg.price_std_dev = 20;
    }

    return cfg;
}

inline const char* const kScenarioNames[] = {"small",       "medium",       "large",
                                             "high_cancel", "market_heavy", "iceberg_stop"};
inline constexpr std::size_t kScenarioCount = 6;

} // namespace lob
