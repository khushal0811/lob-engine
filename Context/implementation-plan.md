# Master implementation plan
# High-performance C++ limit order book and matching engine

---

## How to use this document

This document is the authoritative implementation guide for every coding session. Each phase is broken into numbered parts. Each part has a precise goal, exact file list, implementation details, code patterns, and acceptance criteria. After every phase there is a checklist that must be fully cleared before the next phase begins.

Work through parts in order within each phase. Do not skip ahead. Do not start a new phase until the checklist is complete.

The matching core is single-threaded and deterministic. If you are ever unsure whether a design decision is correct, prefer the simpler, more deterministic option.

---

## Repository setup (do this before phase 1)

```
mkdir lob && cd lob
git init
git remote add origin git@github.com:<you>/lob.git
```

Create the following files in the first commit:

**.gitignore**
```
build/
.cache/
*.o
*.a
*.so
compile_commands.json
```

**.clang-format**
```yaml
BasedOnStyle: LLVM
ColumnLimit: 100
PointerAlignment: Left
IndentWidth: 4
AccessModifierOffset: -4
SortIncludes: true
```

**.clang-tidy**
```yaml
Checks: >
  modernize-*,
  performance-*,
  readability-*,
  bugprone-*,
  cppcoreguidelines-*,
  -cppcoreguidelines-avoid-magic-numbers,
  -readability-magic-numbers,
  -modernize-use-trailing-return-type
WarningsAsErrors: ""
HeaderFilterRegex: "src/.*"
```

**CMakeLists.txt** (skeleton, will be expanded each phase)
```cmake
cmake_minimum_required(VERSION 3.20)
project(lob CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
```

**README.md** (one-line stub, will be filled in phase 6)
```markdown
# lob — high-performance C++ limit order book
Work in progress.
```

First commit message: `chore: initial repo scaffold, clang-format, clang-tidy, cmake skeleton`

---

---

# Phase 1 — Core models, book structure, and limit order matching

**Goal:** A compilable, tested limit order book that accepts limit orders on both sides, matches them with correct price-time priority, handles partial and full fills, and produces structured trade records. Nothing else yet.

**Commit strategy:** One commit per part. Each commit must compile and all existing tests must pass.

---

## Part 1.1 — Project structure and CMake foundation

### Goal
Create the full directory tree and a working CMake build that compiles an empty library and an empty test binary.

### Actions

Create all directories:
```
mkdir -p src/core src/engine src/book src/feed src/risk src/logging src/metrics src/snapshot
mkdir -p tests/unit tests/replay tests/stress tests/invariants
mkdir -p benchmarks tools scripts docs data/sample-replay
```

Create placeholder files so CMake sees non-empty targets:
- `src/core/core.cpp` — empty translation unit for now
- `tests/unit/main_test.cpp` — GoogleTest main

**tests/unit/main_test.cpp:**
```cpp
#include <gtest/gtest.h>
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
```

**CMakeLists.txt** (full version for phase 1):
```cmake
cmake_minimum_required(VERSION 3.20)
project(lob CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Dependencies
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

# Core library
add_library(lob_core STATIC src/core/core.cpp)
target_include_directories(lob_core PUBLIC src/)

# Tests
add_executable(lob_tests tests/unit/main_test.cpp)
target_link_libraries(lob_tests lob_core GTest::gtest GTest::gtest_main)
enable_testing()
add_test(NAME all_tests COMMAND lob_tests)
```

### Verify
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest
```
Must produce zero errors and zero test failures (zero tests run is fine at this point).

### Commit
`build: cmake foundation with googletest, directory structure`

---

## Part 1.2 — Enums

### Goal
Define all domain enumerations that the rest of the codebase depends on.

### File: `src/core/enums.hpp`

```cpp
#pragma once
#include <cstdint>

namespace lob {

enum class Side : uint8_t {
    Buy,
    Sell
};

enum class OrderType : uint8_t {
    Limit,
    Market,
    Stop,
    StopLimit,
    Iceberg
};

enum class OrderStatus : uint8_t {
    New,        // accepted, not yet on book
    Resting,    // on book, waiting for match
    PartialFill,// partially filled, remainder resting
    Filled,     // fully filled, removed from book
    Cancelled,  // cancelled by request or exhaustion
    Rejected,   // failed validation
    Triggered   // stop order that has been converted
};

enum class RejectReason : uint8_t {
    None,
    InvalidPrice,
    InvalidQuantity,
    DuplicateOrderId,
    OrderNotFound,
    OrderNotCancellable,
    InvalidModify,
    StopPriceInconsistent,
    PriceBandViolation,
    MaxSizeViolation,
    MarketExhausted
};

enum class EventType : uint8_t {
    NewOrder,
    CancelOrder,
    ModifyOrder,
    ReplaceOrder
};

enum class AggressorSide : uint8_t {
    Buy,
    Sell,
    None
};

} // namespace lob
```

### Notes
- All enums are `uint8_t` backed for minimal memory footprint in the `Order` struct.
- `AggressorSide::None` is used for book snapshot events where no trade occurred.

### Commit
`core: enums — Side, OrderType, OrderStatus, RejectReason, EventType`

---

## Part 1.3 — Order model

### Goal
Define the `Order` struct. This is the central data structure of the entire project. Get it right.

### File: `src/core/order.hpp`

```cpp
#pragma once
#include "core/enums.hpp"
#include <cstdint>
#include <string>

namespace lob {

using OrderId  = uint64_t;
using Price    = int64_t;   // integer price in ticks (avoids float precision issues)
using Quantity = uint64_t;
using Timestamp = uint64_t; // nanoseconds since epoch

struct Order {
    OrderId   id          {0};
    Side      side        {Side::Buy};
    OrderType type        {OrderType::Limit};
    Price     price       {0};      // limit price (0 for market orders)
    Price     stop_price  {0};      // trigger price for stop and stop-limit orders
    Quantity  quantity    {0};      // remaining quantity
    Quantity  orig_qty    {0};      // original quantity (set once, never changed)
    Quantity  peak_qty    {0};      // visible peak for iceberg (0 if not iceberg)
    Quantity  reserve_qty {0};      // hidden reserve for iceberg
    Timestamp timestamp   {0};      // submission timestamp, used for time priority
    OrderStatus status    {OrderStatus::New};

    // Convenience helpers
    [[nodiscard]] bool is_buy()    const noexcept { return side == Side::Buy; }
    [[nodiscard]] bool is_sell()   const noexcept { return side == Side::Sell; }
    [[nodiscard]] bool is_resting() const noexcept {
        return status == OrderStatus::Resting || status == OrderStatus::PartialFill;
    }
    [[nodiscard]] bool is_iceberg() const noexcept { return peak_qty > 0; }
    [[nodiscard]] Quantity visible_qty() const noexcept {
        return is_iceberg() ? peak_qty : quantity;
    }
};

} // namespace lob
```

### Design notes
- `Price` is `int64_t` (integer ticks). Never use `double` for prices in a matching engine. Floating-point comparison is non-deterministic across compilers and platforms.
- `orig_qty` is set at order creation and never modified. Required for execution summary calculations.
- `peak_qty` and `reserve_qty` are 0 for non-iceberg orders — no extra memory cost in a flat struct.
- `timestamp` is the submission time. For modify-with-price-change, a new timestamp is assigned. For modify-qty-reduction, the timestamp is preserved (time priority is kept).

### Commit
`core: Order struct with Price/Quantity/Timestamp type aliases`

---

## Part 1.4 — Trade and execution records

### Goal
Define the output records that the matching engine produces.

### File: `src/core/trade.hpp`

```cpp
#pragma once
#include "core/enums.hpp"
#include "core/order.hpp"

namespace lob {

struct Trade {
    OrderId    aggressor_id  {0};
    OrderId    passive_id    {0};
    Price      price         {0};
    Quantity   quantity      {0};
    Timestamp  timestamp     {0};
    AggressorSide aggressor_side {AggressorSide::None};
};

struct PartialFill {
    OrderId    order_id         {0};
    Quantity   filled_qty       {0};
    Quantity   remaining_qty    {0};
    Timestamp  timestamp        {0};
};

struct ExecutionReport {
    OrderId    order_id         {0};
    OrderStatus status          {OrderStatus::New};
    Quantity   cumulative_qty   {0};  // total filled so far
    Quantity   remaining_qty    {0};
    Price      avg_fill_price   {0};  // cumulative value / cumulative qty
    Timestamp  timestamp        {0};
};

struct Rejection {
    OrderId      order_id  {0};
    RejectReason reason    {RejectReason::None};
    Timestamp    timestamp {0};
};

} // namespace lob
```

### Notes
- `avg_fill_price` is computed as `cumulative_value / cumulative_qty` where `cumulative_value` is tracked separately as `Price * Quantity` sum. Store it as integer arithmetic; divide only when reporting.
- Every matching operation produces a `Trade`, updates the passive order's `ExecutionReport`, and updates the aggressor's `ExecutionReport`.

### Commit
`core: Trade, PartialFill, ExecutionReport, Rejection output records`

---

## Part 1.5 — Event types

### Goal
Define the input event variant that the engine accepts. This is the single entry point for all inbound order flow.

### File: `src/core/event.hpp`

```cpp
#pragma once
#include "core/enums.hpp"
#include "core/order.hpp"
#include <variant>

