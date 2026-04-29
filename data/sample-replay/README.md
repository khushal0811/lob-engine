# Sample replay data

This directory contains sample CSV event files that can be fed through the matching engine using the `lob_replay` tool. Each scenario demonstrates specific engine capabilities.

## CSV format

### Event CSV (input)

```
event_type,order_id,side,order_type,price,quantity,peak_qty,stop_price,timestamp
```

| Column | Type | Description |
|--------|------|-------------|
| `event_type` | string | `NEW`, `CANCEL`, `MODIFY`, or `REPLACE` |
| `order_id` | uint64 | Unique order identifier |
| `side` | string | `BUY` or `SELL` |
| `order_type` | string | `LIMIT`, `MARKET`, `STOP`, `STOPLIMIT`, or `ICEBERG` |
| `price` | int64 | Limit price in ticks (0 for market orders) |
| `quantity` | uint64 | Order quantity |
| `peak_qty` | uint64 | Visible peak for iceberg orders (0 if not iceberg) |
| `stop_price` | int64 | Trigger price for stop orders (0 if not a stop) |
| `timestamp` | uint64 | Nanoseconds since epoch |

For `CANCEL` events, only `event_type`, `order_id`, and `timestamp` are required. Other fields are ignored.

### Expected CSV (output)

```
aggressor_id,passive_id,price,quantity
```

| Column | Type | Description |
|--------|------|-------------|
| `aggressor_id` | uint64 | The incoming (active) order ID |
| `passive_id` | uint64 | The resting (passive) order ID |
| `price` | int64 | Fill price (always the passive order's price) |
| `quantity` | uint64 | Fill quantity |

## Scenarios

### `small_market.csv` (30 events)

A compact scenario demonstrating basic limit order book behaviour: order placement, price crosses, partial fills, cancellations, and market sweeps. Good for understanding the matching algorithm step by step.

### `stop_scenario.csv` (25 events)

Demonstrates stop and stop-limit order lifecycle: pending placement, trade-triggered activation, and conversion to market or limit orders. Includes both buy and sell stops.

### `iceberg_scenario.csv` (20 events)

Shows iceberg order behaviour: only the peak quantity is visible, the peak is replenished from the hidden reserve after each fill, and time priority resets on each replenishment.

## Running

```bash
# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run a scenario and see trades
./build/tools/lob_replay --events data/sample-replay/small_market.csv

# Validate against expected output
./build/tools/lob_replay \
  --events data/sample-replay/small_market.csv \
  --expected data/sample-replay/small_market_expected.csv
```
