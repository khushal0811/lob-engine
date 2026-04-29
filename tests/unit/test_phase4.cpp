#include "core/spsc_queue.hpp"
#include "engine/matching_engine.hpp"
#include "feed/csv_replay.hpp"
#include "feed/synthetic_gen.hpp"
#include "logging/logger.hpp"
#include "metrics/histogram.hpp"
#include "metrics/metrics.hpp"
#include "snapshot/snapshot.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

using namespace lob;

// ============ SPSC Queue Tests ============

TEST(SpscQueue, PushPopSingleItem) {
    SpscQueue<int, 4> q;
    EXPECT_TRUE(q.empty());
    EXPECT_TRUE(q.push(42));
    EXPECT_FALSE(q.empty());
    int val = 0;
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(q.empty());
}

TEST(SpscQueue, PopEmptyReturnsFalse) {
    SpscQueue<int, 4> q;
    int val = 0;
    EXPECT_FALSE(q.pop(val));
}

TEST(SpscQueue, PushFullReturnsFalse) {
    SpscQueue<int, 4> q; // capacity 4 means 3 usable slots (ring buffer)
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));
    EXPECT_FALSE(q.push(4)); // full
}

TEST(SpscQueue, FifoOrder) {
    SpscQueue<int, 8> q;
    for (int i = 0; i < 7; ++i)
        q.push(i);
    for (int i = 0; i < 7; ++i) {
        int val = -1;
        EXPECT_TRUE(q.pop(val));
        EXPECT_EQ(val, i);
    }
}

// ============ CSV Replay Reader Tests ============

TEST(CsvReplayReader, ReadsLimitOrders) {
    // Use the basic_limit fixture
    std::string path = "tests/replay/data/basic_limit.csv";
    if (!std::filesystem::exists(path)) {
        path = "../tests/replay/data/basic_limit.csv";
    }
    CsvReplayReader reader(path);
    OrderEvent event;
    int count = 0;
    while (reader.next(event))
        ++count;
    EXPECT_EQ(count, 20);
    EXPECT_EQ(reader.error_count(), 0u);
}

TEST(CsvReplayReader, SkipsMalformedLines) {
    // Create a temp file with bad lines
    std::string path = "test_malformed.csv";
    {
        std::ofstream f(path);
        f << "event_type,order_id,side,order_type,price,quantity,peak_qty,stop_price,timestamp\n";
        f << "NEW,1,BUY,LIMIT,100,50,0,0,1700000000000000000\n";
        f << "GARBAGE_LINE\n";
        f << "NEW,2,SELL,LIMIT,100,50,0,0,1700000000000001000\n";
    }
    CsvReplayReader reader(path);
    OrderEvent event;
    int count = 0;
    while (reader.next(event))
        ++count;
    EXPECT_EQ(count, 2);
    EXPECT_EQ(reader.error_count(), 1u);
    std::filesystem::remove(path);
}

// ============ Synthetic Generator Tests ============

TEST(SyntheticGenerator, FixedSeedProducesIdenticalSequence) {
    EngineConfig cfg;
    cfg.cancel_probability = 0.3;
    SyntheticGenerator gen1(cfg, 42, 100);
    SyntheticGenerator gen2(cfg, 42, 100);

    for (int i = 0; i < 100; ++i) {
        OrderEvent e1, e2;
        ASSERT_TRUE(gen1.next(e1));
        ASSERT_TRUE(gen2.next(e2));
        EXPECT_EQ(e1.index(), e2.index());
    }
}

TEST(SyntheticGenerator, ZeroCancelProbabilityProducesOnlyNewOrders) {
    EngineConfig cfg;
    cfg.cancel_probability = 0.0;
    SyntheticGenerator gen(cfg, 42, 50);
    OrderEvent event;
    while (gen.next(event)) {
        EXPECT_TRUE(std::holds_alternative<NewOrderEvent>(event));
    }
}

TEST(SyntheticGenerator, RespectsMaxEvents) {
    EngineConfig cfg;
    SyntheticGenerator gen(cfg, 42, 10);
    OrderEvent event;
    int count = 0;
    while (gen.next(event))
        ++count;
    EXPECT_EQ(count, 10);
}

// ============ Logger Tests ============

TEST(Logger, WritesJsonLines) {
    std::string path = "test_logger_output.jsonl";
    {
        Logger logger(path, 64);
        logger.start();
        logger.log(Logger::Level::Info, "TRADE", "test message");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        logger.stop();
    }

    std::ifstream f(path);
    std::string line;
    ASSERT_TRUE(std::getline(f, line));
    EXPECT_NE(line.find("\"level\":\"INFO\""), std::string::npos);
    EXPECT_NE(line.find("\"cat\":\"TRADE\""), std::string::npos);
    EXPECT_NE(line.find("\"msg\":\"test message\""), std::string::npos);
    std::filesystem::remove(path);
}

TEST(Logger, DropsOnFull) {
    std::string path = "test_logger_drops.jsonl";
    {
        Logger logger(path, 4); // very small buffer
        // Don't start the writer thread — so nothing drains
        for (int i = 0; i < 100; ++i) {
            logger.log(Logger::Level::Debug, "TEST", "msg");
        }
        EXPECT_GT(logger.drop_count(), 0u);
    }
    std::filesystem::remove(path);
}

