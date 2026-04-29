# High-performance C++ limit order book — project scope

## Overview

A production-style limit order book (LOB) and matching engine written in C++17/20. The engine targets exchange-grade correctness and low-latency execution, with deterministic replay, a benchmark suite, and full CI/CD infrastructure. Scope covers the core system only — no optional extras.

---

## Order types

| Type | Description |
|---|---|
| Limit | Resting order at a specified price |
| Market | Immediate execution at best available price, sweeps levels |
| Cancel | Remove a resting order by ID |
| Modify / Replace | Amend quantity or price of a resting order |
| Stop | Converts to market order when trigger price is hit |
| Stop-limit | Converts to limit order when trigger price is hit |
| Iceberg | Visible peak quantity with hidden reserve; replenishes on fill |

Fill-or-kill and immediate-or-cancel are excluded from core scope.

---

## Matching rules

- Strict price-time priority (FIFO at each price level)
- Partial fills supported — residual quantity rests on book
- Full fills remove order from book and ID map
- Market orders sweep multiple price levels until filled or book exhausted
- Residual market order quantity after exhausting book is cancelled with a record
- Stop orders are held in a pending stop list; triggered when last trade price crosses their trigger
- Iceberg replenishment is deterministic and preserves time priority of the peak, not the reserve
- All execution is deterministic and single-threaded in the matching core

---

## Order book features

- Bid side sorted descending by price
- Ask side sorted ascending by price
- Per-price-level FIFO order queue
- O(1) order lookup by ID (hash map)
- Per-level aggregated visible volume
- Cached best bid and best ask (updated on every mutation)
- Mid-price, spread, and last trade price tracking

---

## Trade output

Every matching event produces structured output:

- **Trade record** — aggressor ID, passive ID, price, quantity, timestamp, aggressor side
- **Partial fill record** — remaining quantity on the passive order
- **Execution summary** — total filled quantity, average fill price, status
- **Book snapshot** — top-N levels of bid and ask with price and volume

---

## Data and event pipeline

### Input sources

| Source | Description |
|---|---|
| Synthetic generator | Configurable simulated market flow |
| CSV replay | Structured event log replayed in sequence |

The synthetic generator supports: clustered prices around mid, order bursts, spread changes, volatility spikes, market-maker liquidity patterns, random trader behaviour, and cancellation-heavy scenarios.

### Supported event types

- New order (any supported type)
- Cancel
- Modify / Replace
- Stop trigger activation
- Market event replay tick

---

## Architecture modules

| Module | Responsibility |
|---|---|
| `core/` | Order models, enums, trade records, event types |
| `engine/` | Matching engine, stop trigger processor, iceberg replenishment |
| `book/` | Order book state, price level manager, best bid/ask cache |
| `feed/` | Synthetic generator, CSV replay reader |
| `risk/` | Order validator, sanity checks |
| `logging/` | Async structured logger |
| `metrics/` | Throughput, latency histograms, microstructure signals |
| `snapshot/` | Book state serialisation, snapshot save/restore |
| `tests/` | Unit, replay, stress, and invariant tests |
| `benchmarks/` | Scenario benchmarks, profiling harness |
| `tools/` | CLI runner, replay tool |
| `scripts/` | CI helpers, data generation |
| `docs/` | Design documents, benchmark reports |

---

## Validation and risk layer

Checks applied to every inbound order before it reaches the matching core:

- Invalid price (zero, negative, or NaN)
- Invalid quantity (zero or negative)
- Duplicate order ID
- Cancel targeting a non-existent order ID
- Modify targeting an order in an invalid state
- Stop trigger price inconsistency (e.g. buy stop below current market)
- Price band checks (configurable max deviation from mid)
- Maximum order size check (configurable)

Rejected orders produce a structured rejection record with reason code.

---

## Snapshot and replay

- Book state snapshot serialised to binary at configurable intervals
- Snapshot includes: all resting orders, price levels, stop pending list, sequence number, last trade price
- Deterministic replay: load snapshot, feed event stream, verify output matches expected
- Replay engine used for both testing and historical analysis
- All outputs from replay of identical inputs must be bit-identical

