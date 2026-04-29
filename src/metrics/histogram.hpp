#pragma once
#include "core/order.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>

namespace lob {

class LatencyHistogram {
public:
    void record(uint64_t latency_ns) {
        ++count_;
        if (latency_ns > max_)
            max_ = latency_ns;

        std::size_t idx = bucket_index(latency_ns);
        if (idx < kBuckets)
            ++buckets_[idx];
    }

    [[nodiscard]] uint64_t percentile(double p) const {
        if (count_ == 0)
            return 0;
        uint64_t target = static_cast<uint64_t>(count_ * p / 100.0);
        uint64_t cumulative = 0;
        for (std::size_t i = 0; i < kBuckets; ++i) {
            cumulative += buckets_[i];
            if (cumulative >= target) {
                return bucket_to_ns(i);
            }
        }
        return max_;
    }

    [[nodiscard]] uint64_t p50() const { return percentile(50.0); }
    [[nodiscard]] uint64_t p95() const { return percentile(95.0); }
    [[nodiscard]] uint64_t p99() const { return percentile(99.0); }
    [[nodiscard]] uint64_t max_val() const noexcept { return max_; }
    [[nodiscard]] uint64_t count() const noexcept { return count_; }

    void reset() {
        buckets_.fill(0);
        count_ = 0;
        max_ = 0;
    }

private:
    // Buckets: 0-999 ns (1 ns res), 1µs-999µs (1 µs res), 1ms-9ms (100 µs res)
    static constexpr std::size_t kBuckets = 1000 + 1000 + 90;
    std::array<uint64_t, kBuckets> buckets_{};
    uint64_t count_{0};
    uint64_t max_{0};

    static std::size_t bucket_index(uint64_t ns) {
        if (ns < 1000)
            return ns; // 0-999ns → bucket 0-999
        if (ns < 1'000'000)
            return 1000 + (ns / 1000); // 1µs-999µs → 1000-1999
        if (ns < 10'000'000)
            return 2000 + (ns / 100'000); // 1ms-9ms → 2000-2089
        return kBuckets;                  // overflow — counted by max but not bucketed
    }

    static uint64_t bucket_to_ns(std::size_t idx) {
        if (idx < 1000)
            return idx;
        if (idx < 2000)
            return (idx - 1000) * 1000;
        return (idx - 2000) * 100'000;
    }
};

} // namespace lob