TEST(Logger, JoinsCleanlyOnDestruction) {
    std::string path = "test_logger_join.jsonl";
    {
        Logger logger(path, 64);
        logger.start();
        logger.log(Logger::Level::Info, "TEST", "hello");
    } // destructor should join without hanging
    std::filesystem::remove(path);
    SUCCEED(); // if we get here, join worked
}

// ============ Histogram Tests ============

TEST(LatencyHistogram, RecordAndPercentile) {
    LatencyHistogram h;
    for (uint64_t i = 1; i <= 100; ++i)
        h.record(i);
    EXPECT_EQ(h.count(), 100u);
    EXPECT_EQ(h.max_val(), 100u);
    EXPECT_GE(h.p50(), 49u);
    EXPECT_LE(h.p50(), 51u);
}

TEST(LatencyHistogram, Reset) {
    LatencyHistogram h;
    h.record(42);
    h.reset();
    EXPECT_EQ(h.count(), 0u);
    EXPECT_EQ(h.max_val(), 0u);
}

// ============ Metrics Tests ============

TEST(Metrics, CountersIncrement) {
    Metrics m;
    m.record_trade();
    m.record_trade();
    m.record_rejection();
    m.record_cancel();
    EXPECT_EQ(m.trade_count(), 2u);
    EXPECT_EQ(m.rejection_count(), 1u);
    EXPECT_EQ(m.cancel_count(), 1u);
}

// ============ Snapshot Tests ============

TEST(Snapshot, RoundTripPreservesState) {
    std::string path = "test_snapshot.bin";
    OrderBook original_book;

    // Seed some orders
    Order buy;
    buy.id = 1;
    buy.side = Side::Buy;
    buy.type = OrderType::Limit;
    buy.price = 100;
    buy.quantity = 50;
    buy.orig_qty = 50;
    buy.status = OrderStatus::Resting;
    original_book.insert_order(buy);

    Order sell;
    sell.id = 2;
    sell.side = Side::Sell;
    sell.type = OrderType::Limit;
    sell.price = 105;
    sell.quantity = 30;
    sell.orig_qty = 30;
    sell.status = OrderStatus::Resting;
    original_book.insert_order(sell);

    original_book.last_trade_price = 102;

    std::vector<Order> original_stops;
    Order stop;
    stop.id = 3;
    stop.side = Side::Buy;
    stop.type = OrderType::Stop;
    stop.stop_price = 110;
    stop.quantity = 20;
    original_stops.push_back(stop);

    // Save
    SnapshotManager mgr(path);
    ASSERT_TRUE(mgr.save(original_book, original_stops, 42));

    // Load
    OrderBook loaded_book;
    std::vector<Order> loaded_stops;
    uint64_t seq = 0;
    ASSERT_TRUE(mgr.load(loaded_book, loaded_stops, seq));

    EXPECT_EQ(seq, 42u);
    EXPECT_EQ(loaded_book.last_trade_price, 102);
    EXPECT_EQ(loaded_book.order_count(), 2u);
    EXPECT_TRUE(loaded_book.has_order(1));
    EXPECT_TRUE(loaded_book.has_order(2));
    EXPECT_EQ(*loaded_book.best_bid(), 100);
    EXPECT_EQ(*loaded_book.best_ask(), 105);
    ASSERT_EQ(loaded_stops.size(), 1u);
    EXPECT_EQ(loaded_stops[0].id, 3u);
    EXPECT_EQ(loaded_stops[0].stop_price, 110);

    std::filesystem::remove(path);
}

// ============ Replay Determinism Tests ============

static std::vector<Trade> run_replay(const std::string& path) {
    CsvReplayReader reader(path);
    MatchingEngine engine;
    std::vector<Trade> all_trades;
    OrderEvent event;
    while (reader.next(event)) {
        auto result = engine.submit(event);
        all_trades.insert(all_trades.end(), result.trades.begin(), result.trades.end());
    }
    return all_trades;
}

TEST(Replay, BasicLimitDeterministic) {
    std::string path = "tests/replay/data/basic_limit.csv";
    if (!std::filesystem::exists(path)) {
        path = "../tests/replay/data/basic_limit.csv";
    }
    auto output1 = run_replay(path);
    auto output2 = run_replay(path);
    ASSERT_EQ(output1.size(), output2.size());
    for (size_t i = 0; i < output1.size(); ++i) {
        EXPECT_EQ(output1[i].price, output2[i].price);
        EXPECT_EQ(output1[i].quantity, output2[i].quantity);
        EXPECT_EQ(output1[i].aggressor_id, output2[i].aggressor_id);
        EXPECT_EQ(output1[i].passive_id, output2[i].passive_id);
    }
}

TEST(Replay, BasicLimitProducesTrades) {
    std::string path = "tests/replay/data/basic_limit.csv";
    if (!std::filesystem::exists(path)) {
        path = "../tests/replay/data/basic_limit.csv";
    }
    auto trades = run_replay(path);
    EXPECT_GT(trades.size(), 0u);
}

TEST(Replay, StopTriggerDeterministic) {
    std::string path = "tests/replay/data/stop_trigger.csv";
    if (!std::filesystem::exists(path)) {
        path = "../tests/replay/data/stop_trigger.csv";
    }
    auto output1 = run_replay(path);
    auto output2 = run_replay(path);
    ASSERT_EQ(output1.size(), output2.size());
    for (size_t i = 0; i < output1.size(); ++i) {
        EXPECT_EQ(output1[i].price, output2[i].price);
        EXPECT_EQ(output1[i].quantity, output2[i].quantity);
    }
}