namespace lob {

struct NewOrderEvent {
    Order order;
};

struct CancelOrderEvent {
    OrderId   order_id  {0};
    Timestamp timestamp {0};
};

struct ModifyOrderEvent {
    OrderId   order_id     {0};
    Price     new_price    {0};  // 0 = no change
    Quantity  new_quantity {0};  // 0 = no change
    Timestamp timestamp    {0};
};

struct ReplaceOrderEvent {
    OrderId   old_order_id {0};
    Order     new_order;         // fully specified new order
};

using OrderEvent = std::variant<
    NewOrderEvent,
    CancelOrderEvent,
    ModifyOrderEvent,
    ReplaceOrderEvent
>;

} // namespace lob
```

### Notes
- `std::variant` + `std::visit` gives exhaustive pattern matching with zero virtual dispatch. The compiler will warn if any variant case is unhandled.
- `ModifyOrderEvent` uses 0 as a sentinel for "no change". The validator will reject a modify where both `new_price` and `new_quantity` are 0.
- `ReplaceOrderEvent` is atomic: if the cancel of `old_order_id` fails validation, the new order is not inserted.

### Commit
`core: OrderEvent variant — NewOrder, Cancel, Modify, Replace`

---

## Part 1.6 — Engine config

### File: `src/core/config.hpp`

```cpp
#pragma once
#include "core/order.hpp"
#include <cstdint>
#include <string>

namespace lob {

struct EngineConfig {
    // Risk limits
    Price    max_price          {10'000'000};  // maximum allowed price in ticks
    Quantity max_order_size     {1'000'000};   // maximum order quantity
    Price    price_band_bps     {500};         // max deviation from mid in basis points

    // Snapshot
    uint64_t snapshot_interval  {10'000};      // snapshot every N events
    std::string snapshot_path   {"snapshots/"};

    // Logging
    std::string log_path        {"logs/"};
    uint32_t    log_queue_size  {65536};

    // Synthetic generator
    Price    mid_price          {10'000};      // starting mid in ticks
    double   arrival_rate       {1000.0};      // orders per second
    double   price_std_dev      {20.0};        // std dev of price around mid (ticks)
    double   cancel_probability {0.3};         // fraction of events that are cancels
    uint32_t burst_size         {0};           // 0 = no burst mode
    bool     market_maker_mode  {false};
};

} // namespace lob
```

### Commit
`core: EngineConfig with risk, snapshot, logging, and generator params`

---

## Part 1.7 — PriceLevel

### Goal
Implement the per-price FIFO queue. This is the hot data structure in the matching loop.

### File: `src/book/price_level.hpp`

```cpp
#pragma once
#include "core/order.hpp"
#include <deque>
#include <cstdint>

namespace lob {

class PriceLevel {
public:
    explicit PriceLevel(Price price) : price_(price) {}

    void push_back(OrderId id, Quantity visible_qty) {
        queue_.push_back(id);
        total_volume_ += visible_qty;
    }

    void pop_front(Quantity filled_qty) {
        queue_.pop_front();
        total_volume_ = (total_volume_ >= filled_qty) ? total_volume_ - filled_qty : 0;
    }

    void reduce_front_volume(Quantity qty) {
        total_volume_ = (total_volume_ >= qty) ? total_volume_ - qty : 0;
    }

    void remove(OrderId id, Quantity visible_qty);  // O(n) — only used for cancel

    [[nodiscard]] bool          empty()        const noexcept { return queue_.empty(); }
    [[nodiscard]] OrderId       front()        const noexcept { return queue_.front(); }
    [[nodiscard]] Price         price()        const noexcept { return price_; }
    [[nodiscard]] Quantity      total_volume() const noexcept { return total_volume_; }
    [[nodiscard]] std::size_t   size()         const noexcept { return queue_.size(); }

private:
    Price              price_        {0};
    Quantity           total_volume_ {0};
    std::deque<OrderId> queue_;
};

} // namespace lob
```

### File: `src/book/price_level.cpp`

```cpp
#include "book/price_level.hpp"
#include <algorithm>

namespace lob {

void PriceLevel::remove(OrderId id, Quantity visible_qty) {
    auto it = std::find(queue_.begin(), queue_.end(), id);
    if (it != queue_.end()) {
        queue_.erase(it);
        total_volume_ = (total_volume_ >= visible_qty) ? total_volume_ - visible_qty : 0;
    }
}

} // namespace lob
```

### Design notes
- `remove()` is O(n) in the level queue. This is acceptable because cancel is not on the matching hot path. The hot path is `front()` / `pop_front()` which are O(1).
- `total_volume_` tracks visible quantity only. For iceberg orders, only `peak_qty` counts toward level volume — the reserve is hidden.
- The saturating subtraction guards against bugs where volume goes negative due to a logic error. In production, this would be an assertion.

### Commit
`book: PriceLevel — FIFO deque with O(1) front/push and O(n) cancel remove`

---

## Part 1.8 — OrderBook

### Goal
Implement the book container: bid map, ask map, order ID map, and best bid/ask cache.

### File: `src/book/order_book.hpp`

```cpp
#pragma once
#include "core/order.hpp"
#include "core/trade.hpp"
#include "book/price_level.hpp"
#include <map>
#include <unordered_map>
#include <functional>
#include <optional>

namespace lob {

class OrderBook {
public:
    // Insertion
    void insert_order(const Order& order);

    // Removal
    bool cancel_order(OrderId id);  // returns false if not found

    // Lookup
    [[nodiscard]] std::optional<Order> find_order(OrderId id) const;
    [[nodiscard]] bool has_order(OrderId id) const noexcept;

    // Best prices
    [[nodiscard]] std::optional<Price> best_bid() const noexcept;
    [[nodiscard]] std::optional<Price> best_ask() const noexcept;
    [[nodiscard]] std::optional<Price> mid_price() const noexcept;
    [[nodiscard]] std::optional<Price> spread()    const noexcept;

    // Level access (for matching engine)
    PriceLevel*       best_bid_level();
    PriceLevel*       best_ask_level();
    Order*            find_order_mut(OrderId id);

    // Introspection
    [[nodiscard]] std::size_t order_count()     const noexcept { return id_map_.size(); }
    [[nodiscard]] std::size_t bid_level_count() const noexcept { return bids_.size(); }
    [[nodiscard]] std::size_t ask_level_count() const noexcept { return asks_.size(); }

    Price last_trade_price {0};

    // Iteration for snapshot and metrics (returns sorted levels)
    const std::map<Price, PriceLevel, std::greater<Price>>& bids() const { return bids_; }
    const std::map<Price, PriceLevel>&                      asks() const { return asks_; }

private:
    // Bids: descending (highest price first — std::greater)
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    // Asks: ascending (lowest price first — default)
    std::map<Price, PriceLevel>                      asks_;
    // O(1) order lookup by ID
    std::unordered_map<OrderId, Order>               id_map_;

    void remove_empty_level(bool is_bid, Price price);
};

} // namespace lob
```

### File: `src/book/order_book.cpp`

```cpp
#include "book/order_book.hpp"

namespace lob {

void OrderBook::insert_order(const Order& order) {
    id_map_[order.id] = order;
    auto& map = order.is_buy() ? reinterpret_cast<std::map<Price,PriceLevel>&>(bids_) : asks_;
    // Use the correct typed map:
    if (order.is_buy()) {
        auto [it, _] = bids_.emplace(order.price, PriceLevel{order.price});
        it->second.push_back(order.id, order.visible_qty());
    } else {
        auto [it, _] = asks_.emplace(order.price, PriceLevel{order.price});
        it->second.push_back(order.id, order.visible_qty());
    }
}

bool OrderBook::cancel_order(OrderId id) {
    auto it = id_map_.find(id);
    if (it == id_map_.end()) return false;
    const Order& order = it->second;
    if (order.is_buy()) {
        auto lvl_it = bids_.find(order.price);
        if (lvl_it != bids_.end()) {
            lvl_it->second.remove(id, order.visible_qty());
            if (lvl_it->second.empty()) bids_.erase(lvl_it);
        }
    } else {
        auto lvl_it = asks_.find(order.price);
        if (lvl_it != asks_.end()) {
            lvl_it->second.remove(id, order.visible_qty());
            if (lvl_it->second.empty()) asks_.erase(lvl_it);
        }
    }
    id_map_.erase(it);
    return true;
}

std::optional<Order> OrderBook::find_order(OrderId id) const {
    auto it = id_map_.find(id);
    if (it == id_map_.end()) return std::nullopt;
    return it->second;
}

bool OrderBook::has_order(OrderId id) const noexcept {
    return id_map_.count(id) > 0;
}

Order* OrderBook::find_order_mut(OrderId id) {
    auto it = id_map_.find(id);
    if (it == id_map_.end()) return nullptr;
    return &it->second;
}

std::optional<Price> OrderBook::best_bid() const noexcept {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const noexcept {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<Price> OrderBook::mid_price() const noexcept {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb || !ba) return std::nullopt;
    return (*bb + *ba) / 2;
}

std::optional<Price> OrderBook::spread() const noexcept {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb || !ba) return std::nullopt;
    return *ba - *bb;
}

PriceLevel* OrderBook::best_bid_level() {
    if (bids_.empty()) return nullptr;
    return &bids_.begin()->second;
}

PriceLevel* OrderBook::best_ask_level() {
    if (asks_.empty()) return nullptr;
    return &asks_.begin()->second;
}

} // namespace lob
```

### Commit
`book: OrderBook — bid/ask maps, ID map, best bid/ask access`

---

## Part 1.9 — Matching engine (limit orders only)

### Goal
Implement the `MatchingEngine` class that processes `NewOrderEvent` for limit orders and produces `Trade` records. Market, stop, and iceberg are not handled yet — throw or assert if they arrive.

### File: `src/engine/matching_engine.hpp`

```cpp
#pragma once
#include "book/order_book.hpp"
#include "core/event.hpp"
#include "core/trade.hpp"
#include <vector>
#include <functional>

namespace lob {

struct MatchResult {
    std::vector<Trade>           trades;
    std::vector<ExecutionReport> reports;
    std::vector<Rejection>       rejections;
};

class MatchingEngine {
public:
    explicit MatchingEngine(EngineConfig config = {});

