#include "gateway/replay_logger.hpp"
#include "serialization/serializer.hpp"
#include <cassert>

namespace lob::gateway {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ReplayLogger::ReplayLogger(std::string path, uint32_t queue_capacity) : path_(std::move(path)) {
    assert((queue_capacity & (queue_capacity - 1)) == 0 && "capacity must be power of 2");
    buffer_.resize(queue_capacity);
    mask_ = queue_capacity - 1;
}

ReplayLogger::~ReplayLogger() { stop(); }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ReplayLogger::start() {
    running_.store(true, std::memory_order_release);
    writer_thread_ = std::thread(&ReplayLogger::writer_loop, this);
}

void ReplayLogger::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel))
        return;
    if (writer_thread_.joinable())
        writer_thread_.join();
}

// ---------------------------------------------------------------------------
// log() — called from exchange thread (T2), non-blocking
//
// Serializes the event to a JSON string on T2 (fast, avoids variant copy),
// then pushes the string into the ring buffer. The writer thread (T4) only
// performs file I/O.
// ---------------------------------------------------------------------------

void ReplayLogger::log(const events::ExchangeEvent& ev) noexcept {
    const uint32_t tail = tail_.load(std::memory_order_relaxed);
    const uint32_t next_tail = (tail + 1) & mask_;
    if (next_tail == head_.load(std::memory_order_acquire)) {
        drops_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // Serialize here on T2. serialize_event() is a pure string-format
    // operation with no heap alloc for primitive event types.
    buffer_[tail] = serialization::serialize_event(ev);
    tail_.store(next_tail, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Writer loop (T4) — pure I/O, no business logic
// ---------------------------------------------------------------------------

void ReplayLogger::writer_loop() {
    std::ofstream file(path_, std::ios::app);

    auto drain = [&]() {
        while (true) {
            const uint32_t head = head_.load(std::memory_order_relaxed);
            if (head == tail_.load(std::memory_order_acquire))
                break;
            const std::string& line = buffer_[head];
            head_.store((head + 1) & mask_, std::memory_order_release);
            if (!line.empty())
                file << line << '\n';
        }
    };

    while (running_.load(std::memory_order_acquire)) {
        drain();
        std::this_thread::yield();
    }
    // Final drain on shutdown
    drain();
    file.flush();
}

} // namespace lob::gateway
