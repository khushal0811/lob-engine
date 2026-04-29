# Build plan — 6-phase coding roadmap

Each phase is independently shippable and buildable. Tests are written alongside the code they cover, not at the end. Every phase ends with a working, compilable, passing-test state.

---

## Phase 1 — Core models, book structure, and basic limit matching

**Goal:** A working order book that accepts limit orders, matches them, and produces trade records. Nothing fancy, but 100% correct.

### Deliverables

- Full `src/core/` module
  - `Order`, `Trade`, `ExecutionReport`, `Rejection` structs
  - `Side`, `OrderType`, `OrderStatus`, `RejectReason`, `EventType` enums
  - `OrderEvent` as `std::variant`
  - `EngineConfig` with sensible defaults
- Full `src/book/` module
  - `PriceLevel` with FIFO deque
  - `OrderBook` with bid map, ask map, order ID map
  - Best bid / ask cache
  - Mid-price, spread, last trade price
- `src/engine/matching_engine` — limit order matching only (buy and sell)
  - Partial fills
  - Full fills
  - Multi-level sweep for aggressive limit orders
  - Residual resting after partial match
- `src/risk/validator` — basic checks only (price > 0, qty > 0, dupe ID)
- CMakeLists.txt with `lob_core` static library target and `lob_tests` target
- `.clang-format` and `.clang-tidy` configs committed
- Unit tests for: price level FIFO, book insertion, limit-limit matching, partial fills, crossed-book prevention

### Done when

- All unit tests pass under GCC and Clang
- No warnings with `-Wall -Wextra -Werror`
- clang-format produces no diff
- A hand-written event sequence in a test produces the correct trade records

---

## Phase 2 — Market orders, cancel, modify, replace

**Goal:** Full CRUD on orders. The book can now handle a realistic order lifecycle.

### Deliverables

- Market order matching
  - Full multi-level sweep
  - Residual cancellation with `MARKET_EXHAUSTED` reject record when book is exhausted
- Cancel order handling
  - Validation (order must exist, must be in resting state)
  - Removal from price level queue and ID map
  - Level cleanup if queue becomes empty
- Modify order handling
  - Quantity reduction: in-place update, preserves time priority
  - Quantity increase or price change: cancel and reinsert (loses time priority — this is correct)
- Replace order: atomic cancel + new order as a single event
- Expanded validator for cancel / modify / replace edge cases
- Unit tests for: market sweep, book exhaustion, cancel, modify (both paths), replace, validator rejection cases

### Done when

- All phase 1 tests still pass
- Market order tests cover: partial sweep, full sweep, exhaustion
- Cancel tests cover: valid cancel, cancel of non-existent ID, cancel of already-filled order
- Modify tests cover: qty reduction (keeps priority), price change (loses priority)

---

## Phase 3 — Advanced order types: stop and iceberg

**Goal:** The two most portfolio-impressive order types. These demonstrate understanding of real exchange mechanics.

### Deliverables

- Stop order support
  - Pending stop list (`std::vector<Order>`)
  - Trigger evaluation after every trade — checks last trade price against all pending stops
  - Buy stop: triggers when last trade price >= stop price → converts to market order
  - Sell stop: triggers when last trade price <= stop price → converts to market order
  - Stop-limit variant: converts to limit order at the stop's limit price instead of market
- Iceberg order support
  - Peak quantity and reserve quantity tracked on `Order` struct
  - Only peak quantity is visible on the book
  - On peak exhaustion: reserve replenishes peak, order reinserted at back of queue (time priority reset)
  - If reserve also exhausted: order removed normally
- Unit tests for: stop trigger (buy and sell), stop trigger at boundary price, stop-limit conversion, iceberg partial fill and replenishment, iceberg full exhaustion, multiple icebergs at same level

### Done when

- Stop orders trigger exactly at and past the trigger price, not before
- Stop-limit correctly rests as a limit order after triggering (does not immediately sweep)
- Iceberg peak quantity on book snapshot equals configured peak size (not total)
- Replay of a known stop-trigger sequence produces identical output on two runs

---

## Phase 4 — Snapshot, replay engine, and async infrastructure

**Goal:** Deterministic replay and async output subsystems. This phase is what makes the project feel like a real system.

### Deliverables

- `src/snapshot/` — snapshot manager
  - Binary serialisation of full book state (all resting orders, stop list, sequence number, last trade price)
  - Deserialise and restore book state
  - Snapshot interval configurable via `EngineConfig`
- Replay engine
  - Load snapshot → restore state
  - Feed subsequent events from CSV starting at snapshot sequence number
  - Compare output against expected output file (byte-level diff in CI)
- `src/feed/csv_replay` — CSV event log reader
  - Parses event log format defined in architecture doc
  - Emits `MalformedEvent` records for unparseable lines rather than aborting