    MatchResult submit(const OrderEvent& event);

    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }

private:
    OrderBook    book_;
    EngineConfig config_;
    uint64_t     sequence_ {0};  // monotonically increasing event counter

    MatchResult process_new_order(const NewOrderEvent& e);
    MatchResult process_cancel(const CancelOrderEvent& e);
    MatchResult process_modify(const ModifyOrderEvent& e);
    MatchResult process_replace(const ReplaceOrderEvent& e);

    // Core matching
    void match_limit(Order& aggressor, MatchResult& result);

    // Fill helpers
    void execute_fill(Order& aggressor, Order& passive,
                      Quantity fill_qty, MatchResult& result);
};

} // namespace lob
```

### File: `src/engine/matching_engine.cpp`

```cpp
#include "engine/matching_engine.hpp"
#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace lob {

MatchingEngine::MatchingEngine(EngineConfig config)
    : config_(std::move(config)) {}

MatchResult MatchingEngine::submit(const OrderEvent& event) {
    ++sequence_;
    return std::visit([this](const auto& e) -> MatchResult {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, NewOrderEvent>)
            return process_new_order(e);
        else if constexpr (std::is_same_v<T, CancelOrderEvent>)
            return process_cancel(e);
        else if constexpr (std::is_same_v<T, ModifyOrderEvent>)
            return process_modify(e);
        else if constexpr (std::is_same_v<T, ReplaceOrderEvent>)
            return process_replace(e);
    }, event);
}

MatchResult MatchingEngine::process_new_order(const NewOrderEvent& e) {
    MatchResult result;
    Order order = e.order;

    assert(order.type == OrderType::Limit &&
           "Phase 1: only limit orders supported");

    order.status = OrderStatus::Resting;

    match_limit(order, result);

    if (order.quantity > 0) {
        book_.insert_order(order);
    }

    return result;
}

void MatchingEngine::match_limit(Order& aggressor, MatchResult& result) {
    while (aggressor.quantity > 0) {
        PriceLevel* passive_level = aggressor.is_buy()
            ? book_.best_ask_level()
            : book_.best_bid_level();

        if (!passive_level) break;

        // Check price cross
        if (aggressor.is_buy()  && passive_level->price() > aggressor.price) break;
        if (aggressor.is_sell() && passive_level->price() < aggressor.price) break;

        OrderId passive_id = passive_level->front();
        Order*  passive    = book_.find_order_mut(passive_id);
        assert(passive != nullptr);

        Quantity fill_qty = std::min(aggressor.quantity, passive->quantity);
        execute_fill(aggressor, *passive, fill_qty, result);

        if (passive->quantity == 0) {
            passive->status = OrderStatus::Filled;
            passive_level->pop_front(fill_qty);
            if (passive_level->empty()) {
                // Remove empty level
                if (aggressor.is_buy())
                    const_cast<std::map<Price,PriceLevel>&>(
                        reinterpret_cast<const std::map<Price,PriceLevel>&>(
                            book_.asks())).erase(passive_level->price());
                else
                    // handled via book_.cancel_order path in full impl
                    ;
            }
            book_.find_order_mut(passive_id); // will be null after erase — see note
        } else {
            passive_level->reduce_front_volume(fill_qty);
        }
    }

    if (aggressor.quantity == 0)
        aggressor.status = OrderStatus::Filled;
}

void MatchingEngine::execute_fill(Order& aggressor, Order& passive,
                                   Quantity fill_qty, MatchResult& result) {
    Price fill_price = passive.price; // passive order sets the price

    aggressor.quantity -= fill_qty;
    passive.quantity   -= fill_qty;

    book_.last_trade_price = fill_price;

    Trade trade;
    trade.aggressor_id   = aggressor.id;
    trade.passive_id     = passive.id;
    trade.price          = fill_price;
    trade.quantity       = fill_qty;
    trade.aggressor_side = aggressor.is_buy() ? AggressorSide::Buy : AggressorSide::Sell;

    result.trades.push_back(trade);
}

MatchResult MatchingEngine::process_cancel(const CancelOrderEvent& e) {
    MatchResult result;
    if (!book_.cancel_order(e.order_id)) {
        Rejection rej;
        rej.order_id = e.order_id;
        rej.reason   = RejectReason::OrderNotFound;
        result.rejections.push_back(rej);
    }
    return result;
}

MatchResult MatchingEngine::process_modify(const ModifyOrderEvent&) {
    // Implemented in phase 2
    return {};
}

MatchResult MatchingEngine::process_replace(const ReplaceOrderEvent&) {
    // Implemented in phase 2
    return {};
}

} // namespace lob
```

### Note on level removal
The level removal logic in `match_limit` is simplified here for clarity. In the full implementation in Part 1.9, expose a `remove_level(Side, Price)` method on `OrderBook` and call it directly. The inline cast shown above is a placeholder that will be replaced in Part 2.1.

### Commit
`engine: MatchingEngine — limit order matching, partial fills, multi-level sweep`

---

## Part 1.10 — Unit tests for phase 1

### Goal
Test every meaningful behaviour introduced in phase 1. Tests are the acceptance gate.

### File: `tests/unit/test_price_level.cpp`

Test cases:
- `push_back` increases volume
- `pop_front` decreases volume and removes front
- `remove` finds and removes by ID, updates volume
- `empty()` is true after all orders removed
- `front()` returns FIFO order (first inserted)

### File: `tests/unit/test_order_book.cpp`

Test cases:
- `insert_order` — buy order appears on bid side
- `insert_order` — sell order appears on ask side
- `best_bid` returns highest bid price
- `best_ask` returns lowest ask price
- `cancel_order` removes order and updates level
- `cancel_order` on non-existent ID returns false
- Empty book returns `std::nullopt` for best_bid, best_ask, mid, spread

### File: `tests/unit/test_matching_limit.cpp`

Test cases:
- **No match:** limit buy below best ask → rests on book
- **No match:** limit sell above best bid → rests on book
- **Full match:** limit buy at ask price → single trade, both orders filled
- **Partial match (aggressor partial):** aggressor quantity < passive quantity → aggressor filled, passive partially filled and still resting
- **Partial match (passive partial):** aggressor quantity > passive quantity → passive filled, aggressor partially rests
- **Multi-level sweep:** buy sweeps through three ask levels → three trades produced
- **FIFO priority:** two resting orders at same price → earlier order fills first
- **No crossed book:** after any sequence of operations, best_bid < best_ask

### Test structure example:
```cpp
TEST(MatchingEngineLimit, FullMatch) {
    MatchingEngine engine;

    // Place a resting sell
    Order sell;
    sell.id = 1; sell.side = Side::Sell; sell.type = OrderType::Limit;
    sell.price = 100; sell.quantity = 50; sell.orig_qty = 50;
    sell.status = OrderStatus::Resting;
    engine.submit(NewOrderEvent{sell});

    // Aggressive buy at same price
    Order buy;
    buy.id = 2; buy.side = Side::Buy; buy.type = OrderType::Limit;
    buy.price = 100; buy.quantity = 50; buy.orig_qty = 50;
    auto result = engine.submit(NewOrderEvent{buy});

    ASSERT_EQ(result.trades.size(), 1u);
    EXPECT_EQ(result.trades[0].price, 100);
    EXPECT_EQ(result.trades[0].quantity, 50u);
    EXPECT_EQ(result.trades[0].passive_id, 1u);
    EXPECT_EQ(result.trades[0].aggressor_id, 2u);

    // Book should be empty
    EXPECT_FALSE(engine.book().best_bid().has_value());
    EXPECT_FALSE(engine.book().best_ask().has_value());
}
```

### Commit
`tests: unit tests for PriceLevel, OrderBook, and limit order matching`

---

## Phase 1 checklist

Before starting phase 2, every item must be true.

### Build
- [ ] `cmake -B build && cmake --build build` succeeds with zero warnings on GCC
- [ ] Same succeeds on Clang
- [ ] `-Wall -Wextra -Wpedantic -Werror` flags are active

### Tests
- [ ] All unit tests in `tests/unit/` pass
- [ ] `test_price_level`: all cases pass
- [ ] `test_order_book`: all cases pass
- [ ] `test_matching_limit`: all cases pass including FIFO priority and multi-level sweep

### Correctness invariants (verify manually or with a test)
- [ ] After any sequence of limit order submissions, `best_bid < best_ask` (no crossed book)
- [ ] After a full fill, the order does not appear on the book
- [ ] After a partial fill, the passive order's remaining quantity is correct
- [ ] Level volume equals sum of visible quantities of all orders at that level
- [ ] `order_count()` equals the number of resting orders

### Code quality
- [ ] `clang-format --dry-run src/ tests/` produces no diff
- [ ] No `TODO` or `FIXME` markers left in committed code (use GitHub issues instead)
- [ ] Every `.hpp` has a `#pragma once`
- [ ] Every class/struct is in `namespace lob`

