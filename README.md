# lob-engine — high-performance C++ limit order book and exchange service

> A production-grade, event-driven exchange service built on a deterministic price-time priority matching engine. Supports 25 instruments, ZeroMQ client communication, lock-free inter-thread messaging, and async NDJSON replay logging — all in C++20.

[![CI](https://github.com/khushal0811/lob-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/khushal0811/lob-engine/actions/workflows/ci.yml)

## What this is

A complete limit order book (LOB), matching engine, **and exchange service** written in C++20. External clients connect over ZeroMQ, submit orders as JSON, and receive standardised market data events in real time. The engine processes order events (new, cancel, modify, replace) and produces trades, execution reports, and rejections. The engine is deterministic, fully benchmarked, and sanitizer-clean.

This is **not** a trading bot or strategy backtester. It is the **exchange-side matching core** — the component that decides which orders trade against each other and at what price — now exposed as a reusable networked service.

## Why limit order books matter

Every electronic exchange — equities, futures, options, crypto — runs a matching engine at its core. Understanding how an LOB works is fundamental to:

- Market microstructure research
- High-frequency trading system design
- Exchange technology development
- Algorithmic trading strategy evaluation

This project implements the same price-time priority algorithm used by the NYSE, NASDAQ, CME, and most modern exchanges.

---

## Exchange Service Architecture

```
                External Clients
         ┌──────────────────────────┐
         │   Any ZMQ PUSH client    │
         │   (Python, C++, Rust…)   │
         └───────────┬──────────────┘
                     │  JSON order message
                     ▼
              tcp://*:5555  ZMQ PULL
                     │
    ┌────────────────┼──────────────────────────────────────────┐
    │ Gateway        │                                          │
    │                ▼                                          │
    │  [T1 — Receiver Thread]                                   │
    │    zmq_recv() → deserialize_order() → inbound SPSC queue  │
    │                │                                          │
    │                ▼                                          │
    │  [T2 — Exchange Thread]  ← DEDICATED, reserved            │
    │    pop queue → ExchangeManager::process(msg)              │
    │      └─ routes to engine_map["STOCK_N"]                   │
    │      └─ MatchingEngine::submit()   (unchanged core)       │
    │      └─ MatchResult → vector<ExchangeEvent>               │
    │    → dispatcher_.enqueue(events)   [T2→T3 SPSC]           │
    │    → replay_.log(events)           [T2→T4 SPSC]           │
    │    every 100ms: snapshot_all() → enqueue 25 snapshots     │
    │                │                                          │
    │       ┌────────┴────────┐                                 │
    │       ▼                 ▼                                 │
    │  [T3 — Publisher]  [T4 — Replay Logger]                   │
    │  serialize → PUB   NDJSON → disk                          │
    │                │                                          │
    └────────────────┼──────────────────────────────────────────┘
                     │  "TOPIC {json_payload}"
                     ▼
              tcp://*:5556  ZMQ PUB
                     │
    ┌────────────────┼──────────────────────┐
    │ SUB subscribers (filter by topic)     │
    │  TRADE / BOOK / SNAPSHOT /            │
    │  ACCEPTED / REJECTED / CANCELED       │
    └───────────────────────────────────────┘
```

### Thread model

| Thread | Role | Constraints |
|--------|------|-------------|
| **T1** Receiver | `zmq_recv()` → JSON parse → inbound SPSC queue | Never touches engine state |
| **T2** Exchange | Pops orders, runs all 25 engines, enqueues events | **Sole owner** of all `MatchingEngine` instances — no locks needed |
| **T3** Publisher | Pops events, JSON serializes, `zmq_send()` | Never touches engine state |
| **T4** Replay Logger | Pops pre-serialized strings, writes NDJSON to disk | Pure I/O — no business logic |

All inter-thread communication uses lock-free SPSC ring buffers. If a buffer is full, the event is dropped and an atomic counter is incremented. **T2 never waits.**

### Instruments

25 synthetic instruments: `STOCK_1` through `STOCK_25`. Each has its own independent `MatchingEngine` instance managed by `ExchangeManager`. All 25 are processed sequentially on T2 — zero lock contention.

---

## Matching engine architecture

```
                    ┌──────────────────────────────────────────┐
                    │            MatchingEngine                │
                    │                                          │
  OrderEvent ──────>│  submit()                                │
  (variant)         │    ├── process_new_order()               │
                    │    │     ├── match_limit()               │
                    │    │     ├── match_market()              │
                    │    │     ├── evaluate_stop_triggers()    │
                    │    │     └── replenish_iceberg()         │
                    │    ├── process_cancel()                  │──────> MatchResult
                    │    ├── process_modify()                  │        (trades,
                    │    └── process_replace()                 │         reports,
                    │                                          │         rejections)
                    │  ┌─────────────────────────────────┐     │
                    │  │          OrderBook               │    │
                    │  │  bids_: map<Price, PriceLevel>   │    │
                    │  │         (descending)             │    │
                    │  │  asks_: map<Price, PriceLevel>   │    │
                    │  │         (ascending)              │    │
                    │  │  id_map_: unordered_map<Id,Order>│    │
                    │  └─────────────────────────────────┘     │
                    │  pending_stops_: vector<Order>           │
                    └──────────────────────────────────────────┘
```

---

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

---

## Building

### Prerequisites

- C++20 compiler (GCC 11+, Clang 14+, or AppleClang 15+)
- CMake 3.20+
- pkg-config
- Google Test
- **ZeroMQ** (for the exchange service)
- **nlohmann/json** (for JSON serialization)

### macOS

```bash
brew install cmake googletest zeromq nlohmann-json
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release -j$(sysctl -n hw.logicalcpu)
```

### Linux (Ubuntu/Debian)

```bash
sudo apt-get install cmake pkg-config libgtest-dev g++ libzmq3-dev nlohmann-json3-dev
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build_release -j$(nproc)
```

---

## Running the exchange service

```bash
# Start with defaults (PULL: 5555, PUB: 5556, snapshot every 100ms)
./build_release/src/lob_exchange

# Custom configuration
./build_release/src/lob_exchange \
  --pull tcp://*:5555 \
  --pub  tcp://*:5556 \
  --snapshot-interval 100 \
  --log-path logs/replay.ndjson
```

### Sending orders (Python example)

```python
import zmq, json

ctx = zmq.Context()

# Send orders
push = ctx.socket(zmq.PUSH)
push.connect("tcp://localhost:5555")

push.send_string(json.dumps({
    "order_id": 1, "client_id": 42, "symbol": "STOCK_7",
    "action": "NEW_ORDER", "side": "BUY", "type": "LIMIT",
    "quantity": 100, "price": 10050,
    "stop_price": 0, "peak_qty": 0, "timestamp": 0
}))

# Receive events
sub = ctx.socket(zmq.SUB)
sub.connect("tcp://localhost:5556")
sub.setsockopt(zmq.SUBSCRIBE, b"")        # all topics
# sub.setsockopt(zmq.SUBSCRIBE, b"TRADE") # or filter by topic

while True:
    print(sub.recv_string())
```

### ZMQ topic reference

| Topic | Event | When emitted |
|-------|-------|--------------|
| `ACCEPTED` | Order placed on book | Every valid new order |
| `REJECTED` | Order failed validation | Invalid price, duplicate ID, etc. |
| `CANCELED` | Order removed from book | Successful cancel request |
| `TRADE` | Fill between two orders | Every match |
| `BOOK` | Best bid/ask update | After every order action |
| `SNAPSHOT` | Full 25-symbol depth | Every `snapshot-interval` ms |

### JSON wire format

**Inbound order:**
```json
{
  "order_id": 1, "client_id": 42, "symbol": "STOCK_7",
  "action": "NEW_ORDER",
  "side": "BUY", "type": "LIMIT",
  "quantity": 100, "price": 10050,
  "stop_price": 0, "peak_qty": 0, "timestamp": 0
}
```

**Outbound trade event:**
```
TRADE {"ts":1715200000000000000,"type":"TRADE","symbol":"STOCK_7","buy_id":1,"sell_id":2,"price":10050,"qty":100}
```

**Replay log** (`logs/replay.ndjson`) — one JSON object per line, append-only.

---

## Running tests

```bash
cd build_release && ctest --output-on-failure
```

All 78 tests pass in under 1 second.

### Running sample scenarios

```bash
# Run the small market scenario
./build_release/tools/lob_replay --events data/sample-replay/small_market.csv

# Run and validate against expected output
./build_release/tools/lob_replay \
  --events data/sample-replay/iceberg_scenario.csv \
  --expected data/sample-replay/iceberg_scenario_expected.csv
```

See [data/sample-replay/README.md](data/sample-replay/README.md) for the CSV format and available scenarios.

### Sanitizer build

```bash
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g" \
      -DCMAKE_CXX_COMPILER=clang++
cmake --build build_asan
./build_asan/tests/lob_tests
```

---

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

---

## Benchmark results

Measured on Apple Silicon (ARM64), AppleClang 17.0.0, `-O2 -march=native`, single-threaded engine core.

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

Exchange service overhead: **zero** — gateway threads are fully isolated from the matching path. Engine throughput is unchanged at 3.4M ev/s (medium scenario).

See [docs/performance.md](docs/performance.md) for full profiling analysis and methodology.

---

## Optimisation notes

Profiling with macOS `sample` revealed that `PriceLevel::remove()` consumed 24% of CPU time due to O(n) `std::deque::erase()` calling `memmove` on every cancel. The fix: a lazy deletion pattern using `std::unordered_set<OrderId>` — cancelled IDs are marked in a hash set and lazily drained from the deque front during matching. This reduced cancel cost from O(n) to O(1) and improved throughput by 51–144% on matching-heavy workloads.

---

## Project structure

```
src/
  core/             Order, Trade, Event types, enums, config, SPSC queue
  book/             PriceLevel (FIFO + lazy deletion), OrderBook
  engine/           MatchingEngine (limit, market, stop, iceberg)
  feed/             CSV replay reader, synthetic order generator
  logging/          Async JSON-lines logger (lock-free SPSC)
  metrics/          Latency histogram, counters
  snapshot/         Binary serialisation/deserialisation

  events/           External wire-format types (OrderMessage, ExchangeEvent)
  serialization/    JSON ↔ event conversion (nlohmann/json, publisher thread only)
  gateway/          ExchangeManager (25 engines), EventDispatcher (T3),
                    ReplayLogger (T4), Gateway (4-thread coordinator)
  exchange_main.cpp lob_exchange binary entry point

tests/
  unit/             78 Google Test cases

benchmarks/         Benchmark harness with 6 scenarios

tools/              lob_replay CLI tool

docs/               Engine design, data structures, performance, testing
```

---

## Replay and snapshot

- **Replay logger** (`logs/replay.ndjson`): Every exchange event is written to an append-only NDJSON file by the dedicated T4 writer thread. Format: one JSON object per line.
- **Replay tool** (`lob_replay`): Reads a CSV event sequence, feeds it through the engine, and optionally compares output against expected results. Used for deterministic regression testing.
- **Binary snapshot**: Serialises the full order book state (all orders, levels, pending stops, sequence number) to a binary file. Can be loaded to resume processing from a checkpoint.

---

## Future work

1. **`unordered_map::reserve`** for order ID map — eliminate rehash overhead
2. **Flat sorted vector** for shallow books (<15 levels) — better cache locality
3. **Priority queue for stop orders** — O(log n) trigger evaluation instead of O(n)
4. **FIX protocol adapter** — accept orders via standard financial messaging alongside ZMQ
5. **Engine sharding** — partition the 25 instruments across multiple CPU cores, each with its own dedicated exchange thread

---

## Visual interface — lob-ui

A companion desktop application ([lob-ui](https://github.com/khushal0811/lob-ui)) wraps this library with a real-time Qt6 UI. It visualises the live order book, trade tape, and metrics — and lets you submit orders and watch the engine process them in real time.

The UI runs the engine on a background thread, communicating exclusively through typed Qt signals. The engine code is unchanged — `lob-ui` links against the pre-built static library (`liblob_core.a`) produced by this repo.

---

## License

MIT — see [LICENSE](LICENSE).
