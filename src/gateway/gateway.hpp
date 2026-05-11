#pragma once
#include "core/config.hpp"
#include "events/order_message.hpp"
#include "gateway/event_dispatcher.hpp"
#include "gateway/exchange_manager.hpp"
#include "gateway/replay_logger.hpp"
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace lob::gateway {

// ---------------------------------------------------------------------------
// GatewayConfig — runtime configuration for the exchange service.
// ---------------------------------------------------------------------------
struct GatewayConfig {
    std::string pull_endpoint{"tcp://*:5555"}; // ZMQ PULL — inbound orders
    std::string pub_endpoint{"tcp://*:5556"};  // ZMQ PUB  — outbound events
    uint64_t snapshot_interval_ms{100};        // periodic full-depth snapshot
    std::string replay_log_path{"logs/replay.ndjson"};
    EngineConfig engine_config{};
    uint32_t inbound_queue_capacity{65536};
    uint32_t outbound_queue_capacity{65536};
    uint32_t replay_queue_capacity{65536};
};

// ---------------------------------------------------------------------------
// Gateway — top-level coordinator for the exchange service.
//
// Thread model:
//
//   T1  Receiver thread
//       zmq_recv() on PULL socket → deserialize JSON → push to inbound queue
//
//   T2  Exchange thread  (DEDICATED — never touches network/IO)
//       pop inbound queue → ExchangeManager::process() → enqueue events
//       every snapshot_interval_ms: generate and enqueue full-depth snapshots
//
//   T3  Publisher thread  (inside EventDispatcher)
//       pop outbound event queue → serialize JSON → zmq_send() on PUB socket
//
//   T4  Replay logger thread  (inside ReplayLogger)
//       pop replay queue → write NDJSON line to disk
//
// The exchange thread (T2) is the ONLY thread that calls any MatchingEngine
// method. No locks are required on engine state.
// ---------------------------------------------------------------------------
class Gateway {
public:
    explicit Gateway(GatewayConfig config = {});
    ~Gateway();

    // Non-copyable, non-movable.
    Gateway(const Gateway&) = delete;
    Gateway& operator=(const Gateway&) = delete;

    void start();
    void stop();

private:
    GatewayConfig config_;
    ExchangeManager exchange_;
    EventDispatcher dispatcher_;
    ReplayLogger replay_;

    std::atomic<bool> running_{false};
    std::thread receiver_thread_;
    std::thread exchange_thread_;

    void* zmq_ctx_{nullptr};
    void* zmq_pull_{nullptr};

    // Inbound SPSC ring buffer: T1 (receiver) → T2 (exchange)
    std::vector<events::OrderMessage> inbound_buffer_;
    uint32_t inbound_mask_;
    alignas(64) std::atomic<uint32_t> inbound_head_{0};
    alignas(64) std::atomic<uint32_t> inbound_tail_{0};
    std::atomic<uint64_t> inbound_drops_{0};

    void receiver_loop();
    void exchange_loop();

    bool inbound_push(events::OrderMessage msg) noexcept;
    bool inbound_pop(events::OrderMessage& msg) noexcept;
};

} // namespace lob::gateway