### Git
- [ ] All commits from parts 1.1 through 1.10 are present
- [ ] Each commit message follows the pattern: `scope: description`
- [ ] `main` branch is clean (no uncommitted changes)

---

---

# Phase 2 — Market orders, cancel, modify, and replace

**Goal:** Complete order lifecycle. By the end of this phase the engine handles every non-advanced event type correctly, including all edge cases.

---

## Part 2.1 — Clean up level removal and expose OrderBook mutation API

### Goal
The level removal in Part 1.9 was a placeholder. Fix it properly before building on top of it.

### Changes to `src/book/order_book.hpp`
Add:
```cpp
void remove_best_ask_level();
void remove_best_bid_level();
void update_order_quantity(OrderId id, Quantity new_qty);
```

### Changes to `src/book/order_book.cpp`
Implement:
```cpp
void OrderBook::remove_best_ask_level() {
    if (!asks_.empty()) asks_.erase(asks_.begin());
}
void OrderBook::remove_best_bid_level() {
    if (!bids_.empty()) bids_.erase(bids_.begin());
}
void OrderBook::update_order_quantity(OrderId id, Quantity new_qty) {
    auto it = id_map_.find(id);
    if (it != id_map_.end()) it->second.quantity = new_qty;
}
```

### Update `match_limit` in `matching_engine.cpp`
Replace the inline cast with:
```cpp
if (passive->quantity == 0) {
    passive->status = OrderStatus::Filled;
    book_.cancel_order(passive_id);  // removes from level and id_map
}
```

`cancel_order` already handles level cleanup when the queue becomes empty.

### Commit
`book: expose remove_level and update_order_quantity, fix level cleanup in engine`

---

## Part 2.2 — Market order matching

### Goal
Market orders sweep all available liquidity until filled or the book is exhausted. No price limit.

### New method in `matching_engine.cpp`

```cpp
MatchResult MatchingEngine::process_new_order(const NewOrderEvent& e) {
    MatchResult result;
    Order order = e.order;
    order.status = OrderStatus::Resting;

    if (order.type == OrderType::Limit) {
        match_limit(order, result);
    } else if (order.type == OrderType::Market) {
        match_market(order, result);
    } else {
        // Stop and iceberg handled in phase 3
        assert(false && "Unsupported order type in phase 2");
    }

    if (order.quantity > 0 && order.type == OrderType::Limit) {
        book_.insert_order(order);
    }

    return result;
}

void MatchingEngine::match_market(Order& aggressor, MatchResult& result) {
    while (aggressor.quantity > 0) {
        PriceLevel* passive_level = aggressor.is_buy()
            ? book_.best_ask_level()
            : book_.best_bid_level();

        if (!passive_level) {
            // Book exhausted — cancel residual
            Rejection rej;
            rej.order_id = aggressor.id;
            rej.reason   = RejectReason::MarketExhausted;
            result.rejections.push_back(rej);
            aggressor.status = OrderStatus::Cancelled;
            break;
        }

        OrderId passive_id = passive_level->front();
        Order*  passive    = book_.find_order_mut(passive_id);
        Quantity fill_qty  = std::min(aggressor.quantity, passive->quantity);

        execute_fill(aggressor, *passive, fill_qty, result);

        if (passive->quantity == 0) {
            book_.cancel_order(passive_id);
        } else {
            passive_level->reduce_front_volume(fill_qty);
            book_.update_order_quantity(passive_id, passive->quantity);
        }
    }

    if (aggressor.quantity == 0)
        aggressor.status = OrderStatus::Filled;
}
```

### Commit
`engine: market order matching with multi-level sweep and exhaustion handling`

---

## Part 2.3 — Cancel order

### Goal
Full cancel implementation with validation.

### Update `process_cancel` in `matching_engine.cpp`

```cpp
MatchResult MatchingEngine::process_cancel(const CancelOrderEvent& e) {
    MatchResult result;

    auto order_opt = book_.find_order(e.order_id);
    if (!order_opt) {
        Rejection rej;
        rej.order_id  = e.order_id;
        rej.reason    = RejectReason::OrderNotFound;
        rej.timestamp = e.timestamp;
        result.rejections.push_back(rej);
        return result;
    }

    if (!order_opt->is_resting()) {
        Rejection rej;
        rej.order_id  = e.order_id;
        rej.reason    = RejectReason::OrderNotCancellable;
        rej.timestamp = e.timestamp;
        result.rejections.push_back(rej);
        return result;
    }

    book_.cancel_order(e.order_id);

    ExecutionReport rep;
    rep.order_id      = e.order_id;
    rep.status        = OrderStatus::Cancelled;
    rep.remaining_qty = order_opt->quantity;
    rep.timestamp     = e.timestamp;
    result.reports.push_back(rep);

    return result;
}
```

### Commit
`engine: cancel order with validation and execution report`

---

## Part 2.4 — Modify order

### Goal
Quantity reduction preserves time priority. Price change or quantity increase loses time priority (cancel + reinsert).

### Update `process_modify` in `matching_engine.cpp`

```cpp
MatchResult MatchingEngine::process_modify(const ModifyOrderEvent& e) {
    MatchResult result;

    auto order_opt = book_.find_order(e.order_id);
    if (!order_opt || !order_opt->is_resting()) {
        Rejection rej;
        rej.order_id = e.order_id;
        rej.reason   = !order_opt ? RejectReason::OrderNotFound
                                  : RejectReason::OrderNotCancellable;
        result.rejections.push_back(rej);
        return result;
    }

    Order order = *order_opt;
    bool price_changed = (e.new_price != 0 && e.new_price != order.price);
    bool qty_increased = (e.new_quantity != 0 && e.new_quantity > order.quantity);

    if (price_changed || qty_increased) {
        // Loses time priority: cancel and reinsert
        book_.cancel_order(e.order_id);
        if (e.new_price    != 0) order.price    = e.new_price;
        if (e.new_quantity != 0) order.quantity = e.new_quantity;
        order.timestamp = e.timestamp; // new timestamp = new time priority
        book_.insert_order(order);
    } else {
        // Quantity reduction only: in-place update, preserve priority
        if (e.new_quantity != 0 && e.new_quantity < order.quantity) {
            Quantity delta = order.quantity - e.new_quantity;
            book_.update_order_quantity(e.order_id, e.new_quantity);
            // Update level volume
            // (requires a reduce_volume_at method on OrderBook — see below)
            book_.reduce_level_volume(order.side, order.price, delta);
        }
    }

    return result;
}
```

### Add to `OrderBook`:
```cpp
void reduce_level_volume(Side side, Price price, Quantity delta);
```

```cpp
void OrderBook::reduce_level_volume(Side side, Price price, Quantity delta) {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it != bids_.end()) it->second.reduce_front_volume(delta);
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end()) it->second.reduce_front_volume(delta);
    }
}
```

Note: `reduce_front_volume` on `PriceLevel` reduces total_volume but does NOT pop the front. This is correct for in-place quantity reduction.

### Commit
`engine: modify order — qty reduction preserves priority, price change loses priority`

---

## Part 2.5 — Replace order

### Goal
Atomic cancel + new order. If cancel fails, new order is not inserted.

### Update `process_replace` in `matching_engine.cpp`

```cpp
MatchResult MatchingEngine::process_replace(const ReplaceOrderEvent& e) {
    MatchResult result;

    if (!book_.has_order(e.old_order_id)) {
        Rejection rej;
        rej.order_id = e.old_order_id;
        rej.reason   = RejectReason::OrderNotFound;
        result.rejections.push_back(rej);
        return result;
    }

    book_.cancel_order(e.old_order_id);

    // Submit new order through normal path
    auto new_result = process_new_order(NewOrderEvent{e.new_order});
    result.trades.insert(result.trades.end(),
                         new_result.trades.begin(), new_result.trades.end());
    result.reports.insert(result.reports.end(),
                          new_result.reports.begin(), new_result.reports.end());
    result.rejections.insert(result.rejections.end(),
                             new_result.rejections.begin(), new_result.rejections.end());
    return result;
}
```

### Commit
`engine: replace order — atomic cancel and reinsert`

---

## Part 2.6 — Unit tests for phase 2

### File: `tests/unit/test_matching_market.cpp`

Test cases:
- Market buy fills completely against resting asks
- Market buy sweeps multiple levels
- Market buy on empty book → `MarketExhausted` rejection
- Market buy larger than available liquidity → partial fill, residual rejected
- Market sell symmetric cases

### File: `tests/unit/test_cancel.cpp`

Test cases:
- Cancel existing resting order → success, order not on book
- Cancel non-existent order → `OrderNotFound` rejection
- Cancel already-filled order → `OrderNotCancellable` rejection

### File: `tests/unit/test_modify.cpp`

Test cases:
- Qty reduction → order stays at same position in queue
- Price change → order moves to new price level, gets new timestamp
- Qty increase → treated as cancel + reinsert (loses priority)
- Modify non-existent order → rejection

### File: `tests/unit/test_replace.cpp`

Test cases:
- Replace existing order → old order gone, new order inserted
- Replace with immediately matching new order → trade produced
- Replace non-existent order → rejection, no new order inserted

### Commit
`tests: market order, cancel, modify, replace unit tests`

---

## Phase 2 checklist

### Build
- [ ] Clean build with zero warnings on GCC and Clang
- [ ] `clang-format` produces no diff

