#pragma once
#include "events/exchange_events.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace lob::gateway {

// ---------------------------------------------------------------------------
// EventDispatcher
//
// Bridges the exchange thread (T2) and the publisher thread (T3).
//
// The exchange thread calls enqueue() after every match — non-blocking.
// The publisher thread pops events, serializes them to JSON, and sends them
// over the ZeroMQ PUB socket. Serialization never touches the exchange thread.
//
// If the internal ring buffer is full, the event is silently dropped and
// the atomic drop counter is incremented. The exchange thread never waits.
// ---------------------------------------------------------------------------
class EventDispatcher {
public:
    explicit EventDispatcher(uint32_t queue_capacity = 65536);
    ~EventDispatcher();

    // Non-copyable, non-movable.
    EventDispatcher(const EventDispatcher&)            = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    // Bind the PUB socket and start the publisher thread.
    // Must be called before any enqueue().
    void start(const std::string& pub_endpoint);

    // Stop the publisher thread and close the ZMQ socket.
    void stop();

    // Enqueue a single event. Called from the exchange thread (T2).
    // Non-blocking — drops if the buffer is full.
    void enqueue(events::ExchangeEvent ev) noexcept;

    // Convenience: enqueue a batch.
    void enqueue(std::vector<events::ExchangeEvent> evs) noexcept;

    [[nodiscard]] uint64_t drop_count() const noexcept {
        return drops_.load(std::memory_order_relaxed);
    }

private:
    // Dynamic SPSC ring buffer — same pattern as lob::Logger.
    std::vector<events::ExchangeEvent> buffer_;
    uint32_t                           mask_;
    alignas(64) std::atomic<uint32_t>  head_{0};
    alignas(64) std::atomic<uint32_t>  tail_{0};
    std::atomic<uint64_t>              drops_{0};

    std::atomic<bool> running_{false};
    std::thread       pub_thread_;
    void*             zmq_ctx_{nullptr};
    void*             zmq_pub_{nullptr};

    void publisher_loop();
};

} // namespace lob::gateway
