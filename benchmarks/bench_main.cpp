#include "benchmark_runner.hpp"
#include "scenarios.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string scenario = "medium";
    uint64_t seed = 42;
    uint32_t duration_sec = 10;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenario = argv[++i];
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = std::stoull(argv[++i]);
        } else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration_sec = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--all") == 0) {
            for (std::size_t s = 0; s < lob::kScenarioCount; ++s) {
                lob::BenchmarkRunner runner(lob::kScenarioNames[s], seed, duration_sec);
                runner.run();
                runner.report();
            }
            return 0;
        } else {
            std::cerr << "Usage: " << argv[0]
                      << " [--scenario <name>] [--seed <n>] [--duration <sec>] [--all]\n"
                      << "Scenarios: small, medium, large, high_cancel, market_heavy, iceberg_stop\n";
            return 1;
        }
    }

    lob::BenchmarkRunner runner(scenario, seed, duration_sec);
    runner.run();
    runner.report();

    return 0;
}
