#pragma once
#include "core/config.hpp"
#include "metrics/histogram.hpp"
#include <cstdint>
#include <string>

namespace lob {

class BenchmarkRunner {
public:
    BenchmarkRunner(std::string scenario, uint64_t seed, uint32_t duration_sec);
    void run();
    void report() const;

private:
    void run_scenario();

    uint64_t events_processed_{0};
    LatencyHistogram latency_;
    std::string scenario_;
    uint64_t seed_;
    uint32_t duration_sec_;
    EngineConfig config_;
};

} // namespace lob
