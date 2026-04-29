#include "snapshot/snapshot.hpp"
#include <cstring>

namespace lob {

SnapshotManager::SnapshotManager(std::string path) : path_(std::move(path)) {}

bool SnapshotManager::save(const OrderBook& book,
                            const std::vector<Order>& pending_stops,
                            uint64_t sequence) {
    std::ofstream out(path_, std::ios::binary);
    if (!out) return false;

    // Magic
    out.write(reinterpret_cast<const char*>(&kMagic), sizeof(kMagic));
    // Sequence
    out.write(reinterpret_cast<const char*>(&sequence), sizeof(sequence));
    // Last trade price
    Price ltp = book.last_trade_price;
    out.write(reinterpret_cast<const char*>(&ltp), sizeof(ltp));

    // Collect all orders from the book
    std::vector<Order> orders;
    for (const auto& [price, level] : book.bids()) {
        (void)price;
        (void)level;
    }
    for (const auto& [price, level] : book.asks()) {
        (void)price;
        (void)level;
    }

    // Use the id_map via find_order to serialize all orders
    uint32_t order_count = static_cast<uint32_t>(book.order_count());
    out.write(reinterpret_cast<const char*>(&order_count), sizeof(order_count));

    // We need to iterate all orders - use a snapshot approach
    // Since we can't directly iterate id_map_, we'll iterate levels
    auto write_orders_from_side = [&](const auto& side_map) {
        for (const auto& [price, level] : side_map) {
            (void)price;
            auto ids = level.order_ids();
            for (OrderId id : ids) {
                auto opt = book.find_order(id);
                if (opt) {
                    out.write(reinterpret_cast<const char*>(&(*opt)), sizeof(Order));
                }
            }
        }
    };
    write_orders_from_side(book.bids());
    write_orders_from_side(book.asks());

    // Stop orders
    uint32_t stop_count = static_cast<uint32_t>(pending_stops.size());
    out.write(reinterpret_cast<const char*>(&stop_count), sizeof(stop_count));
    for (const auto& stop : pending_stops) {
        out.write(reinterpret_cast<const char*>(&stop), sizeof(Order));
    }

    return out.good();
}

bool SnapshotManager::load(OrderBook& book, std::vector<Order>& pending_stops,
                            uint64_t& sequence) {
    std::ifstream in(path_, std::ios::binary);
    if (!in) return false;

    uint32_t magic = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != kMagic) return false;

    in.read(reinterpret_cast<char*>(&sequence), sizeof(sequence));

    Price ltp = 0;
    in.read(reinterpret_cast<char*>(&ltp), sizeof(ltp));
    book.last_trade_price = ltp;

    uint32_t order_count = 0;
    in.read(reinterpret_cast<char*>(&order_count), sizeof(order_count));
    for (uint32_t i = 0; i < order_count; ++i) {
        Order o;
        in.read(reinterpret_cast<char*>(&o), sizeof(Order));
        book.insert_order(o);
    }

    uint32_t stop_count = 0;
    in.read(reinterpret_cast<char*>(&stop_count), sizeof(stop_count));
    pending_stops.clear();
    for (uint32_t i = 0; i < stop_count; ++i) {
        Order o;
        in.read(reinterpret_cast<char*>(&o), sizeof(Order));
        pending_stops.push_back(o);
    }

    return in.good();
}

} // namespace lob