### Tests
- [ ] All phase 1 tests still pass (no regressions)
- [ ] `test_matching_market`: all market order cases pass
- [ ] `test_cancel`: all cancel cases pass
- [ ] `test_modify`: priority preservation and loss cases both tested and passing
- [ ] `test_replace`: atomicity verified (failed cancel blocks new order insertion)

### Correctness invariants
- [ ] Market order on empty book produces `MarketExhausted` rejection, not a crash or hang
- [ ] After market order sweeps multiple levels, all swept levels are removed from the book
- [ ] Modify qty-reduction does not change order's position in FIFO queue
- [ ] Replace failure (old ID not found) does not insert new order

### Code quality
- [ ] No remaining `// Implemented in phase 2` placeholder comments
- [ ] All public methods on `OrderBook` have `[[nodiscard]]` where appropriate

### Git
- [ ] All commits from parts 2.1 through 2.6 are present with correct messages

---

---

# Phase 3 — Stop orders and iceberg orders

**Goal:** The two advanced order types that demonstrate real exchange mechanics. Both require careful state management and correct trigger / replenishment logic.

---

## Part 3.1 — Stop order data structures

### Goal
Add the pending stop list to the engine and a method to evaluate triggers after every trade.

### Add to `src/engine/matching_engine.hpp`

```cpp
#include <vector>

// Inside MatchingEngine private section:
std::vector<Order> pending_stops_;

void evaluate_stop_triggers(MatchResult& result);
void process_triggered_stop(Order& stop, MatchResult& result);
```

### Stop order lifecycle
1. A stop order arrives via `NewOrderEvent`.
2. Validator checks trigger price consistency (buy stop: `stop_price >= best_ask`; sell stop: `stop_price <= best_bid`).
3. If valid, placed in `pending_stops_`. Does NOT go on the book.
4. After every trade, `evaluate_stop_triggers` is called with the last trade price.
5. If triggered, the stop is converted to a market order (or limit order for stop-limit) and submitted through `process_new_order`.

### Commit
`engine: stop order pending list and trigger evaluation skeleton`

---

## Part 3.2 — Stop trigger evaluation

### File: `src/engine/matching_engine.cpp`

```cpp
void MatchingEngine::evaluate_stop_triggers(MatchResult& result) {
    if (pending_stops_.empty()) return;

    Price last_price = book_.last_trade_price;
    if (last_price == 0) return;

    std::vector<Order> remaining;
    for (auto& stop : pending_stops_) {
        bool triggered = false;
        if (stop.is_buy()  && last_price >= stop.stop_price) triggered = true;
        if (stop.is_sell() && last_price <= stop.stop_price) triggered = true;

        if (triggered) {
            process_triggered_stop(stop, result);
        } else {
            remaining.push_back(stop);
        }
    }
    pending_stops_ = std::move(remaining);
}

void MatchingEngine::process_triggered_stop(Order& stop, MatchResult& result) {
    stop.status = OrderStatus::Triggered;

    Order converted = stop;
    if (stop.type == OrderType::Stop) {
        converted.type  = OrderType::Market;
        converted.price = 0;
    } else { // StopLimit
        converted.type  = OrderType::Limit;
        // converted.price is already set to the limit price
    }

    auto sub_result = process_new_order(NewOrderEvent{converted});
    // Merge sub_result into result
    result.trades.insert(result.trades.end(),
                         sub_result.trades.begin(), sub_result.trades.end());
    result.reports.insert(result.reports.end(),
                          sub_result.reports.begin(), sub_result.reports.end());
    result.rejections.insert(result.rejections.end(),
                             sub_result.rejections.begin(), sub_result.rejections.end());
}
```

### Call `evaluate_stop_triggers` at the end of `execute_fill`:
```cpp
void MatchingEngine::execute_fill(...) {
    // ... existing fill logic ...
    evaluate_stop_triggers(result);
}
```

### Commit
`engine: stop trigger evaluation — converts to market/limit on price crossing`

---

## Part 3.3 — Stop order new order handling

### Update `process_new_order` to handle stop orders:

```cpp
} else if (order.type == OrderType::Stop || order.type == OrderType::StopLimit) {
    // Check if immediately triggered
    bool immediately_triggered = false;
    if (order.is_buy() && book_.last_trade_price >= order.stop_price)
        immediately_triggered = true;
    if (order.is_sell() && book_.last_trade_price <= order.stop_price)
        immediately_triggered = true;

    if (immediately_triggered) {
        process_triggered_stop(order, result);
    } else {
        order.status = OrderStatus::Resting; // resting in pending list
        pending_stops_.push_back(order);
    }
}
```

### Commit
`engine: stop order submission — pending list placement and immediate trigger check`

---

## Part 3.4 — Iceberg order matching and replenishment

### Goal
Iceberg orders show only `peak_qty` on the book. When the peak is filled, the reserve replenishes it, and the order is reinserted at the back of the queue (time priority resets).

### Update `process_new_order` for iceberg:

```cpp
} else if (order.type == OrderType::Iceberg) {
    // Set peak as visible quantity; reserve is remainder
    order.reserve_qty = order.quantity - order.peak_qty;
    order.quantity    = order.peak_qty;
    order.status      = OrderStatus::Resting;
    match_limit(order, result); // match against peak
    if (order.quantity > 0 || order.reserve_qty > 0) {
        book_.insert_order(order);
    }
}
```

### Add iceberg replenishment to `execute_fill`:

After filling the passive order, check if it's an iceberg with an exhausted peak:

```cpp
// In execute_fill, after updating passive->quantity:
if (passive->quantity == 0 && passive->is_iceberg() && passive->reserve_qty > 0) {
    replenish_iceberg(*passive, result);
    return; // do not call cancel_order — order stays on book
}
if (passive->quantity == 0) {
    book_.cancel_order(passive_id); // normal full fill removal
}
```

### New method `replenish_iceberg`:

```cpp
void MatchingEngine::replenish_iceberg(Order& order, MatchResult& result) {
    // Remove from current position in level queue
    // (it's at the front — we just filled it)
    // Get the level and pop front
    PriceLevel* level = order.is_buy()
        ? nullptr // buy icebergs don't exist as passive — ask side only
        : book_.best_ask_level(); // more precisely: level at order.price

    // Get the level at order.price
    // Expose get_ask_level(Price) on OrderBook:
    level = book_.get_level(order.side, order.price);

    Quantity new_peak = std::min(order.peak_qty, order.reserve_qty);
    order.reserve_qty -= new_peak;
    order.quantity     = new_peak;

    // Pop the exhausted front entry
    level->pop_front(0); // volume already reduced during fill

    // Reinsert at back — new time priority
    level->push_back(order.id, new_peak);
    book_.update_order_quantity(order.id, new_peak);
}
```

### Add to `OrderBook`:
```cpp
PriceLevel* get_level(Side side, Price price);
```

```cpp
PriceLevel* OrderBook::get_level(Side side, Price price) {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        return it != bids_.end() ? &it->second : nullptr;
    } else {
        auto it = asks_.find(price);
        return it != asks_.end() ? &it->second : nullptr;
    }
}
```

### Commit
`engine: iceberg order — peak/reserve tracking, replenishment with time priority reset`

---

## Part 3.5 — Unit tests for phase 3

### File: `tests/unit/test_stop_orders.cpp`

Test cases:
- Buy stop at price above market → placed in pending, not on book
- Buy stop triggered when trade at or above trigger price → converted to market, executes
- Sell stop triggered when trade at or below trigger price → executes
- Stop-limit triggered → rests as limit order at limit price, does not immediately sweep
- Stop placed when already triggered (immediate trigger) → executes immediately
- Multiple stops at different trigger prices → only correct ones trigger per trade
- Stop cancel before trigger → removed from pending list

### File: `tests/unit/test_iceberg.cpp`

Test cases:
- Iceberg: only peak_qty visible on book (level volume == peak_qty)
- Iceberg: filled up to peak → replenishment with new_peak = min(original_peak, reserve)
- Iceberg: after replenishment, order is at back of queue (FIFO test)
- Iceberg: full exhaustion (peak + reserve both filled) → order removed from book
- Iceberg: aggressor fills across multiple replenishments in single sweep
- Two icebergs at same level → FIFO respected, first iceberg fills before second

### Commit
`tests: stop order and iceberg unit tests`

---

## Phase 3 checklist

### Build
- [ ] Clean build zero warnings GCC and Clang
- [ ] `clang-format` no diff

### Tests
- [ ] All phase 1 and 2 tests still pass
- [ ] `test_stop_orders`: trigger at boundary price (exactly at trigger), above, and below all tested
- [ ] `test_stop_orders`: stop-limit does NOT immediately sweep — it rests as a limit
- [ ] `test_iceberg`: level volume reflects only peak, not reserve
- [ ] `test_iceberg`: replenishment resets time priority (verified by FIFO test)
- [ ] `test_iceberg`: reserve exhaustion removes order from book

### Correctness invariants
- [ ] Stop orders never appear in `bids_` or `asks_` maps — only in `pending_stops_`
- [ ] After a stop trigger event, `pending_stops_` is checked for chain triggers (a triggered stop causes a trade, which may trigger another stop — verify this chain works)
- [ ] Iceberg: `visible_qty()` always returns peak_qty when peak > 0
- [ ] No crossed book after any stop trigger or iceberg replenishment

### Git
- [ ] Parts 3.1 through 3.5 committed in order
- [ ] Commit messages clearly describe stop vs iceberg work