---

## Metrics and observability

### Execution metrics

- Throughput (orders/sec)
- Average matching latency
- p50, p95, p99, max latency (latency histogram)
- Fill rate
- Rejection rate
- Cancel rate

### Market microstructure signals

- Top-of-book depth (bid and ask)
- Order imbalance (bid volume vs ask volume)
- Spread evolution over time
- Volume by price level
- Trade frequency
- Aggressor side tracking (buy-initiated vs sell-initiated)

---

## Testing strategy

### Unit tests

- Order insertion and validation
- Price level FIFO behaviour
- Limit and market order matching
- Cancel, modify, replace logic
- Stop order trigger behaviour
- Iceberg replenishment
- Snapshot save and restore

### Replay tests

- Known event streams with known expected outputs
- Deterministic state validation (replay twice, compare outputs)

### Stress tests

- High-volume order ingestion
- Heavy cancellation workload
- Volatile synthetic market bursts

### Invariant / property tests

- Total volume consistency (sum of level volumes equals sum of order quantities)
- Book ordering correctness (no inversion of bid/ask sides)
- No crossed book after any matching operation
- ID map consistency (every resting order has an entry; no stale entries)

### Sanitizer coverage

- AddressSanitizer (ASan)
- UndefinedBehaviorSanitizer (UBSan)

---

## Benchmark suite

Benchmark scenarios are first-class deliverables, not afterthoughts.

| Scenario | Description |
|---|---|
| Small market | Light order flow, low depth |
| Medium market | Moderate flow, typical spread |
| Large market | High order rate, deep book |
| High-cancel market | Cancellation-heavy workload |
| Market-order-heavy | Aggressive sweep-heavy flow |
| Iceberg / stop flow | Advanced order type mix |

Each scenario produces:

- Throughput measurement (orders/sec)
- Latency histogram (p50 / p95 / p99 / max)
- Fill and cancel rates

### Profiling deliverables

- Hot path identification (perf or gprof)
- Container and data structure evaluation (map vs flat_map vs sorted vector)
- Allocation overhead measurement
- Documented bottleneck and optimisation applied
- Before/after benchmark comparison with quantified gain

---

## CI/CD pipeline

| Stage | Tool |
|---|---|
| Build (GCC) | CMake + GCC |
| Build (Clang) | CMake + Clang |
| Unit + replay tests | GoogleTest |
| Formatting check | clang-format |
| Static analysis | clang-tidy |
| Sanitizer build | ASan + UBSan |
| Benchmark (nightly / manual) | Custom benchmark binary |
| Release artifact | Tagged release build |

Pipeline hosted on GitHub Actions. All stages must pass on both compilers before merge.

---

## Documentation deliverables

### README

- Project overview and positioning statement
- Why the limit order book is a core exchange component
- Architecture diagram
- Matching rules summary
- Order lifecycle description
- Supported order types
- Benchmark results table
- Optimisation notes
- Replay and snapshot explanation
- Build instructions
- Future work

### docs/ folder

- `engine-design.md` — matching algorithm, stop trigger, iceberg logic
- `data-structures.md` — price level container choices and rationale
- `performance.md` — benchmark results, profiling findings, optimisations applied
- `testing.md` — test strategy, invariant definitions, sanitizer setup

---

## Excluded from scope

The following are deliberately out of scope for the core build:

- Fill-or-kill / immediate-or-cancel order types
- Web frontend or GUI
- Authentication or session management
- Cloud-native or distributed design
- Databases in the hot path
- Multi-symbol support
- Custom allocator
- Lock-free queue (ring buffer uses a standard SPSC pattern)
- Real market data feed integration

---

## One-line positioning

> Built a high-performance C++ limit order book and matching engine with advanced order types, deterministic replay, snapshotting, benchmarked low-latency execution, and production-style CI/CD, testing, and profiling infrastructure.
