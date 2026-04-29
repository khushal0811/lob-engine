#include "book/price_level.hpp"

namespace lob {

void PriceLevel::remove(OrderId id, Quantity visible_qty) {
    // Mark as cancelled and subtract volume. The deque entry is lazily
    // drained the next time it reaches the front via drain_cancelled().
    cancelled_.insert(id);
    total_volume_ = (total_volume_ >= visible_qty) ? total_volume_ - visible_qty : 0;
}

} // namespace lob
