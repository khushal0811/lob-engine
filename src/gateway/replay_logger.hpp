#pragma once
#include "events/exchange_events.hpp"
#include <atomic>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace lob::gateway {

// ---------------------------------------------------------------------------
// ReplayLogger
//
// Writes all exchange events to a newline-delimited JSON (NDJSON) file on a
// dedicated background thread (T4). The exchange thread (T2) calls log() —
// which pushes a serialized JSON string into an SPSC ring buffer — and
// returns immediately.
//
// Format (one object per line):
//   {"ts":1715200000000000000,"type":"TRADE","symbol":"STOCK_7",...}
//
// If the buffer is full, the event is silently dropped. The exchange thread
// never waits on I/O.
// ---------------------------------------------------------------------------
class ReplayLogger {
public:
    explicit ReplayLogger(std::string path, uint32_t queue_capacity = 65536);
    ~ReplayLogger();

    // Non-copyable, non-movable.
    ReplayLogger(const ReplayLogger&) = delete;
    ReplayLogger& operator=(const ReplayLogger&) = delete;

    void start();
    void stop();

    // Enqueue an event for logging. Called from the exchange thread (T2).
    // Non-blocking — drops on full buffer.
    void log(const events::ExchangeEvent& ev) noexcept;

    [[nodiscard]] uint64_t drop_count() const noexcept {
        return drops_.load(std::memory_order_relaxed);
    }

private:
    // Ring buffer stores pre-serialized NDJSON strings to keep the writer
    // thread simple (just write string + newline, no JSON work in T4).
    // Serialization happens in enqueue() on T2 — it is a lightweight
    // string-format operation with no heap alloc for small events.
    //
    // Alternative considered: push ExchangeEvent and format in T4.
    // Decision: format on T2 to keep T4 as a pure I/O thread, and because
    // string formatting is fast compared to file I/O.
    std::vector<std::string> buffer_;
    uint32_t mask_;
    alignas(64) std::atomic<uint32_t> head_{0};
    alignas(64) std::atomic<uint32_t> tail_{0};
    std::atomic<uint64_t> drops_{0};

    std::atomic<bool> running_{false};
    std::thread writer_thread_;
    std::string path_;

    void writer_loop();
};

} // namespace lob::gateway
