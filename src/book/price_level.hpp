#pragma once
#include "core/order.hpp"
#include <cstdint>
#include <deque>
#include <unordered_set>
#include <vector>

namespace lob {

class PriceLevel {
public:
    explicit PriceLevel(Price price) : price_(price) {}

    void push_back(OrderId id, Quantity visible_qty) {
        queue_.push_back(id);
        total_volume_ += visible_qty;
    }

    void pop_front(Quantity filled_qty) {
        drain_cancelled();
        queue_.pop_front();
        total_volume_ = (total_volume_ >= filled_qty) ? total_volume_ - filled_qty : 0;
    }

    void reduce_front_volume(Quantity qty) {
        total_volume_ = (total_volume_ >= qty) ? total_volume_ - qty : 0;
    }

    // O(1) cancel via lazy deletion — deque entry is drained on next front access.
    void remove(OrderId id, Quantity visible_qty);

    [[nodiscard]] OrderId front() const noexcept {
        drain_cancelled();
        return queue_.front();
    }

    [[nodiscard]] bool empty() const noexcept {
        drain_cancelled();
        return queue_.empty();
    }

    [[nodiscard]] Price    price()        const noexcept { return price_; }
    [[nodiscard]] Quantity total_volume() const noexcept { return total_volume_; }

    [[nodiscard]] std::size_t size() const noexcept {
        return queue_.size() - cancelled_.size();
    }

    [[nodiscard]] std::vector<OrderId> order_ids() const {
        std::vector<OrderId> ids;
        ids.reserve(queue_.size() - cancelled_.size());
        for (auto id : queue_) {
            if (cancelled_.count(id) == 0) ids.push_back(id);
        }
        return ids;
    }

private:
    Price    price_{0};
    Quantity total_volume_{0};

    // mutable: drain_cancelled() is invoked from logically-const accessors
    // (front, empty) to maintain FIFO ordering without exposing mutability.
    mutable std::deque<OrderId>         queue_;
    mutable std::unordered_set<OrderId> cancelled_;

    // Drains all leading cancelled entries from the deque front.
    void drain_cancelled() const noexcept {
        while (!queue_.empty() && cancelled_.count(queue_.front())) {
            cancelled_.erase(queue_.front());
            queue_.pop_front();
        }
    }
};

} // namespace lob
