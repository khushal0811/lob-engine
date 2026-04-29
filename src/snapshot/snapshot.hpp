#pragma once
#include "book/order_book.hpp"
#include "core/order.hpp"
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace lob {

class SnapshotManager {
public:
    explicit SnapshotManager(std::string path);

    bool save(const OrderBook& book, const std::vector<Order>& pending_stops,
              uint64_t sequence);

    bool load(OrderBook& book, std::vector<Order>& pending_stops,
              uint64_t& sequence);

private:
    std::string path_;
    static constexpr uint32_t kMagic = 0x4C4F4201;
};

} // namespace lob
