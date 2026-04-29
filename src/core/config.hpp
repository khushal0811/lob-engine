#pragma once
#include "core/order.hpp"
#include <cstdint>
#include <string>

namespace lob {

struct EngineConfig {
    // Risk limits
    Price    max_price        {10'000'000};  // maximum allowed price in ticks
    Quantity max_order_size   {1'000'000};   // maximum order quantity
    Price    price_band_bps   {500};         // max deviation from mid in basis points

    // Snapshot
    uint64_t    snapshot_interval {10'000};      // snapshot every N events
    std::string snapshot_path     {"snapshots/"};

    // Logging
    std::string log_path       {"logs/"};
    uint32_t    log_queue_size {65536};

    // Synthetic generator
    Price    mid_price          {10'000};    // starting mid in ticks
    double   arrival_rate       {1000.0};    // orders per second
    double   price_std_dev      {20.0};      // std dev of price around mid (ticks)
    double   cancel_probability {0.3};       // fraction of events that are cancels
    uint32_t burst_size         {0};         // 0 = no burst mode
    bool     market_maker_mode  {false};
};

} // namespace lob
