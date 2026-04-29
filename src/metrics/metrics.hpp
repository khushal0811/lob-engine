#pragma once
#include "core/order.hpp"
#include "metrics/histogram.hpp"
#include <atomic>
#include <cstdint>
#include <iostream>

namespace lob {

class Metrics {
public:
    void record_event_latency(uint64_t latency_ns) { latency_.record(latency_ns); }
    void record_trade() { trade_count_.fetch_add(1, std::memory_order_relaxed); }
    void record_rejection() { rejection_count_.fetch_add(1, std::memory_order_relaxed); }
    void record_cancel() { cancel_count_.fetch_add(1, std::memory_order_relaxed); }

    void update_spread(Price spread) { last_spread_ = spread; }
    void update_imbalance(double imbalance) { last_imbalance_ = imbalance; }
    void update_depth(std::size_t bid_levels, std::size_t ask_levels) {
        last_bid_depth_ = bid_levels;
        last_ask_depth_ = ask_levels;
    }

    void print_summary() const {
        std::cout << "=== Engine Metrics ===\n"
                  << "Events:     " << latency_.count() << "\n"
                  << "Trades:     " << trade_count_.load() << "\n"
                  << "Rejections: " << rejection_count_.load() << "\n"
                  << "Cancels:    " << cancel_count_.load() << "\n"
                  << "Latency p50:  " << latency_.p50() << " ns\n"
                  << "Latency p95:  " << latency_.p95() << " ns\n"
                  << "Latency p99:  " << latency_.p99() << " ns\n"
                  << "Latency max:  " << latency_.max_val() << " ns\n"
                  << "Spread:     " << last_spread_ << "\n"
                  << "Imbalance:  " << last_imbalance_ << "\n"
                  << "Depth:      " << last_bid_depth_ << " bid / " << last_ask_depth_ << " ask\n";
    }

    [[nodiscard]] const LatencyHistogram& latency() const { return latency_; }

    [[nodiscard]] uint64_t trade_count() const { return trade_count_.load(); }
    [[nodiscard]] uint64_t rejection_count() const { return rejection_count_.load(); }
    [[nodiscard]] uint64_t cancel_count() const { return cancel_count_.load(); }

private:
    LatencyHistogram latency_;
    std::atomic<uint64_t> trade_count_{0};
    std::atomic<uint64_t> rejection_count_{0};
    std::atomic<uint64_t> cancel_count_{0};
    Price last_spread_{0};
    double last_imbalance_{0.0};
    std::size_t last_bid_depth_{0};
    std::size_t last_ask_depth_{0};
};

} // namespace lob
