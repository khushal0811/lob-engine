# Architecture — high-performance C++ limit order book

## Design philosophy

The engine is designed around three principles:

1. **Determinism first.** Given the same sequence of input events, the engine must produce bit-identical output every time. This is non-negotiable for correctness, replay, and debugging.
2. **Concurrency at the edges, not the core.** The matching core is single-threaded. Concurrency lives in ingestion (lock-free ring buffer) and output (async logger, async metrics export). This is how real exchange matching engines are built.
3. **Measure before optimising.** The baseline is correct and clear. Optimisations are applied to measured hot paths, with before/after benchmarks documenting every change.

---

## Top-level data flow

```
producers
  ├── synthetic generator
  ├── CSV replay reader
  └── (future: real feed adapter)
        │
        ▼
  lock-free SPSC ring buffer   ← ingress queue (producer threads)
        │
        ▼
  order validator + risk layer  ← rejects malformed / risky orders
        │
        ▼
  matching core  (single thread, deterministic)
  ├── order book state manager
  ├── price level manager
  ├── matching engine
  ├── stop trigger processor
  └── iceberg replenishment handler
        │
        ├──▶  trade output records
        ├──▶  async structured logger
        ├──▶  async metrics collector
        └──▶  snapshot subsystem
```

The matching core consumes one event at a time. There are no locks inside it. All shared state lives in a single thread.

---

## Directory structure

```
lob/
├── src/
│   ├── core/          # Order models, enums, event types, trade records
│   ├── engine/        # Matching engine, stop processor, iceberg handler
│   ├── book/          # Order book, price level manager, best bid/ask cache
│   ├── feed/          # Synthetic generator, CSV replay reader
│   ├── risk/          # Validator, rejection records
│   ├── logging/       # Async structured logger
│   ├── metrics/       # Latency histogram, throughput counters, microstructure
│   └── snapshot/      # Serialisation, snapshot save/restore
├── tests/
│   ├── unit/
│   ├── replay/
│   ├── stress/
│   └── invariants/
├── benchmarks/
├── tools/             # CLI runner, replay CLI
├── scripts/           # CI helpers, data generators
├── docs/
│   ├── engine-design.md
│   ├── data-structures.md
│   ├── performance.md
│   └── testing.md
├── data/
│   └── sample-replay/ # Example CSV event logs
├── CMakeLists.txt
├── .clang-format
├── .clang-tidy
└── README.md
```

---

## Module responsibilities

### `src/core/` — domain models

Everything the rest of the system depends on. No dependencies on other modules.

| File | Contents |
|---|---|
| `order.hpp` | `Order` struct: ID, side, type, price, quantity, peak qty (iceberg), stop price, timestamp, status |
| `trade.hpp` | `Trade` struct: aggressor ID, passive ID, price, qty, timestamp, side |
| `enums.hpp` | `Side`, `OrderType`, `OrderStatus`, `RejectReason`, `EventType` |
| `event.hpp` | `OrderEvent` tagged union / variant covering all input event types |
| `execution.hpp` | `ExecutionReport`, `PartialFill`, `Rejection` |
| `config.hpp` | `EngineConfig`: price band limits, max order size, snapshot interval, log path |

Design notes:
- `Order` uses a plain struct with no virtual methods — it lives in flat containers and must be cheap to copy and move.
- `OrderEvent` is a `std::variant<NewOrder, CancelOrder, ModifyOrder, ReplaceOrder>` — exhaustive pattern matching via `std::visit`.
- All timestamps are `uint64_t` nanoseconds since epoch.

---

### `src/book/` — order book state

| File | Contents |
|---|---|
| `price_level.hpp` | `PriceLevel`: price, total visible volume, FIFO deque of order IDs |
| `order_book.hpp` | `OrderBook`: bid map, ask map, order ID → Order map, stop list |
| `book_snapshot.hpp` | `BookSnapshot`: serialisable representation of full book state |

Key data structure decisions:

**Bid and ask sides:** `std::map<Price, PriceLevel>` (descending for bids via `std::greater<Price>`). Gives O(log n) insertion, O(1) iteration to best level (begin()), and automatic sorted order.

**Order ID lookup:** `std::unordered_map<OrderId, Order>`. O(1) average lookup for cancel and modify. This is the hot path for cancel-heavy workloads.

**Price level queue:** `std::deque<OrderId>` per level. O(1) push_back (new resting order), O(1) pop_front (fill from front). Supports partial fill by modifying front order quantity in-place.

**Best bid/ask cache:** Maintained as pointers/iterators to `begin()` of each side's map. Invalidated and refreshed on every level deletion. Avoids repeated map lookups in the matching hot path.

**Stop order list:** `std::vector<Order>` (unsorted). Triggers are checked on every trade. This is acceptable at core scope — a priority queue optimisation is documented as future work.

---

### `src/engine/` — matching logic

