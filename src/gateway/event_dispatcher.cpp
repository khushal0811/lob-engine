#include "gateway/event_dispatcher.hpp"
#include "serialization/serializer.hpp"
#include <cassert>
#include <zmq.h>

namespace lob::gateway {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

EventDispatcher::EventDispatcher(uint32_t queue_capacity) {
    assert((queue_capacity & (queue_capacity - 1)) == 0 && "capacity must be power of 2");
    buffer_.resize(queue_capacity);
    mask_ = queue_capacity - 1;
}

EventDispatcher::~EventDispatcher() { stop(); }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void EventDispatcher::start(const std::string& pub_endpoint) {
    zmq_ctx_ = zmq_ctx_new();
    zmq_pub_ = zmq_socket(zmq_ctx_, ZMQ_PUB);

    int sndhwm = 0; // unlimited send HWM — drop policy enforced in our ring buffer
    zmq_setsockopt(zmq_pub_, ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));

    if (zmq_bind(zmq_pub_, pub_endpoint.c_str()) != 0)
        throw std::runtime_error("EventDispatcher: zmq_bind failed on " + pub_endpoint);

    running_.store(true, std::memory_order_release);
    pub_thread_ = std::thread(&EventDispatcher::publisher_loop, this);
}

void EventDispatcher::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel))
        return;

    if (pub_thread_.joinable())
        pub_thread_.join();

    if (zmq_pub_) {
        zmq_close(zmq_pub_);
        zmq_pub_ = nullptr;
    }
    if (zmq_ctx_) {
        zmq_ctx_destroy(zmq_ctx_);
        zmq_ctx_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Enqueue (called from exchange thread T2 — must never block)
// ---------------------------------------------------------------------------

void EventDispatcher::enqueue(events::ExchangeEvent ev) noexcept {
    const uint32_t tail = tail_.load(std::memory_order_relaxed);
    const uint32_t next_tail = (tail + 1) & mask_;
    if (next_tail == head_.load(std::memory_order_acquire)) {
        drops_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    buffer_[tail] = std::move(ev);
    tail_.store(next_tail, std::memory_order_release);
}

void EventDispatcher::enqueue(std::vector<events::ExchangeEvent> evs) noexcept {
    for (auto& ev : evs)
        enqueue(std::move(ev));
}

// ---------------------------------------------------------------------------
// Publisher loop (T3) — serializes and sends, never touches engine state
// ---------------------------------------------------------------------------

void EventDispatcher::publisher_loop() {
    while (running_.load(std::memory_order_acquire)) {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            // Empty — yield briefly to avoid spinning at 100 % CPU
            std::this_thread::yield();
            continue;
        }

        // Serialization happens here, off the exchange thread
        std::string msg = serialization::serialize_event(buffer_[head]);
        head_.store((head + 1) & mask_, std::memory_order_release);

        if (!msg.empty()) {
            zmq_send(zmq_pub_, msg.data(), msg.size(), ZMQ_DONTWAIT);
        }
    }

    // Drain remaining events before exit
    while (true) {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire))
            break;
        std::string msg = serialization::serialize_event(buffer_[head]);
        head_.store((head + 1) & mask_, std::memory_order_release);
        if (!msg.empty())
            zmq_send(zmq_pub_, msg.data(), msg.size(), ZMQ_DONTWAIT);
    }
}

} // namespace lob::gateway
