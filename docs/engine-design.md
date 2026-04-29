# Engine design — lob-engine

## Matching algorithm

The matching engine implements a **price-time priority** (FIFO) matching algorithm, the standard model used by most equity, futures, and options exchanges.

### Limit order matching

When a new limit order arrives, it is tested against the opposite side of the book:

```
function match_limit(aggressor):
    while aggressor.remaining_qty > 0:
        passive_level = opposite_best_level(aggressor.side)

        if passive_level is null:
            break  // no liquidity

        if aggressor is buy and passive_level.price > aggressor.price:
            break  // price cross gone

        if aggressor is sell and passive_level.price < aggressor.price:
            break  // price cross gone

        passive_id = passive_level.front()
        passive = lookup_order(passive_id)

        fill_qty = min(aggressor.remaining_qty, passive.remaining_qty)
        execute_fill(aggressor, passive, fill_qty)

    if aggressor.remaining_qty > 0:
        insert_order(aggressor)  // unfilled remainder rests on book
```

Key properties:

- **Price priority**: the aggressor matches against the best-priced resting level first. For buy aggressors, this is the lowest ask; for sell aggressors, the highest bid.
- **Time priority**: within a price level, orders are matched in FIFO order — the order that arrived first is filled first.
- **Immediate matching**: a new limit order is matched immediately upon arrival before it rests. This is aggressive-to-passive matching, consistent with exchange behaviour.

### Market order matching

Market orders follow the same algorithm as limit orders but with no price constraint:

```
function match_market(aggressor):
    while aggressor.remaining_qty > 0:
        passive_level = opposite_best_level(aggressor.side)

        if passive_level is null:
            reject(aggressor, MarketExhausted)
            break

        passive_id = passive_level.front()
        passive = lookup_order(passive_id)

        fill_qty = min(aggressor.remaining_qty, passive.remaining_qty)
        execute_fill(aggressor, passive, fill_qty)
```

Market orders sweep through all available levels until completely filled or the book is exhausted. They never rest on the book.

---

## Stop order lifecycle

A stop order is a conditional order that activates when a specified trigger price is reached. The engine supports two stop types:

| Type | Behaviour on trigger |
|------|---------------------|
| `Stop` | Converts to a **market order** — immediate execution at best available price |
| `StopLimit` | Converts to a **limit order** at the original limit price — may rest if no cross |

### Submission

When a stop order is submitted, the engine first checks whether the trigger condition is already satisfied by the last trade price:

- **Buy stop**: triggers if `last_trade_price >= stop_price`
- **Sell stop**: triggers if `last_trade_price <= stop_price`

If immediately triggered, the order is converted and processed as a new order. Otherwise, it is placed in the `pending_stops_` list, which is not part of the visible order book.

### Trigger evaluation

After every trade execution, the engine evaluates all pending stops against the new last trade price. To prevent re-entrant infinite loops (a triggered stop produces a trade, which triggers more stops), the engine uses a **swap guard**:

```
function evaluate_stop_triggers():
    swap(working, pending_stops_)  // pending_stops_ is now empty

    for stop in working:
        if triggered(stop, last_trade_price):
            process_triggered_stop(stop)  // may add new stops to pending_stops_
        else:
            pending_stops_.push_back(stop)  // re-queue for later
```

The swap ensures that during processing, any newly triggered stop's sub-fills see an empty pending list, preventing the same stop from being re-evaluated recursively.

### Cancellation

A pending stop can be cancelled by its order ID. The engine scans `pending_stops_` for the matching ID and removes it. Stop orders that have already triggered cannot be cancelled.

---

## Iceberg replenishment logic

Iceberg orders (also called reserve orders) show only a portion of their total quantity on the book. When the visible peak is fully consumed, a new peak is replenished from the hidden reserve.

### Step-by-step process

1. **Submission**: An iceberg order with `quantity=1000` and `peak_qty=200` arrives.
   - `reserve_qty = quantity - peak_qty = 800`
   - `quantity = peak_qty = 200` (only the peak is visible)
   - The order is placed on the book with 200 visible.

2. **Peak exhausted**: An aggressor fills the entire visible peak (200 units).
   - The order's quantity reaches zero.
   - The engine detects `is_iceberg() && reserve_qty > 0`.

3. **Replenishment**: The engine replenishes:
   - `new_peak = min(peak_qty, reserve_qty) = min(200, 800) = 200`
   - `reserve_qty = 800 - 200 = 600`
   - `quantity = 200` (reset to new peak)
   - The order is **popped from the front** and **pushed to the back** of the price level — this resets its time priority.

4. **Repeat**: Steps 2–3 repeat until `reserve_qty` is exhausted.

5. **Final peak**: When `reserve_qty < peak_qty`, the last peak is smaller:
   - `new_peak = min(200, 150) = 150` (using remaining reserve)

6. **Full exhaustion**: When `reserve_qty = 0` and the last peak is filled, the order is fully filled and removed from the book.

### Key property: time priority reset

Every replenishment pushes the order to the back of its price level queue. This means other orders at the same price that arrived after the iceberg but have not been replenished will be matched first. This is consistent with how iceberg orders behave on real exchanges.

---

## Price-time priority explanation

The lob-engine implements strict **price-time priority** (also called **FIFO** or **first-in-first-out** priority):

1. **Price priority**: Orders offering a better price are matched first.
   - For buyers: higher price = better (more willing to pay)
   - For sellers: lower price = better (more willing to accept)

2. **Time priority**: Among orders at the same price, the order that arrived earliest is matched first. Arrival order is determined by position in the `std::deque<OrderId>` queue within each `PriceLevel`.

3. **Priority loss**: An order loses its time priority and goes to the back of the queue when:
   - Its quantity is **increased** (modify with larger quantity)
   - Its **price is changed** (modify or replace)
   - It is an iceberg order and its peak is **replenished** from reserve

   Quantity reductions do **not** lose time priority.

---

## Determinism guarantee

The matching engine is deterministic: given the same sequence of input events, it will always produce the same sequence of trades and execution reports.

This is guaranteed by the single-threaded core design:

- All matching logic runs on a single thread with no concurrency.
- The synthetic generator uses a seeded PRNG (`std::mt19937_64` with seed 42), producing identical event sequences across runs.
- Data structures (`std::map`, `std::deque`, `std::unordered_map`) iterate in consistent order.
- No system calls, I/O, or timers are in the matching hot path.

The only non-deterministic component is the async logger, which runs on a separate thread. But it is output-only — it reads from a lock-free queue and has no effect on matching decisions.

---

## Sequence number and replay correctness

Every call to `MatchingEngine::submit()` increments a monotonic sequence counter. This counter:

1. Provides a total ordering of all events processed by the engine.
2. Is used by the snapshot system to record the exact event position, allowing replay to resume from a known state.
3. Guarantees that replay from a snapshot + remaining events produces the same output as replay from scratch.

The replay tool (`lob_replay`) demonstrates this: it reads events from a CSV file, feeds them through the engine, and compares the resulting trade stream against an expected output file. Bit-exact match is required for the test to pass.