| File | Contents |
|---|---|
| `matching_engine.hpp/.cpp` | Main engine class: `submit(OrderEvent)`, internal match loop |
| `match_limit.cpp` | Limit order matching logic |
| `match_market.cpp` | Market order multi-level sweep |
| `stop_processor.hpp/.cpp` | Stop trigger evaluation and conversion |
| `iceberg_handler.hpp/.cpp` | Peak replenishment logic |

**Matching algorithm — limit order:**

```
on new limit buy at price P:
  while ask side is not empty
    and best ask price <= P
    and order has remaining quantity:
      fill against front of best ask level (partial or full)
      if level is empty: remove level, update best ask cache
  if order has remaining quantity:
    insert into bid side at price P (back of queue at that level)
    update order ID map
```

Sell limit is symmetric.

**Matching algorithm — market order:**

```
on new market buy:
  while ask side is not empty
    and order has remaining quantity:
      fill against front of best ask level
      if level is empty: remove level
  if residual remains: cancel with MARKET_EXHAUSTED reason, emit rejection record
```

**Stop trigger evaluation:**

```
after every trade at price T:
  for each stop order in pending list:
    if buy stop and T >= trigger price: convert to market, submit
    if sell stop and T <= trigger price: convert to market, submit
  (stop-limit converts to limit instead of market)
```

**Iceberg replenishment:**

```
after a fill reduces peak quantity to zero:
  if reserve quantity > 0:
    new_peak = min(original_peak_size, reserve_quantity)
    reserve_quantity -= new_peak
    peak_quantity = new_peak
    re-insert order at back of queue at its price level
    (time priority is reset — this is correct exchange behaviour)
```

---

### `src/feed/` — input sources

| File | Contents |
|---|---|
| `synthetic_gen.hpp/.cpp` | Configurable market simulator |
| `csv_replay.hpp/.cpp` | CSV event log reader and parser |
| `feed_interface.hpp` | Abstract `IFeed` interface for both sources |

**Synthetic generator parameters (all configurable via `EngineConfig`):**

- Mid-price starting point and drift
- Order arrival rate (orders/sec)
- Price clustering standard deviation around mid
- Probability of cancel vs new order
- Burst mode: N orders in rapid succession, then pause
- Market maker mode: maintain N levels of resting bids and asks
- Volatility spike: randomly widen spread for K events
- Cancellation storm: K% of events are cancel orders

**CSV format:**

```
event_type,order_id,side,type,price,quantity,peak_qty,stop_price,timestamp
NEW,1,BUY,LIMIT,10050,100,0,0,1700000000000000000
NEW,2,SELL,LIMIT,10060,50,0,0,1700000000000001000
CANCEL,1,,,,,,,1700000000000002000
```

Missing fields for non-applicable columns are left empty. Parser validates each row and emits a `MalformedEvent` record for any unparseable line rather than aborting.

---

### `src/risk/` — validation layer

| File | Contents |
|---|---|
| `validator.hpp/.cpp` | `Validator` class: `validate(OrderEvent) -> ValidationResult` |
| `rejection.hpp` | `Rejection` struct with `RejectReason` enum |

Checks applied in order:

1. Price > 0 (limit and stop orders)
2. Quantity > 0
3. Order ID not already in book (for NEW events)
4. Order ID exists in book (for CANCEL, MODIFY, REPLACE)
5. Order in a cancellable state (for CANCEL)
6. Modified price and quantity are valid
7. Stop trigger price on correct side of current mid
8. Price within configured band (mid ± band_bps basis points)
9. Quantity below configured maximum

The validator is a pure function — it reads the current book state (best bid, best ask, order ID map) but never mutates it.

---

### `src/logging/` — async structured logger

| File | Contents |
|---|---|
| `logger.hpp/.cpp` | `Logger`: lock-free queue, background writer thread |
| `log_record.hpp` | `LogRecord` struct: level, category, message, timestamp |

The logger runs a dedicated background thread that drains a lock-free queue to disk. The matching core enqueues log records in O(1) without blocking. If the queue is full, records are dropped and a drop counter is incremented (never blocking the matching core).

Log categories: `TRADE`, `CANCEL`, `REJECT`, `SNAPSHOT`, `METRIC`, `SYSTEM`.

Output format: structured JSON lines, one record per line, suitable for ingestion by external tools.

---

### `src/metrics/` — observability

| File | Contents |
|---|---|
| `metrics.hpp/.cpp` | `Metrics` class: counters, latency histogram |
| `histogram.hpp` | `LatencyHistogram`: HDR-style fixed-bucket histogram |
| `microstructure.hpp` | `MicrostructureTracker`: spread, imbalance, depth |

**Latency measurement:** Timestamp is captured when the event enters the matching core and again when execution output is emitted. Delta is recorded in the histogram.

**Histogram buckets:** 1 µs resolution up to 1 ms, 10 µs resolution up to 10 ms, coarse above that. Reports p50, p95, p99, max.

**Microstructure signals computed after every event:**