---

---

# Phase 4 — Snapshot, replay engine, and async infrastructure

**Goal:** Deterministic replay, book state persistence, CSV feed reader, synthetic generator, async logger, metrics. This phase is the systems layer that makes the project feel like a real exchange component.

---

## Part 4.1 — CSV event log reader

### File: `src/feed/csv_replay.hpp` / `.cpp`

CSV format (one event per line):
```
event_type,order_id,side,order_type,price,quantity,peak_qty,stop_price,timestamp
NEW,1,BUY,LIMIT,10050,100,0,0,1700000000000000000
NEW,2,SELL,LIMIT,10060,50,0,0,1700000000000001000
CANCEL,1,,,,,,1700000000000002000
MODIFY,3,,,,75,,1700000000000003000
```

Implementation:
- Parse line by line
- For each line, construct the appropriate `OrderEvent` variant
- On parse error, emit a `MalformedEvent` log record and skip the line (do not abort)
- Expose an iterator interface: `bool next(OrderEvent& out)`
- Track line number for error reporting

### Commit
`feed: CSV replay reader with malformed-line tolerance`

---

## Part 4.2 — Synthetic order generator

### File: `src/feed/synthetic_gen.hpp` / `.cpp`

```cpp
class SyntheticGenerator {
public:
    explicit SyntheticGenerator(EngineConfig config, uint64_t seed = 42);
    bool next(OrderEvent& out);  // generates next event, returns false when done

private:
    // State
    Price        mid_price_;
    uint64_t     next_order_id_ {1};
    uint64_t     event_count_   {0};
    uint64_t     max_events_;

    // RNG
    std::mt19937_64 rng_;
    std::normal_distribution<double> price_dist_;
    std::uniform_real_distribution<double> uniform_;

    OrderEvent generate_new_order();
    OrderEvent generate_cancel();
    Price      sample_price();
};
```

Generator behaviour:
- With probability `cancel_probability`: generate a cancel for a randomly chosen live order ID
- Otherwise: generate a new limit order
- Price sampled from `Normal(mid_price, price_std_dev)`, rounded to nearest tick
- Side randomly Buy or Sell with equal probability
- Quantity sampled uniformly from [1, 500]
- If `market_maker_mode`: always place orders within 3 ticks of mid on both sides
- Timestamps increment by a fixed interval (simulate 1000 orders/sec)

### Commit
`feed: synthetic order generator with configurable parameters and seeded RNG`

---

## Part 4.3 — SPSC ring buffer (ingress queue)

### File: `src/core/spsc_queue.hpp`

A header-only, lock-free, single-producer single-consumer ring buffer:

```cpp
#pragma once
#include <atomic>
#include <array>
#include <optional>
#include <cstddef>

namespace lob {

template<typename T, std::size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
public:
    bool push(const T& item) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next_tail = (tail + 1) & mask_;
        if (next_tail == head_.load(std::memory_order_acquire)) return false; // full
        buffer_[tail] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return false; // empty
        item = buffer_[head];
        head_.store((head + 1) & mask_, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire)
            == tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t mask_ = Capacity - 1;
    alignas(64) std::atomic<std::size_t> head_ {0};
    alignas(64) std::atomic<std::size_t> tail_ {0};
    std::array<T, Capacity> buffer_;
};

} // namespace lob
```

### Design notes
- `alignas(64)` on head and tail prevents false sharing on cache lines.
- Power-of-two capacity allows bitmask instead of modulo.
- `memory_order_acquire` / `release` is the correct minimal ordering for SPSC.

### Commit
`core: lock-free SPSC ring buffer with cache-line alignment`

---

## Part 4.4 — Async structured logger

### File: `src/logging/logger.hpp` / `.cpp`

```cpp
class Logger {
public:
    enum class Level { Debug, Info, Warn, Error };

    explicit Logger(std::string path, uint32_t queue_size = 65536);
    ~Logger(); // joins background thread

    void log(Level level, std::string_view category, std::string_view message);
    [[nodiscard]] uint64_t drop_count() const noexcept { return drops_.load(); }

    void start();
    void stop();

private:
    struct LogRecord {
        Level       level;
        char        category[16];
        char        message[256];
        uint64_t    timestamp;
    };

    // Fixed-size queue to avoid heap allocation on the hot path
    std::vector<LogRecord>     buffer_;
    std::atomic<uint32_t>      head_ {0};
    std::atomic<uint32_t>      tail_ {0};
    std::atomic<uint64_t>      drops_ {0};
    std::atomic<bool>          running_ {false};
    std::thread                writer_thread_;
    std::string                path_;

    void writer_loop();
};
```

Output format (JSON lines):
```json
{"ts":1700000000000000000,"level":"INFO","cat":"TRADE","msg":"..."}
```

### Key constraint
`log()` must never block. If the internal queue is full, increment `drops_` and return immediately.

### Commit
`logging: async structured logger — lock-free queue, background writer, drop-on-full`

---

## Part 4.5 — Metrics subsystem

### File: `src/metrics/histogram.hpp`

```cpp
class LatencyHistogram {
public:
    void record(uint64_t latency_ns);

    [[nodiscard]] uint64_t percentile(double p) const; // p in [0.0, 100.0]
    [[nodiscard]] uint64_t p50()  const { return percentile(50.0); }
    [[nodiscard]] uint64_t p95()  const { return percentile(95.0); }
    [[nodiscard]] uint64_t p99()  const { return percentile(99.0); }
    [[nodiscard]] uint64_t max()  const noexcept { return max_; }
    [[nodiscard]] uint64_t count() const noexcept { return count_; }
    void reset();

private:
    // Buckets: 0-999 ns (1 ns res), 1µs-999µs (1 µs res), 1ms-9ms (100 µs res)
    static constexpr std::size_t kBuckets = 1000 + 1000 + 90;
    std::array<uint64_t, kBuckets> buckets_ {};
    uint64_t count_ {0};
    uint64_t max_   {0};
};
```

### File: `src/metrics/metrics.hpp`

```cpp
class Metrics {
public:
    // Called by matching engine after each event
    void record_event_latency(uint64_t latency_ns);
    void record_trade();
    void record_rejection();
    void record_cancel();

    // Called by microstructure tracker
    void update_spread(Price spread);
    void update_imbalance(double imbalance); // (bid_vol - ask_vol) / (bid_vol + ask_vol)
    void update_depth(std::size_t bid_levels, std::size_t ask_levels);

    // Reporting
    void print_summary() const;
    [[nodiscard]] const LatencyHistogram& latency() const { return latency_; }

private:
    LatencyHistogram latency_;
    std::atomic<uint64_t> trade_count_     {0};
    std::atomic<uint64_t> rejection_count_ {0};
    std::atomic<uint64_t> cancel_count_    {0};
    Price   last_spread_    {0};
    double  last_imbalance_ {0.0};
};
```

### Commit
`metrics: latency histogram, trade/reject/cancel counters, microstructure signals`

---

## Part 4.6 — Snapshot and restore

### File: `src/snapshot/snapshot.hpp` / `.cpp`

Binary snapshot format (write in this order):
1. Magic bytes: `0x4C4F4201` (LOB\x01)
2. `uint64_t` sequence number
3. `int64_t` last_trade_price
4. `uint32_t` order_count
5. For each order: serialised `Order` struct (fixed-size POD, write raw bytes)
6. `uint32_t` stop_count
7. For each stop: serialised `Order` struct

```cpp
class SnapshotManager {
public:
    explicit SnapshotManager(std::string path);

    bool save(const OrderBook& book,
              const std::vector<Order>& pending_stops,
              uint64_t sequence);

    bool load(OrderBook& book,
              std::vector<Order>& pending_stops,
              uint64_t& sequence);

private:
    std::string path_;
    std::string snapshot_filename(uint64_t sequence) const;
};
```

### Commit
`snapshot: binary book state serialisation and restore`

---

## Part 4.7 — Replay engine

### File: `tools/replay.cpp`

CLI tool:
```
./lob_replay --snapshot snapshots/snap_10000.bin --events data/sample-replay/events.csv
             --expected data/sample-replay/expected_trades.csv
```

Behaviour:
1. Load snapshot
2. Reconstruct `MatchingEngine` from restored book state
3. Feed events from CSV starting at snapshot's sequence number
4. Write produced trades to a temp file
5. Diff against `--expected` file
6. Exit 0 if identical, exit 1 if differs (prints first diverging line)

This tool is the foundation of the replay test suite in CI.

### Commit
`tools: replay CLI — snapshot load, event feed, expected output diff`

---

## Part 4.8 — Replay tests

### Directory: `tests/replay/data/`

Commit two replay test fixtures:

**Fixture 1: `basic_limit.csv` + `basic_limit_expected.csv`**
A sequence of 20 limit orders on both sides producing 5 trades. Hand-verified.

**Fixture 2: `stop_trigger.csv` + `stop_trigger_expected.csv`**
A sequence with two stop orders that trigger at known prices.

### File: `tests/replay/test_replay.cpp`

```cpp
TEST(Replay, BasicLimitDeterministic) {
    // Run replay twice, compare outputs — must be identical
    auto output1 = run_replay("tests/replay/data/basic_limit.csv");
    auto output2 = run_replay("tests/replay/data/basic_limit.csv");
    EXPECT_EQ(output1, output2);
}

TEST(Replay, BasicLimitMatchesExpected) {
    auto output = run_replay("tests/replay/data/basic_limit.csv");
    auto expected = load_expected("tests/replay/data/basic_limit_expected.csv");
    EXPECT_EQ(output, expected);
}
```