- `src/feed/synthetic_gen` — synthetic order generator
  - All configurable parameters: arrival rate, price clustering, cancel probability, burst mode, volatility spike, market-maker mode
- `src/logging/` — async structured logger
  - Lock-free queue between matching core and background writer thread
  - JSON-lines output format
  - Drop-on-full with drop counter
- `src/metrics/` — metrics and microstructure
  - Latency histogram (p50, p95, p99, max)
  - Throughput counter
  - Spread, imbalance, top-N depth after every event
- SPSC ring buffer between feed producer thread and matching core
- Replay tests: two sample CSV files with expected output committed to `tests/replay/data/`

### Done when

- Snapshot + restore round-trip produces identical book state
- Replay of sample CSV matches expected output file exactly
- Async logger does not block matching core under stress (verified by latency comparison)
- Synthetic generator produces valid event streams accepted by the validator

---

## Phase 5 — Benchmark suite, profiling, and optimisation

**Goal:** The performance story. This is the phase that turns the project from "good" to "impressive."

### Deliverables

- `benchmarks/` — full benchmark suite
  - One binary, scenario selected by flag
  - Scenarios: small market, medium market, large market, high-cancel, market-order-heavy, iceberg/stop mix
  - Each scenario reports: throughput (orders/sec), latency histogram
  - Reproducible via fixed seed
- **Baseline benchmark run** — run all scenarios, record results in `docs/performance.md`
- **Profiling pass** — use `perf` (Linux) or `gprof` to identify the top two or three hot paths
- **Targeted optimisation** — apply one or two specific improvements based on profiling. Candidates:
  - Reduce heap allocations in the level queue (e.g. small-buffer optimisation or pre-allocated pool)
  - Replace `std::map` with `std::flat_map` or a sorted `std::vector` at shallow book depths
  - Improve cache locality in the order ID map (open-addressing hash map)
  - Reduce branch misprediction in the sweep loop
- **Post-optimisation benchmark run** — run all scenarios again, record results
- `docs/performance.md` — write-up with: methodology, hot path finding, change applied, before/after numbers, percentage improvement
- Stress tests under ASan + UBSan — run high-volume and cancellation-storm scenarios through sanitizer build, no errors

### Done when

- All six benchmark scenarios run without errors on both GCC and Clang builds
- At least one documented optimisation with a measurable throughput or latency improvement
- `docs/performance.md` contains before/after tables and a clear narrative
- Stress tests under ASan + UBSan are clean

---

## Phase 6 — CI/CD, full test coverage, and documentation polish

**Goal:** The repo looks maintained, not just built. This phase is what you put in front of a hiring manager.

### Deliverables

- GitHub Actions pipeline
  - Job: build + test with GCC (release and sanitizer configurations)
  - Job: build + test with Clang (release and sanitizer configurations)
  - Job: clang-format check (fails if diff is non-empty)
  - Job: clang-tidy static analysis
  - Job: replay test validation (diff expected vs actual output)
  - Job: benchmark (manual trigger + nightly schedule)
  - Job: release artifact build on tag push
- Complete `README.md`
  - Architecture diagram (ASCII or linked image)
  - Matching rules and order lifecycle summary
  - Supported order types table
  - Benchmark results table (copy from `docs/performance.md`)
  - Optimisation notes
  - Build instructions (CMake, dependencies, how to run tests and benchmarks)
  - Replay and snapshot explanation
  - Future work section
- `docs/` folder complete
  - `engine-design.md` — matching algorithm, stop trigger, iceberg
  - `data-structures.md` — container choices and rationale
  - `performance.md` — benchmark methodology and results (from phase 5)
  - `testing.md` — test strategy and invariant definitions
- `data/sample-replay/` — at least two sample CSV event logs with expected output files
- Final invariant test pass — run invariant checker against 100,000 synthetic events, no violations
- All tests passing on both compilers in CI

### Done when

- Every CI job is green on main
- `README.md` renders cleanly on GitHub with no broken links
- `docs/` folder contains all four documents
- A fresh clone, `cmake --build`, `ctest` produces zero failures

---

## Summary

| Phase | Core output | Key signal |
|---|---|---|
| 1 | Limit order book + matching | Correctness, data structures |
| 2 | Full order lifecycle (cancel, modify, market) | CRUD, edge cases |
| 3 | Stop and iceberg orders | Exchange mechanics depth |
| 4 | Snapshot, replay, async infra | Systems maturity, determinism |
| 5 | Benchmarks, profiling, optimisation | Performance engineering story |
| 6 | CI/CD, docs, polish | Maintainability, presentation |

Each phase builds directly on the previous. Do not start phase 4 until phase 3 tests are clean — the replay engine depends on correct order handling for its expected output files.
