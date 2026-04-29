# Data structures — lob-engine

This document explains every core data structure used in the matching engine, the rationale for each choice, and the trade-offs considered.

---

## `std::map` for price levels

```cpp
std::map<Price, PriceLevel, std::greater<Price>> bids_;  // descending: highest first
std::map<Price, PriceLevel>                      asks_;  // ascending: lowest first
```

### Why `std::map`

The order book requires fast access to the best price (front of the sorted sequence) and fast insertion/deletion at arbitrary prices. `std::map` provides:

- **O(log n) insert, erase, and find** for any price level
- **O(1) access to best price** via `begin()` (guaranteed by sorted order)
- **Stable iterators** — inserting or erasing other levels does not invalidate pointers to existing levels, which is critical because the matching engine holds pointers to levels during match loops

### Trade-offs vs alternatives

| Alternative | Pros | Cons |
|---|---|---|
| `std::unordered_map` | O(1) lookup | No sorted order — finding the best price is O(n). Not viable. |
| Flat sorted `std::vector` | Better cache locality, faster iteration | O(n) insert due to shifting. Not viable for deep books. |
| Skip list | O(log n) with better constant factors in theory | More complex to implement, harder to debug, no STL implementation. |
| `boost::flat_map` | Cache-friendly sorted container | O(n) insert on deep books. Good for shallow books (<15 levels). |

**Documented as future work**: For shallow books (under 10–15 levels), a flat sorted vector would likely outperform `std::map` due to cache locality. This is noted in `docs/performance.md` as a future optimisation candidate.

---

## `std::unordered_map` for order ID lookup

```cpp
std::unordered_map<OrderId, Order> id_map_;
```

### Why `std::unordered_map`

Every cancel, modify, and replace operation requires looking up an order by its unique ID. This must be O(1):

- **O(1) average insert, find, erase** (amortised over hash collisions)
- `OrderId` is `uint64_t`, which has a trivial hash function with excellent distribution
- The map stores the full `Order` struct by value — no pointer chasing

### Load factor and reserve strategy

The default load factor of `std::unordered_map` is 1.0, meaning rehash occurs when `size >= bucket_count`. For a benchmark with 100,000 live orders, this could trigger several rehashes during warmup.

**Documented as future work**: Pre-calling `id_map_.reserve(expected_capacity)` before a benchmark run would eliminate rehash overhead. Expected improvement: 5–10% on the large benchmark scenario.

### Why not `std::map<OrderId, Order>`

Sorted order by ID is never needed. The extra overhead of balanced tree operations (O(log n) instead of O(1)) is unjustified.

---

## `std::deque` for level queue

```cpp
mutable std::deque<OrderId> queue_;
```

Each `PriceLevel` maintains a FIFO queue of order IDs. The deque stores only `OrderId` values (8 bytes each), not full `Order` structs.

### Why `std::deque`

- **O(1) push_back** — new orders are appended to the back
- **O(1) pop_front** — filled orders are removed from the front
- **Stable references** — unlike `std::vector`, inserting at the back does not invalidate existing elements (important for the lazy deletion optimisation)
- **Good cache locality** — deque stores elements in contiguous chunks (typically 512 bytes), providing better spatial locality than `std::list`

### Why not `std::list`

`std::list` has O(1) erase at any position, but:

- Each node is a separate heap allocation (16 bytes overhead per node on 64-bit)
- Pointer chasing between nodes defeats CPU prefetching
- For 8-byte `OrderId` values, the overhead-to-payload ratio is terrible

### Why not `std::vector`

`std::vector` would require O(n) shifting on erase from the front or middle. While `pop_front` could be simulated by incrementing a start index, this wastes memory for long-running levels.

### Lazy deletion optimisation

The original implementation used `std::find` + `std::deque::erase` for order cancellation, which was O(n) due to linear scan and memory shifting. This was the primary performance bottleneck (24% of CPU time in profiling).

The current implementation uses lazy deletion:

```cpp
mutable std::unordered_set<OrderId> cancelled_;

void remove(OrderId id, Quantity visible_qty) {
    cancelled_.insert(id);  // O(1)
    total_volume_ -= visible_qty;
}

void drain_cancelled() const noexcept {
    while (!queue_.empty() && cancelled_.count(queue_.front())) {
        cancelled_.erase(queue_.front());
        queue_.pop_front();
    }
}
```

Cancelled IDs are recorded in a hash set. The deque is only cleaned up lazily when the front is accessed during matching. This shifts the cost from `remove()` (O(1) instead of O(n)) to `front()` (amortised O(1) per drain).

The `mutable` qualifier on `queue_` and `cancelled_` allows `front()` and `empty()` to remain logically const while performing lazy cleanup.

---

## `SpscQueue` — lock-free single-producer single-consumer ring buffer

```cpp
template <typename T, size_t Capacity>
class SpscQueue { ... };
```

### Purpose

The `SpscQueue` is used for the ingress path between the event source (producer) and the matching engine (consumer). It decouples event parsing from matching without introducing lock contention.

### Why lock-free

- **No mutex contention**: The matching engine's hot path must not block on a lock. A lock-free ring buffer using atomic load/store operations provides wait-free progress for both producer and consumer.
- **Cache-line alignment**: The read and write cursors are stored on separate cache lines to prevent false sharing between the producer and consumer cores.
- **Bounded memory**: The ring buffer has a fixed capacity set at compile time. If the producer outpaces the consumer, it drops events rather than allocating unbounded memory.

### Implementation details

- Uses `std::atomic<size_t>` for head (write) and tail (read) cursors
- `push()` and `pop()` use `memory_order_acquire` / `memory_order_release` for minimal synchronisation overhead
- Capacity is a power of 2 for efficient modulo via bitmask
- Returns `false` on full (push) or empty (pop) — never blocks

---

## `std::vector<Order>` for pending stops

```cpp
std::vector<Order> pending_stops_;
```

### Why a simple vector

Pending stop orders are stored as a flat vector. On every trade, the entire vector is scanned to check if any stop's trigger price has been reached.

This is a deliberate simplicity trade-off:

- Stop orders are typically rare compared to limit and market orders
- The vector provides excellent cache locality for iteration
- Insertion and removal are infrequent (stops are submitted once, triggered once)

### Trade-off vs priority queue

A `std::priority_queue` sorted by stop price would allow O(1) trigger evaluation:
- Buy stops in ascending order → check only the front
- Sell stops in descending order → check only the front

This would reduce trigger evaluation from O(n) to O(log n) on insert and O(1) on trigger check. However, it only matters when the stop order count is large.

**Documented as future work**: This is noted in `docs/performance.md` as a future optimisation candidate for workloads with many pending stop orders.
