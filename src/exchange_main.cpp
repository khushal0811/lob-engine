#include "gateway/gateway.hpp"
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>

// ---------------------------------------------------------------------------
// Graceful shutdown via SIGINT / SIGTERM
// ---------------------------------------------------------------------------

namespace {

std::atomic<bool> g_shutdown{false};

void signal_handler(int /*sig*/) noexcept {
    g_shutdown.store(true, std::memory_order_release);
}

void install_signal_handler() {
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [OPTIONS]\n"
        << "\nOptions:\n"
        << "  --pull <endpoint>          ZMQ PULL endpoint (default: tcp://*:5555)\n"
        << "  --pub  <endpoint>          ZMQ PUB  endpoint (default: tcp://*:5556)\n"
        << "  --snapshot-interval <ms>   Snapshot interval ms (default: 100)\n"
        << "  --log-path <path>          Replay log path (default: logs/replay.ndjson)\n"
        << "\nInstruments: STOCK_1 … STOCK_25\n";
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    lob::gateway::GatewayConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--pull") && i + 1 < argc) {
            cfg.pull_endpoint = argv[++i];
        } else if ((arg == "--pub") && i + 1 < argc) {
            cfg.pub_endpoint = argv[++i];
        } else if ((arg == "--snapshot-interval") && i + 1 < argc) {
            cfg.snapshot_interval_ms = static_cast<uint64_t>(std::stoul(argv[++i]));
        } else if ((arg == "--log-path") && i + 1 < argc) {
            cfg.replay_log_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Ensure log directory exists
    try {
        std::filesystem::path log_dir =
            std::filesystem::path(cfg.replay_log_path).parent_path();
        if (!log_dir.empty())
            std::filesystem::create_directories(log_dir);
    } catch (const std::exception& ex) {
        std::cerr << "[lob-exchange] Warning: could not create log dir: " << ex.what() << "\n";
    }

    std::cout << "[lob-exchange] Initializing exchange with "
              << lob::gateway::kInstrumentCount << " instruments (STOCK_1…STOCK_"
              << lob::gateway::kInstrumentCount << ")\n";

    install_signal_handler();

    lob::gateway::Gateway gw(cfg);
    gw.start();

    // Block until SIGINT / SIGTERM
    while (!g_shutdown.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "\n[lob-exchange] Shutdown signal received.\n";
    gw.stop();

    return 0;
}
