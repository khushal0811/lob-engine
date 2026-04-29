#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace lob {

class Logger {
public:
    enum class Level : uint8_t { Debug, Info, Warn, Error };

    explicit Logger(std::string path, uint32_t queue_size = 65536);
    ~Logger();

    // Non-blocking log — drops if queue is full
    void log(Level level, std::string_view category, std::string_view message);

    [[nodiscard]] uint64_t drop_count() const noexcept {
        return drops_.load(std::memory_order_relaxed);
    }

    void start();
    void stop();

private:
    struct LogRecord {
        Level level;
        char category[16];
        char message[256];
        uint64_t timestamp;
    };

    std::vector<LogRecord> buffer_;
    uint32_t mask_;
    alignas(64) std::atomic<uint32_t> head_{0};
    alignas(64) std::atomic<uint32_t> tail_{0};
    std::atomic<uint64_t> drops_{0};
    std::atomic<bool> running_{false};
    std::thread writer_thread_;
    std::string path_;

    void writer_loop();

    static const char* level_str(Level l) {
        switch (l) {
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO";
        case Level::Warn:
            return "WARN";
        case Level::Error:
            return "ERROR";
        }
        return "UNKNOWN";
    }
};

} // namespace lob