- Spread = best ask − best bid
- Mid = (best ask + best bid) / 2
- Order imbalance = (bid volume − ask volume) / (bid volume + ask volume)
- Top-N depth snapshot (bid and ask)
- Aggressor side of last trade

---

### `src/snapshot/` — state persistence

| File | Contents |
|---|---|
| `snapshot.hpp/.cpp` | `SnapshotManager`: serialise and deserialise book state |
| `snapshot_format.hpp` | Binary format definitions |

**Snapshot contents:**

- Engine sequence number (monotonically increasing event counter)
- Last trade price
- All resting orders (full `Order` structs)
- Pending stop order list
- Timestamp

**Replay procedure:**

1. Load snapshot → restore book state
2. Feed subsequent events from the event log starting from the snapshot sequence number
3. Verify output matches expected output file

This is the foundation of the deterministic replay test suite.

---

### `tests/` — testing strategy

| Directory | Test type |
|---|---|
| `tests/unit/` | One file per module, GoogleTest fixtures |
| `tests/replay/` | Known event CSVs + expected output files, diff-based validation |
| `tests/stress/` | High-volume synthetic workloads, run under ASan + UBSan |
| `tests/invariants/` | Property checks run after every operation on a live engine instance |

**Invariants checked after every event in invariant tests:**

- Sum of order quantities at each level equals level's reported volume
- Bid side prices are strictly descending
- Ask side prices are strictly ascending
- Best bid < best ask (no crossed book)
- Every order ID in every level's queue exists in the ID map
- No order ID appears in more than one level's queue

---

### `benchmarks/` — performance harness

| File | Contents |
|---|---|
| `bench_main.cpp` | Entry point, scenario selection |
| `scenarios/` | One file per benchmark scenario |
| `report.md` | Generated benchmark output |

Each scenario runs for a configurable duration (default 10 seconds) and reports:
- Total events processed
- Throughput (events/sec)
- Latency histogram

Scenarios are parameterised by a seed so they are reproducible.

---

## Concurrency model

```
thread 1: feed producer
  └── writes to SPSC ring buffer

thread 2: matching core (main)
  ├── reads from ring buffer
  ├── runs validator
  ├── runs matching engine
  └── enqueues to log queue + metrics queue

thread 3: async logger
  └── drains log queue to disk

thread 4: async metrics exporter (optional)
  └── drains metrics queue, computes histograms
```

The ring buffer between threads 1 and 2 is the only cross-thread communication on the critical path. All other queues are best-effort with drop-on-full semantics so they never back-pressure the matching core.

---

## Build system

**CMake targets:**

| Target | Description |
|---|---|
| `lob_core` | Static library: all `src/` modules |
| `lob_engine` | Executable: CLI runner (`tools/`) |
| `lob_tests` | Test executable: all test suites |
| `lob_bench` | Benchmark executable |

**Compiler flags (both GCC and Clang):**

```
-std=c++20
-O2 -march=native          (release)
-O0 -g -fsanitize=address,undefined  (sanitizer build)
-Wall -Wextra -Wpedantic -Werror
```

**clang-format style:** Based on LLVM, column limit 100, pointer alignment left.

**clang-tidy checks enabled:** `modernize-*`, `performance-*`, `readability-*`, `bugprone-*`, `cppcoreguidelines-*` (with selected exclusions documented in `.clang-tidy`).

---

## Key design decisions and rationale

| Decision | Rationale |
|---|---|
| `std::map` for price levels | Correct O(log n) insertion, automatic ordering, begin() is always best price. Evaluated against `std::flat_map` and sorted `std::vector` in benchmarks. |
| `std::unordered_map` for order IDs | O(1) lookup is required for cancel/modify in cancel-heavy workloads. |
| `std::deque` per price level queue | O(1) front pop and back push. Random access not needed. |
| Single-threaded matching core | Eliminates all locking in the hot path. Determinism is free. Concurrency lives at the edges. |
| `std::variant` for events | Exhaustive pattern matching via `std::visit`. No virtual dispatch. Zero-cost abstraction. |
| Lock-free SPSC ring buffer | Ingestion can happen on a separate thread without mutex overhead. |
| Binary snapshot format | Faster serialisation than JSON or text. Deterministic byte layout for replay comparison. |
| Async logger with drop semantics | Logging never blocks the matching core. Drop counter surfaces the issue rather than silently degrading latency. |

---

## Performance optimisation narrative

The project documents a deliberate optimisation story across three phases:

**Phase 1 — baseline.** Correct implementation using standard containers. Benchmarked and profiled. Hot paths identified.

**Phase 2 — targeted optimisation.** Based on profiling data, one or two specific improvements are applied — typically reducing heap allocations in the level queue, improving cache locality in the ID map, or reducing branch mispredictions in the sweep loop. Each change is benchmarked.

**Phase 3 — documented gains.** A `docs/performance.md` write-up presents: the profiling methodology, the bottleneck identified, the change applied, and the measured before/after throughput and latency improvement.

This is the part of the project that reads as genuinely mature systems work.