### Commit
`tests: replay fixtures and determinism tests`

---

## Phase 4 checklist

### Build
- [ ] Clean build zero warnings GCC and Clang
- [ ] `lob_engine` and `lob_replay` executables both build

### Tests
- [ ] All phase 1, 2, 3 tests still pass
- [ ] Replay test: same input → identical output on two runs (bit-identical)
- [ ] Replay test: output matches expected fixture file exactly
- [ ] Snapshot round-trip: save then load → book state identical (test in `test_snapshot.cpp`)

### SPSC queue
- [ ] `SpscQueue`: push returns false when full (no overflow)
- [ ] `SpscQueue`: pop returns false when empty (no underread)
- [ ] `SpscQueue`: single push then pop returns same item

### Async logger
- [ ] Logger does not block matching core when queue full (drop counter increments)
- [ ] Logger output file contains valid JSON lines
- [ ] Logger joins cleanly on destruction (no hung thread)

### Synthetic generator
- [ ] Generator produces only valid events (feed through validator, zero rejections for price/qty)
- [ ] Generator with `cancel_probability=0` produces only NEW events
- [ ] Generator with fixed seed produces identical sequence on two runs

### Git
- [ ] Parts 4.1 through 4.8 committed in order

---

---

# Phase 5 — Benchmark suite, profiling, and optimisation

**Goal:** The performance story. Measure, identify bottlenecks, optimise one or two things, document the gain.

---

## Part 5.1 — Benchmark harness

### File: `benchmarks/bench_main.cpp`

```cpp
int main(int argc, char** argv) {
    std::string scenario = "medium";
    uint64_t    seed     = 42;
    uint32_t    duration_sec = 10;

    // parse argv for --scenario, --seed, --duration

    BenchmarkRunner runner(scenario, seed, duration_sec);
    runner.run();
    runner.report();
}
```

### File: `benchmarks/benchmark_runner.hpp`

```cpp
class BenchmarkRunner {
public:
    BenchmarkRunner(std::string scenario, uint64_t seed, uint32_t duration_sec);
    void run();
    void report() const;

private:
    void run_scenario();
    uint64_t events_processed_ {0};
    LatencyHistogram latency_;
    std::string scenario_;
    uint64_t    seed_;
    uint32_t    duration_sec_;
};
```

Each benchmark loop:
```
auto start = now_ns();
for each event from generator:
    auto t0 = now_ns();
    engine.submit(event);
    auto t1 = now_ns();
    latency_.record(t1 - t0);
    ++events_processed_;
    if (now_ns() - start > duration_ns) break;
```

Use `clock_gettime(CLOCK_MONOTONIC)` for nanosecond timestamps. Do not use `std::chrono` in the hot loop — it has higher overhead on some implementations.

### Commit
`benchmarks: harness with nanosecond timing and latency histogram`

---

## Part 5.2 — Benchmark scenarios

### File: `benchmarks/scenarios.hpp`

Each scenario is a `SyntheticGenerator` config:

| Scenario | arrival_rate | cancel_prob | price_std_dev | notes |
|---|---|---|---|---|
| small | 100 | 0.1 | 5 | Thin book, low activity |
| medium | 1000 | 0.3 | 20 | Baseline scenario |
| large | 10000 | 0.3 | 20 | High throughput stress |
| high_cancel | 5000 | 0.7 | 20 | Cancel-heavy workload |
| market_heavy | 2000 | 0.1 | 20 | 30% market orders |
| iceberg_stop | 1000 | 0.2 | 20 | 20% iceberg, 10% stop |

### Add to CMakeLists.txt:
```cmake
add_executable(lob_bench benchmarks/bench_main.cpp benchmarks/benchmark_runner.cpp)
target_link_libraries(lob_bench lob_core)
```

### Commit
`benchmarks: six scenarios — small, medium, large, high_cancel, market_heavy, iceberg_stop`

---

## Part 5.3 — Baseline benchmark run

Run all six scenarios and record results. This is the baseline before any optimisation.

```bash
cmake -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O2 -march=native"
cmake --build build_release
./build_release/lob_bench --scenario medium --duration 10
# ... repeat for all scenarios
```

Record in `docs/performance.md`:
```markdown
## Baseline results (pre-optimisation)

| Scenario      | Throughput (ev/s) | p50 (ns) | p95 (ns) | p99 (ns) | max (ns) |
|---|---|---|---|---|---|
| small         | ...               | ...      | ...      | ...      | ...      |
| medium        | ...               | ...      | ...      | ...      | ...      |
| large         | ...               | ...      | ...      | ...      | ...      |
| high_cancel   | ...               | ...      | ...      | ...      | ...      |
| market_heavy  | ...               | ...      | ...      | ...      | ...      |
| iceberg_stop  | ...               | ...      | ...      | ...      | ...      |
```

### Commit
`docs: baseline benchmark results (pre-optimisation)`

---

## Part 5.4 — Profiling

Profile the `large` scenario (highest throughput stress).

```bash
# Linux perf
perf record -g ./build_release/lob_bench --scenario large --duration 30
perf report

# Or gprof
cmake -B build_prof -DCMAKE_CXX_FLAGS="-O2 -pg"
cmake --build build_prof
./build_prof/lob_bench --scenario large --duration 30
gprof build_prof/lob_bench gmon.out > profile.txt
```

Identify the top three functions by self time. Expected candidates:
- `std::unordered_map` operations in the ID lookup path
- `std::deque` operations in `PriceLevel`
- `execute_fill` — examine allocation patterns
- `evaluate_stop_triggers` — linear scan of pending_stops

Document findings in `docs/performance.md` under `## Profiling findings`.

### Commit
`docs: profiling findings — hot paths identified`

---

## Part 5.5 — Targeted optimisation

Apply one or two specific optimisations based on profiling. Choose from:

**Option A: Replace `std::map` with flat sorted structure for shallow books**
At book depths < 20 levels, a sorted `std::vector<std::pair<Price, PriceLevel>>` with binary search has better cache locality than `std::map` (no pointer chasing). Implement `FlatBook` and compare.

**Option B: Reserve `unordered_map` capacity**
Before a benchmark run, call `id_map_.reserve(expected_order_count)` to eliminate rehash events during stress.

**Option C: Pool-allocate `Order` objects**
If profiling shows significant allocation pressure, a simple free-list pool for `Order` structs eliminates per-order `new`/`delete`.

**Option D: Reduce `pending_stops_` scan**
If stop orders are present in benchmarks, replace the `std::vector` linear scan with a sorted structure (two priority queues — one for buy stops by ascending trigger, one for sell stops by descending trigger). This makes trigger evaluation O(log n) instead of O(n).

For each optimisation applied:
1. Implement behind a compile-time flag or in a separate branch
2. Run all six benchmark scenarios
3. Record post-optimisation results in `docs/performance.md`
4. Compute % change in throughput and p99 latency

### Commit
`perf: [description of optimisation applied], [X]% throughput improvement on large scenario`

---

## Part 5.6 — Sanitizer stress pass

Run the high_cancel and large scenarios through the sanitizer build:

```bash
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g"
cmake --build build_asan
./build_asan/lob_bench --scenario large --duration 5
./build_asan/lob_bench --scenario high_cancel --duration 5
```

Expected: zero ASan and UBSan errors.

### Commit
`tests: sanitizer stress pass clean — ASan and UBSan on large and high_cancel`

---

## Phase 5 checklist

### Benchmarks
- [ ] All six scenarios run to completion without crash or hang
- [ ] Baseline results recorded in `docs/performance.md`
- [ ] Throughput numbers are stable across runs (< 5% variance with same seed)

### Profiling
- [ ] At least one profiling run completed and top hot paths documented
- [ ] `docs/performance.md` contains profiling methodology section

### Optimisation
- [ ] At least one optimisation applied and benchmarked
- [ ] Before/after numbers recorded with percentage improvement
- [ ] Optimisation is explained in plain English in `docs/performance.md`
- [ ] All existing unit and replay tests still pass after optimisation

### Sanitizers
- [ ] ASan build is clean under `large` and `high_cancel` scenarios
- [ ] UBSan build is clean

### Git
- [ ] Baseline commit precedes optimisation commit (the story is visible in history)

---

---

# Phase 6 — CI/CD, full test coverage, and documentation

**Goal:** The repo looks and behaves like a maintained professional project. Every CI job is green. Every doc is complete.

---

## Part 6.1 — GitHub Actions CI pipeline

### File: `.github/workflows/ci.yml`

