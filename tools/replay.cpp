#include "engine/matching_engine.hpp"
#include "feed/csv_replay.hpp"
#include "snapshot/snapshot.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace lob;

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " --events <csv> [--snapshot <bin>] [--expected <csv>]\n";
}

static std::string trade_to_csv(const Trade& t) {
    return std::to_string(t.aggressor_id) + "," + std::to_string(t.passive_id) + "," +
           std::to_string(t.price) + "," + std::to_string(t.quantity);
}

int main(int argc, char** argv) {
    std::string events_path;
    std::string snapshot_path;
    std::string expected_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--events" && i + 1 < argc)
            events_path = argv[++i];
        else if (arg == "--snapshot" && i + 1 < argc)
            snapshot_path = argv[++i];
        else if (arg == "--expected" && i + 1 < argc)
            expected_path = argv[++i];
        else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (events_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    MatchingEngine engine;

    // Load snapshot if provided
    if (!snapshot_path.empty()) {
        SnapshotManager mgr(snapshot_path);
        std::vector<Order> stops;
        uint64_t seq = 0;
        if (!mgr.load(engine.book(), stops, seq)) {
            std::cerr << "Error: failed to load snapshot: " << snapshot_path << "\n";
            return 1;
        }
        engine.pending_stops() = stops;
        std::cerr << "Loaded snapshot at sequence " << seq << "\n";
    }

    // Replay events
    CsvReplayReader reader(events_path);
    std::vector<std::string> trade_lines;
    OrderEvent event;
    uint64_t event_count = 0;

    while (reader.next(event)) {
        ++event_count;
        auto result = engine.submit(event);
        for (const auto& trade : result.trades) {
            trade_lines.push_back(trade_to_csv(trade));
        }
    }

    std::cerr << "Replayed " << event_count << " events, produced " << trade_lines.size()
              << " trades\n";

    // Output trades
    for (const auto& line : trade_lines) {
        std::cout << line << "\n";
    }

    // Compare with expected if provided
    if (!expected_path.empty()) {
        std::ifstream exp_file(expected_path);
        if (!exp_file) {
            std::cerr << "Error: cannot open expected file: " << expected_path << "\n";
            return 1;
        }

        // Skip header
        std::string header;
        std::getline(exp_file, header);

        std::string line;
        size_t idx = 0;
        while (std::getline(exp_file, line)) {
            if (line.empty())
                continue;
            if (idx >= trade_lines.size()) {
                std::cerr << "MISMATCH at trade " << idx << ": expected more trades, got "
                          << trade_lines.size() << "\n";
                return 1;
            }
            if (trade_lines[idx] != line) {
                std::cerr << "MISMATCH at trade " << idx << ":\n"
                          << "  expected: " << line << "\n"
                          << "  got:      " << trade_lines[idx] << "\n";
                return 1;
            }
            ++idx;
        }

        if (idx != trade_lines.size()) {
            std::cerr << "MISMATCH: expected " << idx << " trades, got " << trade_lines.size()
                      << "\n";
            return 1;
        }

        std::cerr << "OK: output matches expected (" << idx << " trades)\n";
    }

    return 0;
}
