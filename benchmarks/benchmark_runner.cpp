#include "benchmark_runner.hpp"
#include "engine/matching_engine.hpp"
#include "feed/synthetic_gen.hpp"
#include "scenarios.hpp"
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <time.h>

namespace lob {

// Nanosecond timestamp via clock_gettime(CLOCK_MONOTONIC)
static inline uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

BenchmarkRunner::BenchmarkRunner(std::string scenario, uint64_t seed, uint32_t duration_sec)
    : scenario_(std::move(scenario)), seed_(seed), duration_sec_(duration_sec) {
    config_ = get_scenario_config(scenario_);
}

void BenchmarkRunner::run() {
    latency_.reset();
    events_processed_ = 0;
    run_scenario();
}

void BenchmarkRunner::run_scenario() {
    MatchingEngine engine(config_);
    // Use a very large max_events so the generator never runs out — duration is the limit
    SyntheticGenerator gen(config_, seed_, 100'000'000);

    const uint64_t duration_ns = static_cast<uint64_t>(duration_sec_) * 1'000'000'000ULL;
    const uint64_t start = now_ns();

    OrderEvent event;
    while (gen.next(event)) {
        uint64_t t0 = now_ns();
        engine.submit(event);
        uint64_t t1 = now_ns();
        latency_.record(t1 - t0);
        ++events_processed_;
        if (t1 - start > duration_ns)
            break;
    }
}

void BenchmarkRunner::report() const {
    double elapsed_sec = static_cast<double>(duration_sec_);
    double throughput = static_cast<double>(events_processed_) / elapsed_sec;

    std::cout << "=== Benchmark: " << scenario_ << " ===\n"
              << std::fixed << std::setprecision(0) << "  Duration:    " << duration_sec_ << " s\n"
              << "  Events:      " << events_processed_ << "\n"
              << "  Throughput:  " << throughput << " ev/s\n"
              << "  p50:         " << latency_.p50() << " ns\n"
              << "  p95:         " << latency_.p95() << " ns\n"
              << "  p99:         " << latency_.p99() << " ns\n"
              << "  max:         " << latency_.max_val() << " ns\n"
              << "\n";
}

} // namespace lob
