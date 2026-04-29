#include "logging/logger.hpp"
#include <iostream>

namespace lob {

Logger::Logger(std::string path, uint32_t queue_size)
    : path_(std::move(path)) {
    // Round up to next power of 2
    uint32_t n = 1;
    while (n < queue_size) n <<= 1;
    buffer_.resize(n);
    mask_ = n - 1;
}

Logger::~Logger() {
    stop();
}

void Logger::start() {
    if (running_.load()) return;
    running_.store(true);
    writer_thread_ = std::thread(&Logger::writer_loop, this);
}

void Logger::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
}

void Logger::log(Level level, std::string_view category, std::string_view message) {
    uint32_t tail = tail_.load(std::memory_order_relaxed);
    uint32_t next_tail = (tail + 1) & mask_;
    if (next_tail == head_.load(std::memory_order_acquire)) {
        // Queue full — drop
        drops_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    auto& record = buffer_[tail];
    record.level = level;
    record.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());

    std::memset(record.category, 0, sizeof(record.category));
    std::memcpy(record.category, category.data(),
                std::min(category.size(), sizeof(record.category) - 1));

    std::memset(record.message, 0, sizeof(record.message));
    std::memcpy(record.message, message.data(),
                std::min(message.size(), sizeof(record.message) - 1));

    tail_.store(next_tail, std::memory_order_release);
}

void Logger::writer_loop() {
    std::ofstream out(path_, std::ios::app);

    while (running_.load(std::memory_order_relaxed) ||
           head_.load(std::memory_order_acquire) !=
               tail_.load(std::memory_order_acquire)) {
        uint32_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }

        const auto& record = buffer_[head];
        out << "{\"ts\":" << record.timestamp
            << ",\"level\":\"" << level_str(record.level)
            << "\",\"cat\":\"" << record.category
            << "\",\"msg\":\"" << record.message
            << "\"}\n";

        head_.store((head + 1) & mask_, std::memory_order_release);
    }

    out.flush();
}

} // namespace lob