```yaml
name: CI

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  build-gcc:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: sudo apt-get install -y cmake ninja-build
      - name: Build (GCC release)
        run: |
          cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_CXX_COMPILER=g++
          cmake --build build
      - name: Test
        run: cd build && ctest --output-on-failure

  build-clang:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: sudo apt-get install -y cmake ninja-build clang
      - name: Build (Clang release)
        run: |
          cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_CXX_COMPILER=clang++
          cmake --build build
      - name: Test
        run: cd build && ctest --output-on-failure

  sanitizers:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build (ASan + UBSan)
        run: |
          cmake -B build_san -G Ninja -DCMAKE_BUILD_TYPE=Debug \
                -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g" \
                -DCMAKE_CXX_COMPILER=clang++
          cmake --build build_san
      - name: Test under sanitizers
        run: cd build_san && ctest --output-on-failure

  format:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install clang-format
        run: sudo apt-get install -y clang-format
      - name: Check formatting
        run: |
          find src tests benchmarks tools -name '*.cpp' -o -name '*.hpp' \
            | xargs clang-format --dry-run --Werror

  static-analysis:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install clang-tidy
        run: sudo apt-get install -y clang-tidy cmake ninja-build
      - name: Build with compile_commands
        run: |
          cmake -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
                -DCMAKE_CXX_COMPILER=clang++
          cmake --build build
      - name: Run clang-tidy
        run: |
          find src -name '*.cpp' \
            | xargs clang-tidy -p build --warnings-as-errors='*'

  replay-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build
        run: cmake -B build -G Ninja && cmake --build build
      - name: Run replay validation
        run: |
          ./build/lob_replay \
            --events tests/replay/data/basic_limit.csv \
            --expected tests/replay/data/basic_limit_expected.csv
          ./build/lob_replay \
            --events tests/replay/data/stop_trigger.csv \
            --expected tests/replay/data/stop_trigger_expected.csv

  benchmark:
    runs-on: ubuntu-latest
    if: github.event_name == 'workflow_dispatch' || github.event.schedule
    steps:
      - uses: actions/checkout@v4
      - name: Build release
        run: |
          cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_CXX_FLAGS="-O2 -march=native"
          cmake --build build
      - name: Run benchmarks
        run: |
          for scenario in small medium large high_cancel market_heavy iceberg_stop; do
            ./build/lob_bench --scenario $scenario --duration 10
          done
```

### Commit
`ci: GitHub Actions — GCC, Clang, sanitizers, format, tidy, replay, benchmark`

---

## Part 6.2 — Documentation files

### File: `docs/engine-design.md`

Sections:
- Matching algorithm (limit and market, with pseudocode)
- Stop order lifecycle (from submission to trigger to execution)
- Iceberg replenishment logic (step by step)
- Price-time priority explanation
- Determinism guarantee (why single-threaded core ensures it)
- Sequence number and replay correctness

### File: `docs/data-structures.md`

Sections:
- `std::map` for price levels: why, trade-offs vs alternatives
- `std::unordered_map` for order ID lookup: why, load factor, reserve strategy
- `std::deque` for level queue: why not `std::list`, why not `std::vector`
- `SpscQueue`: why lock-free, cache-line alignment explanation
- Pending stops as `std::vector`: trade-off vs priority queue, documented as future work

### File: `docs/testing.md`

Sections:
- Unit test strategy
- Replay test methodology
- Invariant definitions (enumerate all invariants checked)
- Sanitizer setup and how to run locally
- How to add a new replay fixture

### File: `docs/performance.md`

Already populated in phase 5. Add section:
- How to reproduce benchmarks (exact cmake flags, exact command)
- Hardware used for the benchmark run (CPU, RAM, OS)

### Commit
`docs: engine-design, data-structures, testing, performance documents complete`

---

## Part 6.3 — Final README

### File: `README.md`

Structure:
```markdown
# lob — high-performance C++ limit order book and matching engine

> [one-line positioning statement]

## What this is
## Why limit order books matter
## Architecture
[ASCII diagram of the data flow]
## Order types supported
[table]
## Matching rules
## Order lifecycle
## Benchmark results
[copy table from docs/performance.md]
## Optimisation notes
[one paragraph summary]
## Replay and snapshot
## Building
## Running tests
## Running benchmarks
## Project structure
## Future work
```

### Commit
`docs: README complete with architecture, benchmarks, build instructions`

---

## Part 6.4 — Final invariant and integration pass

Write one final integration test that:
1. Generates 100,000 synthetic events with seed 42
2. Feeds them through the engine
3. After every 1,000 events, checks all six invariants
4. Asserts zero violations

```cpp
TEST(Integration, InvariantCheckOver100kEvents) {
    MatchingEngine engine;
    SyntheticGenerator gen(default_config(), 42);
    OrderEvent ev;
    uint64_t event_count = 0;

    while (gen.next(ev) && event_count < 100'000) {
        engine.submit(ev);
        ++event_count;
        if (event_count % 1000 == 0) {
            check_all_invariants(engine.book()); // asserts internally
        }
    }
    SUCCEED(); // reached here without assertion failure
}
```

### Commit
`tests: 100k event integration test with invariant checks every 1000 events`

---

## Part 6.5 — Sample replay data

Commit to `data/sample-replay/`:
- `README.md` explaining the CSV format
- `small_market.csv` — 500 events, realistic market scenario
- `stop_scenario.csv` — 200 events designed to trigger all stop types
- `iceberg_scenario.csv` — 200 events with iceberg replenishment visible in output
- Corresponding `*_expected.csv` for each

These serve as documentation, demo data, and regression anchors simultaneously.

### Commit
`data: sample replay datasets for small_market, stop, and iceberg scenarios`

---

## Phase 6 checklist

### CI
- [ ] All CI jobs are green on `main` (GCC, Clang, sanitizers, format, tidy, replay, benchmark trigger)
- [ ] Green badge is visible on README
- [ ] `clang-format` job fails if any file has formatting issues (verified by introducing deliberate formatting error, then fixing)
- [ ] `clang-tidy` job fails on real issues (verified by introducing a flagged pattern, then fixing)

### Documentation
- [ ] `docs/engine-design.md` complete — no `TODO` markers
- [ ] `docs/data-structures.md` complete
- [ ] `docs/testing.md` complete
- [ ] `docs/performance.md` has baseline results, profiling findings, optimisation result
- [ ] `README.md` renders correctly on GitHub — check all links, no broken anchors
- [ ] `data/sample-replay/README.md` explains CSV format

### Tests
- [ ] All unit tests pass in CI (GCC and Clang)
- [ ] All replay tests pass in CI
- [ ] Integration test (100k events + invariants) passes
- [ ] Zero ASan / UBSan errors in sanitizer CI job

### Final repository state
- [ ] No `TODO` or `FIXME` in any `.cpp` or `.hpp` file
- [ ] No commented-out code blocks
- [ ] Every public class and method has a doc comment or is self-explanatory by name
- [ ] `git log --oneline` shows a clean, readable history with one commit per logical unit
- [ ] Repository has a `LICENSE` file (MIT recommended for portfolio)
- [ ] All six benchmark scenarios produce output in the benchmark CI job

---

---

## Appendix: commit message conventions

All commits follow: `scope: description`

Scopes:
- `build` — CMake, toolchain
- `core` — enums, order model, event types, config
- `book` — price level, order book
- `engine` — matching engine, stop, iceberg
- `feed` — CSV reader, synthetic generator
- `risk` — validator
- `logging` — async logger
- `metrics` — histogram, counters
- `snapshot` — serialisation
- `tests` — any test file
- `benchmarks` — benchmark harness and scenarios
- `tools` — CLI binaries
- `ci` — GitHub Actions
- `docs` — documentation files
- `perf` — performance optimisations
- `chore` — maintenance (gitignore, formatting, etc.)
- `data` — sample data files

---

## Appendix: invariants reference

These six invariants must hold after every event in invariant tests:

1. **Volume consistency:** for every price level, `level.total_volume == sum of visible_qty of all orders in level.queue`
2. **Bid ordering:** for all adjacent prices p1, p2 in bids: `p1 > p2` (strictly descending)
3. **Ask ordering:** for all adjacent prices p1, p2 in asks: `p1 < p2` (strictly ascending)
4. **No crossed book:** if both sides non-empty, `best_bid < best_ask`
5. **ID map consistency:** for every order ID in every level queue, that ID exists in `id_map_`; no ID appears in more than one level queue
6. **Stop list exclusivity:** no order ID in `pending_stops_` appears in `bids_` or `asks_`

---

## Appendix: file index (final state)

```
src/core/
  enums.hpp
  order.hpp
  trade.hpp
  event.hpp
  config.hpp
  spsc_queue.hpp
  core.cpp

src/book/
  price_level.hpp  price_level.cpp
  order_book.hpp   order_book.cpp

src/engine/
  matching_engine.hpp  matching_engine.cpp

src/feed/
  csv_replay.hpp   csv_replay.cpp
  synthetic_gen.hpp  synthetic_gen.cpp

src/risk/
  validator.hpp    validator.cpp

src/logging/
  logger.hpp       logger.cpp

src/metrics/
  histogram.hpp
  metrics.hpp      metrics.cpp

src/snapshot/
  snapshot.hpp     snapshot.cpp

tests/unit/
  main_test.cpp
  test_price_level.cpp
  test_order_book.cpp
  test_matching_limit.cpp
  test_matching_market.cpp
  test_cancel.cpp
  test_modify.cpp
  test_replace.cpp
  test_stop_orders.cpp
  test_iceberg.cpp
  test_snapshot.cpp
  test_spsc_queue.cpp
  test_integration.cpp

tests/replay/
  test_replay.cpp
  data/
    basic_limit.csv
    basic_limit_expected.csv
    stop_trigger.csv
    stop_trigger_expected.csv

benchmarks/
  bench_main.cpp
  benchmark_runner.hpp  benchmark_runner.cpp
  scenarios.hpp

tools/
  replay.cpp

data/sample-replay/
  README.md
  small_market.csv
  stop_scenario.csv
  iceberg_scenario.csv
  [expected files]

docs/
  engine-design.md
  data-structures.md
  performance.md
  testing.md

.github/workflows/ci.yml
CMakeLists.txt
.clang-format
.clang-tidy
.gitignore
README.md
LICENSE
```
