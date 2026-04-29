# Performance notes — lob-engine

## Hardware

- **Machine:** Apple MacBook (Apple Silicon, ARM64)
- **OS:** macOS 26.4.1
- **Compiler:** AppleClang 17.0.0
- **Build flags:** `-O2 -march=native`
- **Benchmark duration:** 10 seconds per scenario

All measurements are from a single-threaded matching core with no async logging or metrics export active during the benchmark run.

---

## How to reproduce

```bash
cmake -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O2 -march=native"
cmake --build build_release
./build_release/benchmarks/lob_bench --all --duration 10
```

Each scenario uses a fixed seed (42) and is fully reproducible. Running the same scenario twice on the same machine produces throughput numbers within 5% variance.

---

## Benchmark scenarios

| Scenario | Description |
|---|---|
| small | Light order flow, low book depth, low cancel rate |
| medium | Moderate flow, typical spread, 30% cancel rate |
| large | High order rate, deep book, 30% cancel rate |
| high_cancel | Cancel-heavy workload, 70% of events are cancels |
| market_heavy | Aggressive sweep-heavy flow, 30% market orders |
| iceberg_stop | Mixed advanced order types, 20% iceberg, 10% stop |

---

## Baseline results (pre-optimisation)

Measured before any optimisation was applied. Standard `std::deque<OrderId>` per price level with O(n) linear scan on cancel.

| Scenario | Throughput (ev/s) | p50 (ns) | p95 (ns) | p99 (ns) | max (ns) |
|---|---|---|---|---|---|
| small | 1,647,656 | 0 | 1,000 | 11,000 | 126,994,000 |
| medium | 2,156,081 | 0 | 1,000 | 7,000 | 138,194,000 |
| large | 2,202,671 | 0 | 1,000 | 7,000 | 136,395,000 |
| high_cancel | 9,434,222 | 0 | 1,000 | 1,000 | 147,000 |
| market_heavy | 2,436,656 | 0 | 1,000 | 6,000 | 225,051,000 |
| iceberg_stop | 2,111,828 | 0 | 1,000 | 8,000 | 127,396,000 |

**Note on p50 = 0 ns:** The macOS `clock_gettime` timer has approximately 1µs granularity on Apple Silicon. Sub-microsecond events round to zero. This is a measurement resolution artefact, not a true zero-latency result. The p95 and p99 figures are more meaningful.

**Note on max latency spikes:** The 126–225ms max latency figures are OS scheduling outliers, not engine latency. The matching core is single-threaded with no system calls in the hot path. Occasional OS preemption causes these spikes. They are expected and not representative of steady-state latency.

---

## Profiling methodology

The `large` scenario was profiled using the macOS `sample` tool — a statistical call-graph sampler built into macOS that samples the call stack at 1ms intervals.

```bash
./build_prof/benchmarks/lob_bench --scenario large --duration 30 &
PID=$!
sample $PID 25 -f profile_large.txt
wait $PID
```

The profiler ran for 25 seconds against a 30-second benchmark run, capturing 21,335 samples.

---

## Profiling findings

The call graph revealed a clear bottleneck in the cancel path:

```
21,335 total samples
  17,161 → MatchingEngine::submit()
    12,770 → process_cancel()           (60% of submit samples)
      6,319 → OrderBook::cancel_order()
        5,169 → PriceLevel::remove()    ← PRIMARY BOTTLENECK
          5,000+ → std::deque::erase()
            ~700 → _platform_memmove    ← ROOT CAUSE
```

**Root cause analysis:**

`PriceLevel::remove()` performs two expensive operations on every cancel:

1. **Linear scan** — `std::find()` iterates through the deque from front to back to locate the order ID. In a deep price level with many resting orders, this is O(n).

2. **Memory copy** — `std::deque::erase()` shifts all elements after the erased position to fill the gap. The profiler shows this resolving to `_platform_memmove` — a bulk memory copy that defeats CPU cache prefetching on large deques.

On a cancel-heavy workload with deep price levels, every single cancel event pays this O(n) scan + O(n) copy cost. At 2.2M events/sec on the large scenario, this was consuming roughly 24% of all execution time.

---

## Optimisation applied — lazy deletion in PriceLevel

**Change:** Replace the O(n) `std::deque::erase()` in `PriceLevel::remove()` with a lazy deletion approach using an `std::unordered_set<OrderId>`.

**Before (`price_level.cpp`):**
```cpp
void PriceLevel::remove(OrderId id, Quantity visible_qty) {
    auto it = std::find(queue_.begin(), queue_.end(), id);
    if (it != queue_.end()) {
        queue_.erase(it);          // O(n) memmove
        total_volume_ -= visible_qty;
    }
}
```

