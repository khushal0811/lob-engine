#include "book/price_level.hpp"

namespace lob {

void PriceLevel::remove(OrderId id, Quantity visible_qty) {
    // O(1): mark as cancelled, subtract volume. The deque entry will be
    // lazily drained when it reaches the front via drain_cancelled().
    cancelled_.insert(id);
    total_volume_ = (total_volume_ >= visible_qty) ? total_volume_ - visible_qty : 0;
}

} // namespace lob
