#include "gateway/gateway.hpp"
#include "serialization/serializer.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <zmq.h>

namespace lob::gateway {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Gateway::Gateway(GatewayConfig config)
    : config_(std::move(config)), exchange_(config_.engine_config),
      dispatcher_(config_.outbound_queue_capacity),
      replay_(config_.replay_log_path, config_.replay_queue_capacity) {
    assert((config_.inbound_queue_capacity & (config_.inbound_queue_capacity - 1)) == 0);
    inbound_buffer_.resize(config_.inbound_queue_capacity);
    inbound_mask_ = config_.inbound_queue_capacity - 1;
}

Gateway::~Gateway() { stop(); }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Gateway::start() {
    // ZMQ context and PULL socket (owned by receiver thread T1)
    zmq_ctx_ = zmq_ctx_new();
    zmq_pull_ = zmq_socket(zmq_ctx_, ZMQ_PULL);

    int rcvhwm = 0;
    zmq_setsockopt(zmq_pull_, ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));

    if (zmq_bind(zmq_pull_, config_.pull_endpoint.c_str()) != 0)
        throw std::runtime_error("Gateway: zmq_bind (PULL) failed on " + config_.pull_endpoint);

    replay_.start();
    dispatcher_.start(config_.pub_endpoint);

    running_.store(true, std::memory_order_release);

    // T2 starts before T1 so the exchange loop is ready before orders arrive.
    exchange_thread_ = std::thread(&Gateway::exchange_loop, this);
    receiver_thread_ = std::thread(&Gateway::receiver_loop, this);

    std::cout << "[lob-exchange] Running\n"
              << "  PULL  : " << config_.pull_endpoint << "\n"
              << "  PUB   : " << config_.pub_endpoint << "\n"
              << "  Snap  : every " << config_.snapshot_interval_ms << " ms\n"
              << "  Replay: " << config_.replay_log_path << "\n";
}

void Gateway::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel))
        return;

    if (receiver_thread_.joinable())
        receiver_thread_.join();
    if (exchange_thread_.joinable())
        exchange_thread_.join();

    dispatcher_.stop();
    replay_.stop();

    if (zmq_pull_) {
        zmq_close(zmq_pull_);
        zmq_pull_ = nullptr;
    }
    if (zmq_ctx_) {
        zmq_ctx_destroy(zmq_ctx_);
        zmq_ctx_ = nullptr;
    }

    std::cout << "[lob-exchange] Stopped. "
              << "inbound_drops=" << inbound_drops_.load()
              << " pub_drops=" << dispatcher_.drop_count()
              << " replay_drops=" << replay_.drop_count() << "\n";
}

// ---------------------------------------------------------------------------
// Inbound SPSC ring buffer helpers (T1 → T2)
// ---------------------------------------------------------------------------

bool Gateway::inbound_push(events::OrderMessage msg) noexcept {
    const uint32_t tail = inbound_tail_.load(std::memory_order_relaxed);
    const uint32_t next_tail = (tail + 1) & inbound_mask_;
    if (next_tail == inbound_head_.load(std::memory_order_acquire)) {
        inbound_drops_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    inbound_buffer_[tail] = std::move(msg);
    inbound_tail_.store(next_tail, std::memory_order_release);
    return true;
}

bool Gateway::inbound_pop(events::OrderMessage& msg) noexcept {
    const uint32_t head = inbound_head_.load(std::memory_order_relaxed);
    if (head == inbound_tail_.load(std::memory_order_acquire))
        return false;
    msg = std::move(inbound_buffer_[head]);
    inbound_head_.store((head + 1) & inbound_mask_, std::memory_order_release);
    return true;
}

// ---------------------------------------------------------------------------
// T1 — Receiver loop
//
// Receives raw JSON from the PULL socket, deserializes it, validates the
// symbol, and pushes the OrderMessage into the inbound SPSC queue.
// Never touches engine state.
// ---------------------------------------------------------------------------

void Gateway::receiver_loop() {
    zmq_msg_t zmsg;
    zmq_msg_init(&zmsg);

    while (running_.load(std::memory_order_acquire)) {
        // Non-blocking recv to allow clean shutdown
        int rc = zmq_msg_recv(&zmsg, zmq_pull_, ZMQ_DONTWAIT);
        if (rc < 0) {
            std::this_thread::yield();
            continue;
        }

        std::string_view raw(static_cast<const char*>(zmq_msg_data(&zmsg)),
                             static_cast<std::size_t>(zmq_msg_size(&zmsg)));

        auto msg_opt = serialization::deserialize_order(raw);
        if (!msg_opt)
            continue;

        if (!exchange_.has_symbol(msg_opt->symbol))
            continue;

        inbound_push(std::move(*msg_opt));
    }

    zmq_msg_close(&zmsg);
}

// ---------------------------------------------------------------------------
// T2 — Exchange loop (DEDICATED — reserved for matching only)
//
// This is the ONLY thread that calls ExchangeManager::process() or
// ExchangeManager::snapshot*(). No networking, no serialization, no logging
// happen on this thread. It only:
//   1. Pops inbound order messages
//   2. Calls ExchangeManager::process()
//   3. Pushes the resulting ExchangeEvents into dispatcher_ and replay_
//   4. On the snapshot interval, generates and enqueues full-depth snapshots
// ---------------------------------------------------------------------------

void Gateway::exchange_loop() {
    using clock = std::chrono::steady_clock;
    using duration = std::chrono::milliseconds;

    const duration snap_interval{config_.snapshot_interval_ms};
    auto next_snapshot = clock::now() + snap_interval;

    events::OrderMessage msg;

    while (running_.load(std::memory_order_acquire)) {
        while (inbound_pop(msg)) {
            auto evs = exchange_.process(msg);
            for (const auto& ev : evs)
                replay_.log(ev);
            dispatcher_.enqueue(std::move(evs));
        }

        auto now = clock::now();
        if (now >= next_snapshot) {
            auto snaps = exchange_.snapshot_all();
            for (const auto& snap : snaps) {
                replay_.log(snap);
                dispatcher_.enqueue(snap);
            }
            next_snapshot = now + snap_interval;
        }

        std::this_thread::yield();
    }

    // Final drain — process any orders that arrived before the flag flipped
    while (inbound_pop(msg)) {
        auto evs = exchange_.process(msg);
        for (const auto& ev : evs)
            replay_.log(ev);
        dispatcher_.enqueue(std::move(evs));
    }
}

} // namespace lob::gateway