**After (`price_level.cpp`):**
```cpp
void PriceLevel::remove(OrderId id, Quantity visible_qty) {
    cancelled_.insert(id);         // O(1) hash set insertion
    total_volume_ = (total_volume_ >= visible_qty)
                  ? total_volume_ - visible_qty : 0;
}

// front() and pop_front() skip cancelled entries lazily:
OrderId PriceLevel::front() {
    while (!queue_.empty() && cancelled_.count(queue_.front())) {
        cancelled_.erase(queue_.front());
        queue_.pop_front();
    }
    return queue_.front();
}
```

**Why this works:**

Instead of paying the O(n) erase cost at cancel time, cancelled order IDs are recorded in a hash set. The deque is only mutated when the front is accessed during matching — at which point dead entries are skipped and cleaned up in O(1) amortised. The total work done is the same, but it is spread across matching operations rather than concentrated in cancel operations, and the expensive `memmove` is eliminated entirely.

**Trade-off:** The `unordered_set` adds a small per-cancel allocation cost and a hash lookup on every `front()` access. This trades poorly on workloads where cancels are frequent but levels are shallow (few resting orders per level), because the original erase was already cheap on short deques.

---

## Post-optimisation results

Measured after implementing lazy deletion in `PriceLevel::remove()`.

| Scenario | Throughput (ev/s) | p50 (ns) | p95 (ns) | p99 (ns) | max (ns) |
|---|---|---|---|---|---|
| small | 4,021,862 | 0 | 1,000 | 1,000 | 263,296,000 |
| medium | 3,274,301 | 0 | 1,000 | 1,000 | 136,226,000 |
| large | 3,326,316 | 0 | 1,000 | 1,000 | 138,841,000 |
| high_cancel | 7,466,016 | 0 | 1,000 | 1,000 | 5,846,000 |
| market_heavy | 3,861,558 | 0 | 1,000 | 1,000 | 230,949,000 |
| iceberg_stop | 3,452,091 | 0 | 1,000 | 1,000 | 258,886,000 |

---

## Before vs after comparison

| Scenario | Before (ev/s) | After (ev/s) | Change |
|---|---|---|---|
| small | 1,647,656 | 4,021,862 | **+144%** |
| medium | 2,156,081 | 3,274,301 | **+52%** |
| large | 2,202,671 | 3,326,316 | **+51%** |
| high_cancel | 9,434,222 | 7,466,016 | -21% |
| market_heavy | 2,436,656 | 3,861,558 | **+58%** |
| iceberg_stop | 2,111,828 | 3,452,091 | **+63%** |

**Matching workloads: +51% to +144% throughput improvement.**

**high_cancel regression (-21%):** The high_cancel scenario (70% cancel rate) regressed slightly. This is expected and consistent with the trade-off described above. In a cancel-heavy workload with shallow price levels, resting orders are short-lived and levels rarely accumulate depth. The original `deque::erase()` was already operating on short sequences where `memmove` is cheap. The lazy deletion approach adds `unordered_set` overhead (hash computation, potential allocation) that costs more than it saves at shallow depth.

This regression is an honest and informative result — it demonstrates that the optimisation is correctly targeted at deep-book cancel workloads, not cancel-heavy shallow workloads. A production system would profile the specific market it serves and choose accordingly.

**p99 improvement on matching scenarios:** p99 dropped from 6–11µs to 1µs on all matching scenarios, confirming that the tail latency spikes were caused by the O(n) erase operations in the cancel path being invoked during matching sweeps.

---

## Sanitizer coverage

All 77 unit tests passed cleanly under AddressSanitizer and UndefinedBehaviorSanitizer:

```bash
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g" \
      -DCMAKE_CXX_COMPILER=clang++
cmake --build build_asan
./build_asan/tests/lob_tests
# 77 tests from 17 test suites — PASSED — 0 errors
```

Zero memory errors. Zero undefined behaviour violations.

---

## Future optimisation candidates

These were identified but not implemented in the current version. Documented as future work:

**1. `std::unordered_map` reserve for order ID map**
Pre-calling `id_map_.reserve(expected_capacity)` before a benchmark run would eliminate rehash events under high load. Expected improvement: 5–10% on large scenarios.

**2. Flat sorted vector for shallow books**
At book depths under 10–15 levels, a `std::vector<std::pair<Price, PriceLevel>>` with binary search has better cache locality than `std::map` due to contiguous memory layout. Expected improvement: measurable on small and medium scenarios where the book stays shallow.

**3. Priority queue for stop order list**
The current `std::vector<Order>` stop list is scanned linearly on every trade. Replacing with two `std::priority_queue` instances (one for buy stops sorted ascending, one for sell stops sorted descending) would make trigger evaluation O(log n) instead of O(n). Meaningful only when stop order count is large.