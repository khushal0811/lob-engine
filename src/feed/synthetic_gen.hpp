#pragma once
#include "core/config.hpp"
#include "core/event.hpp"
#include <random>
#include <vector>

namespace lob {

class SyntheticGenerator {
public:
    explicit SyntheticGenerator(EngineConfig config, uint64_t seed = 42,
                                 uint64_t max_events = 100000);

    bool next(OrderEvent& out);

    [[nodiscard]] uint64_t event_count() const noexcept { return event_count_; }

private:
    EngineConfig config_;
    Price mid_price_;
    uint64_t next_order_id_{1};
    uint64_t event_count_{0};
    uint64_t max_events_;
    Timestamp current_ts_{1'700'000'000'000'000'000ULL};

    // Track live order IDs for cancellation
    std::vector<OrderId> live_orders_;

    // RNG
    std::mt19937_64 rng_;
    std::normal_distribution<double> price_dist_;
    std::uniform_real_distribution<double> uniform_{0.0, 1.0};
    std::uniform_int_distribution<uint64_t> qty_dist_{1, 500};

    OrderEvent generate_new_order();
    OrderEvent generate_cancel();
    Price sample_price();
};

} // namespace lob
