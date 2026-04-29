# Testing strategy — lob-engine

This document covers the testing methodology, invariant definitions, sanitizer setup, and how to extend the test suite.

---

## Unit test strategy

The engine uses [Google Test](https://github.com/google/googletest) as the testing framework. Tests are organized by component in `tests/unit/`:

| File | Component | Test count |
|------|-----------|------------|
| `test_price_level.cpp` | `PriceLevel` FIFO queue, volume tracking, lazy deletion | 7 |
| `test_order_book.cpp` | `OrderBook` insert, cancel, best prices, spread | 9 |
| `test_matching_limit.cpp` | Limit order matching: full, partial, sweep, FIFO | 8 |
| `test_matching_market.cpp` | Market order matching: fill, sweep, exhaustion | 7 |
| `test_cancel.cpp` | Cancel logic: success, not-found, not-cancellable | 5 |
| `test_modify.cpp` | Modify: quantity reduction, price change, priority | 5 |
| `test_replace.cpp` | Replace: old removal, new insertion, matching | 4 |
| `test_stop_orders.cpp` | Stop lifecycle: pending, trigger, cancel, immediate | 7 |
| `test_iceberg.cpp` | Iceberg: peak visibility, replenishment, FIFO reset | 6 |
| `test_phase4.cpp` | Infrastructure: SPSC queue, CSV replay, generator, logger, histogram, metrics, snapshot, replay verification | 16 |
| `test_integration.cpp` | 100k event integration with invariant checks | 1 |

**Total: 75+ tests** covering every public API method, every order type, and every edge case identified during development.

### Test design principles

1. **One behaviour per test**: Each test validates exactly one behaviour (e.g., "partial fill leaves remainder on book").
2. **No shared state**: Every test creates its own `MatchingEngine` instance. No test depends on another.
3. **Explicit assertions**: Tests assert on specific trade quantities, prices, and order statuses — not just "something happened".
4. **Edge cases**: Tests cover empty books, zero-quantity scenarios, duplicate IDs, and exhausted liquidity.

---

## Replay test methodology

Replay tests validate the engine's deterministic output against a known-good expected output file.

### How it works

1. A CSV file contains a sequence of order events (new, cancel, modify, replace).
2. The `lob_replay` tool reads the CSV and feeds events through the matching engine.
3. The resulting trades are compared against an expected CSV file.
4. The test passes only if the output matches exactly — bit-for-bit.

### Test data files

Located in `tests/replay/data/`:

| File | Description |
|------|-------------|
| `basic_limit.csv` | 20 limit orders creating crosses and partial fills |
| `basic_limit_expected.csv` | Expected trade output from the basic limit scenario |
| `stop_trigger.csv` | Stop and stop-limit orders with trigger events |
| `stop_trigger_expected.csv` | Expected trades including triggered stop conversions |

### CSV format

**Event CSV** (input):
```
event_type,order_id,side,order_type,price,quantity,peak_qty,stop_price,timestamp
NEW,1,SELL,LIMIT,10050,100,0,0,1700000000000000000
CANCEL,5,,,,,,,1700000000000010000
```

**Expected CSV** (output):
```
aggressor_id,passive_id,price,quantity
7,1,10050,60
```

### Running replay tests

```bash
./build/tools/lob_replay \
  --events tests/replay/data/basic_limit.csv \
  --expected tests/replay/data/basic_limit_expected.csv
```

Exit code 0 means all trades match. Any mismatch prints the first divergence and exits with code 1.

---

## Invariant definitions

The engine maintains six structural invariants that must hold after every event. These are checked in the integration test every 1,000 events.

### 1. Volume consistency

For every price level, the stored `total_volume` must equal the sum of `visible_qty` of all orders in the level's queue.

```
∀ level ∈ bids ∪ asks:
    level.total_volume == Σ order.visible_qty for order in level.order_ids
```

### 2. Bid ordering

Bid prices must be in strictly descending order (highest price first).

```
∀ adjacent prices p1, p2 in bids:
    p1 > p2
```

This is enforced by `std::map<Price, PriceLevel, std::greater<Price>>`.

### 3. Ask ordering

Ask prices must be in strictly ascending order (lowest price first).

```
∀ adjacent prices p1, p2 in asks:
    p1 < p2
```

This is enforced by `std::map<Price, PriceLevel>` (default comparator).

### 4. No crossed book

If both sides of the book are non-empty, the best bid must be strictly less than the best ask.

```
if bids non-empty and asks non-empty:
    best_bid < best_ask
```

A crossed book would mean a trade should have occurred but did not.

### 5. ID map consistency

Every order ID present in any price level queue must exist in the order ID lookup map. No ID may appear in more than one level.

```
∀ id ∈ any level.queue:
    id ∈ id_map
    id appears exactly once across all levels
```

### 6. Stop list exclusivity

No order ID in the pending stops list may appear in the visible order book (bids or asks).

```
∀ stop ∈ pending_stops:
    stop.id ∉ bids ∪ asks
```

---

## Sanitizer setup and how to run locally

### AddressSanitizer + UndefinedBehaviorSanitizer

ASan detects memory errors (buffer overflow, use-after-free, double-free). UBSan detects undefined behaviour (signed overflow, null pointer dereference, misaligned access).

```bash
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g -fno-omit-frame-pointer" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
      -DCMAKE_CXX_COMPILER=clang++
cmake --build build_asan
./build_asan/tests/lob_tests
```

Expected output: all tests pass with zero sanitizer errors.

### Running under CI

The sanitizer build runs automatically in CI on every push to `main` and every pull request. See `.github/workflows/ci.yml`, job `sanitizers`.

### Performance note

Sanitizer-instrumented binaries run 2–10x slower than release builds. The unit tests complete in under 1 second even under sanitizers. Benchmark scenarios should not be run under sanitizers (they will take minutes instead of seconds).

---

## How to add a new replay fixture

1. **Create the event CSV** in `tests/replay/data/`:
   ```
   event_type,order_id,side,order_type,price,quantity,peak_qty,stop_price,timestamp
   NEW,1,BUY,LIMIT,100,50,0,0,1000
   ...
   ```

2. **Generate the expected output** by running the scenario once:
   ```bash
   ./build/tools/lob_replay --events tests/replay/data/your_scenario.csv > output.csv
   ```
   Review the output manually to confirm correctness.

3. **Save the expected output** as `tests/replay/data/your_scenario_expected.csv`:
   ```
   aggressor_id,passive_id,price,quantity
   ...
   ```
   Add the header line at the top.

4. **Add to CI** in `.github/workflows/ci.yml` under the `replay-tests` job:
   ```yaml
   - name: Run replay validation
     run: |
       ./build/tools/lob_replay \
         --events tests/replay/data/your_scenario.csv \
         --expected tests/replay/data/your_scenario_expected.csv
   ```

5. **Verify** by running locally:
   ```bash
   ./build/tools/lob_replay \
     --events tests/replay/data/your_scenario.csv \
     --expected tests/replay/data/your_scenario_expected.csv
   ```
   Exit code 0 confirms the fixture is correct.

---

## How to add a new unit test

1. Create a test file in `tests/unit/` (or add to an existing one).
2. Include the relevant headers and use `TEST(SuiteName, TestName)` macros.
3. Register the file in `tests/CMakeLists.txt`:
   ```cmake
   add_executable(lob_tests
       ...
       unit/your_new_test.cpp
   )
   ```
4. Build and run:
   ```bash
   cmake --build build_release
   cd build_release && ctest --output-on-failure
   ```

Google Test auto-discovers all `TEST()` macros in registered source files.
