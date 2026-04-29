#include "feed/synthetic_gen.hpp"
#include <algorithm>
#include <cmath>

namespace lob {

SyntheticGenerator::SyntheticGenerator(EngineConfig config, uint64_t seed,
                                         uint64_t max_events)
    : config_(std::move(config)),
      mid_price_(config_.mid_price),
      max_events_(max_events),
      rng_(seed),
      price_dist_(0.0, config_.price_std_dev) {}

bool SyntheticGenerator::next(OrderEvent& out) {
    if (event_count_ >= max_events_) return false;
    ++event_count_;
    current_ts_ += 1'000'000; // 1ms between events (1000 events/sec)

    bool do_cancel = !live_orders_.empty() &&
                     uniform_(rng_) < config_.cancel_probability;

    if (do_cancel) {
        out = generate_cancel();
    } else {
        out = generate_new_order();
    }
    return true;
}

Price SyntheticGenerator::sample_price() {
    double offset = price_dist_(rng_);
    Price p = mid_price_ + static_cast<Price>(std::round(offset));
    return std::max(p, Price{1}); // ensure positive
}

OrderEvent SyntheticGenerator::generate_new_order() {
    Order o;
    o.id = next_order_id_++;
    o.side = (uniform_(rng_) < 0.5) ? Side::Buy : Side::Sell;
    o.type = OrderType::Limit;
    o.quantity = qty_dist_(rng_);
    o.orig_qty = o.quantity;
    o.timestamp = current_ts_;
    o.status = OrderStatus::New;

    if (config_.market_maker_mode) {
        // Place within 3 ticks of mid
        std::uniform_int_distribution<int64_t> spread_dist(1, 3);
        if (o.is_buy()) {
            o.price = mid_price_ - spread_dist(rng_);
        } else {
            o.price = mid_price_ + spread_dist(rng_);
        }
    } else {
        o.price = sample_price();
    }

    // Track for future cancel
    live_orders_.push_back(o.id);

    return NewOrderEvent{o};
}

OrderEvent SyntheticGenerator::generate_cancel() {
    std::uniform_int_distribution<size_t> idx_dist(0, live_orders_.size() - 1);
    size_t idx = idx_dist(rng_);
    OrderId cancel_id = live_orders_[idx];

    // Remove from live list (swap-and-pop)
    live_orders_[idx] = live_orders_.back();
    live_orders_.pop_back();

    CancelOrderEvent e;
    e.order_id = cancel_id;
    e.timestamp = current_ts_;
    return e;
}

} // namespace lob
