# lob — high-performance C++ limit order book and matching engine

> A production-grade, single-threaded matching engine implementing price-time priority with support for limit, market, stop, stop-limit, and iceberg orders — built from scratch in C++20 with zero external runtime dependencies.

[![CI](https://github.com/khushal0811/lob-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/khushal0811/lob-engine/actions/workflows/ci.yml)

## What this is

A complete limit order book (LOB) and matching engine written in C++20. It processes order events (new, cancel, modify, replace) and produces trades, execution reports, and rejections. The engine is deterministic, fully benchmarked, and sanitizer-clean.

This is **not** a trading bot or strategy backtester. It is the **exchange-side matching core** — the component that decides which orders trade against each other and at what price.

## Why limit order books matter

Every electronic exchange — equities, futures, options, crypto — runs a matching engine at its core. Understanding how an LOB works is fundamental to:

- Market microstructure research
- High-frequency trading system design
- Exchange technology development
- Algorithmic trading strategy evaluation

This project implements the same price-time priority algorithm used by the NYSE, NASDAQ, CME, and most modern exchanges.

## Architecture

```
                    ┌──────────────────────────────────────────┐
                    │            Matching Engine               │
                    │                                          │
  OrderEvent ──────>│  submit()                                │
  (variant)         │    ├── process_new_order()               │
                    │    │     ├── match_limit()                │
                    │    │     ├── match_market()               │
                    │    │     ├── evaluate_stop_triggers()     │
                    │    │     └── replenish_iceberg()          │
                    │    ├── process_cancel()                   │──────> MatchResult
                    │    ├── process_modify()                   │        (trades,
                    │    └── process_replace()                  │         reports,
                    │                                          │         rejections)
                    │  ┌─────────────────────────────────┐     │
                    │  │          OrderBook               │     │
                    │  │                                  │     │
                    │  │  bids_: map<Price, PriceLevel>   │     │
                    │  │         (descending)             │     │
                    │  │                                  │     │
                    │  │  asks_: map<Price, PriceLevel>   │     │
                    │  │         (ascending)              │     │
                    │  │                                  │     │
                    │  │  id_map_: unordered_map<Id,Order>│     │
                    │  └─────────────────────────────────┘     │
                    │                                          │
                    │  pending_stops_: vector<Order>            │
                    └──────────────────────────────────────────┘

  Each PriceLevel:
    ┌──────────────────────────────────┐
    │  price: 10050                    │
    │  total_volume: 350              │
    │  queue: [id=1] [id=4] [id=7]    │  ← FIFO order (front = highest priority)
    │  cancelled_: {id=2}             │  ← lazy deletion set
    └──────────────────────────────────┘
```

## Order types supported

| Type | Description | Rests on book? |
|------|-------------|----------------|
| Limit | Buy/sell at a specific price or better | Yes (unfilled remainder) |
| Market | Buy/sell at best available price immediately | No (fills or rejects) |
| Stop | Triggers as market order when stop price is reached | No (pending until triggered) |
| Stop-Limit | Triggers as limit order when stop price is reached | Yes (after trigger, if unfilled) |
| Iceberg | Limit order with hidden reserve; only peak visible | Yes (replenishes from reserve) |

## Matching rules

1. **Price priority**: Orders at better prices match first (highest bid, lowest ask).
2. **Time priority (FIFO)**: Within a price level, the earliest-arriving order matches first.
3. **Aggressive matching**: New limit orders are matched immediately against resting orders before they rest.
4. **Priority loss**: Orders lose time priority on quantity increase, price change, or iceberg replenishment. Quantity reduction preserves priority.

## Order lifecycle

```
NEW ──> Validate ──> Match against opposite side
                         │
                    ┌────┴────┐
                    │         │
                Fully      Partial
                Filled      Fill
                  │          │
                FILLED    RESTING (remainder on book)
                             │
                    ┌────────┼────────┐
                    │        │        │
                 CANCEL   MODIFY   REPLACE
                    │        │        │
                CANCELLED  (may     (old removed,
                           lose      new submitted)
                          priority)
```

## Benchmark results

Measured on Apple Silicon (ARM64), AppleClang 17.0.0, `-O2 -march=native`, single-threaded.

### Before optimisation (O(n) deque erase on cancel)

| Scenario | Throughput (ev/s) | p50 (ns) | p95 (ns) | p99 (ns) | max (ns) |
|---|---|---|---|---|---|
| small | 1,647,656 | 0 | 1,000 | 11,000 | 126,994,000 |
| medium | 2,156,081 | 0 | 1,000 | 7,000 | 138,194,000 |
| large | 2,202,671 | 0 | 1,000 | 7,000 | 136,395,000 |
| high_cancel | 9,434,222 | 0 | 1,000 | 1,000 | 147,000 |
| market_heavy | 2,436,656 | 0 | 1,000 | 6,000 | 225,051,000 |
| iceberg_stop | 2,111,828 | 0 | 1,000 | 8,000 | 127,396,000 |

### After optimisation (lazy deletion with unordered_set)

| Scenario | Throughput (ev/s) | p50 (ns) | p95 (ns) | p99 (ns) | max (ns) |
|---|---|---|---|---|---|
| small | 4,021,862 | 0 | 1,000 | 1,000 | 263,296,000 |
| medium | 3,274,301 | 0 | 1,000 | 1,000 | 136,226,000 |
| large | 3,326,316 | 0 | 1,000 | 1,000 | 138,841,000 |
| high_cancel | 7,466,016 | 0 | 1,000 | 1,000 | 5,846,000 |
| market_heavy | 3,861,558 | 0 | 1,000 | 1,000 | 230,949,000 |
| iceberg_stop | 3,452,091 | 0 | 1,000 | 1,000 | 258,886,000 |

### Improvement

| Scenario | Before (ev/s) | After (ev/s) | Change |
|---|---|---|---|
| small | 1,647,656 | 4,021,862 | **+144%** |
| medium | 2,156,081 | 3,274,301 | **+52%** |
| large | 2,202,671 | 3,326,316 | **+51%** |
| high_cancel | 9,434,222 | 7,466,016 | -21% |
| market_heavy | 2,436,656 | 3,861,558 | **+58%** |
| iceberg_stop | 2,111,828 | 3,452,091 | **+63%** |

See [docs/performance.md](docs/performance.md) for full profiling analysis and methodology.

## Optimisation notes

Profiling with macOS `sample` revealed that `PriceLevel::remove()` consumed 24% of CPU time due to O(n) `std::deque::erase()` calling `memmove` on every cancel. The fix: a lazy deletion pattern using `std::unordered_set<OrderId>` — cancelled IDs are marked in a hash set and lazily drained from the deque front during matching. This reduced cancel cost from O(n) to O(1) and improved throughput by 51–144% on matching-heavy workloads.

## Replay and snapshot

- **Replay tool** (`lob_replay`): Reads a CSV event sequence, feeds it through the engine, and optionally compares output against expected results. Used for deterministic regression testing.
- **Binary snapshot**: Serialises the full order book state (all orders, levels, pending stops, sequence number) to a binary file. Can be loaded to resume processing from a checkpoint.

## Building

### Prerequisites

- C++20 compiler (GCC 11+, Clang 14+, or AppleClang 15+)
- CMake 3.20+
- Google Test (installed via package manager)

### macOS

```bash
brew install cmake googletest
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Linux (Ubuntu/Debian)

```bash
sudo apt-get install cmake libgtest-dev g++
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
```

## Running tests

```bash
cd build && ctest --output-on-failure
```

All 77+ tests should pass in under 1 second.

### Replay tests

```bash
./build/tools/lob_replay \
  --events tests/replay/data/basic_limit.csv \
  --expected tests/replay/data/basic_limit_expected.csv
```

### Sanitizer build

```bash
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g" \
      -DCMAKE_CXX_COMPILER=clang++
cmake --build build_asan
./build_asan/tests/lob_tests
```

## Running benchmarks

```bash
cmake -B build_bench -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O2 -march=native"
cmake --build build_bench
./build_bench/benchmarks/lob_bench --all --duration 10
```

Or run a single scenario:

```bash
./build_bench/benchmarks/lob_bench --scenario high_cancel --duration 10
```

## Project structure

```
src/
  core/           Order, Trade, Event types, enums, config, SPSC queue
  book/           PriceLevel (FIFO queue + lazy deletion), OrderBook
  engine/         MatchingEngine (limit, market, stop, iceberg)
  feed/           CSV replay reader, synthetic order generator
  logging/        Async JSON-lines logger (lock-free SPSC)
  metrics/        Latency histogram, counters
  snapshot/       Binary serialisation/deserialisation

tests/
  unit/           77+ Google Test cases
  replay/data/    Deterministic replay fixtures (CSV in → CSV expected)

benchmarks/       Benchmark harness with 6 scenarios

tools/            lob_replay CLI tool

docs/             Engine design, data structures, performance, testing
```

## Future work

Documented in [docs/performance.md](docs/performance.md):

1. **`unordered_map::reserve`** for order ID map — eliminate rehash overhead
2. **Flat sorted vector** for shallow books (<15 levels) — better cache locality
3. **Priority queue for stop orders** — O(log n) trigger evaluation instead of O(n)
4. **FIX protocol adapter** — accept orders via standard financial messaging
5. **WebSocket market data feed** — real-time BBO and trade streaming

## License

MIT — see [LICENSE](LICENSE).