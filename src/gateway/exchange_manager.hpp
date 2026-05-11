#pragma once
#include "core/config.hpp"
#include "engine/matching_engine.hpp"
#include "events/exchange_events.hpp"
#include "events/order_message.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lob::gateway {

// ---------------------------------------------------------------------------
// Number of instruments managed by the exchange.
// Symbols are STOCK_1 … STOCK_25.
// ---------------------------------------------------------------------------
inline constexpr std::size_t kInstrumentCount = 25;

// ---------------------------------------------------------------------------
// ExchangeManager
//
// Owns one MatchingEngine per instrument. Routes incoming OrderMessages to
// the correct engine by symbol, translates the resulting MatchResult into
// standardized ExchangeEvents, and generates periodic book snapshots.
//
// THREAD SAFETY:
//   All public methods MUST be called exclusively from the exchange thread
//   (T2). No locking is performed — isolation is enforced by convention.
// ---------------------------------------------------------------------------
class ExchangeManager {
public:
    explicit ExchangeManager(EngineConfig cfg = {});

    // Returns true if the symbol is one of the 25 supported instruments.
    [[nodiscard]] bool has_symbol(const std::string& sym) const noexcept;

    // Route an order to the correct engine. Translates the resulting
    // MatchResult into a vector of ExchangeEvents.
    std::vector<events::ExchangeEvent> process(const events::OrderMessage& msg);

    // Build a full depth snapshot for a single symbol.
    // Called from the exchange thread on the periodic snapshot interval.
    events::SnapshotEvent snapshot(const std::string& symbol) const;

    // Build snapshots for all 25 instruments in one call.
    std::vector<events::SnapshotEvent> snapshot_all() const;

private:
    struct Instrument {
        std::string symbol;
        MatchingEngine engine;

        Instrument(std::string sym, EngineConfig cfg)
            : symbol(std::move(sym)), engine(std::move(cfg)) {}

        // Non-copyable, movable.
        Instrument(const Instrument&) = delete;
        Instrument& operator=(const Instrument&) = delete;
        Instrument(Instrument&&) = default;
        Instrument& operator=(Instrument&&) = default;
    };

    std::vector<Instrument> instruments_;
    std::unordered_map<std::string, std::size_t> index_;

    // Translate MatchResult → ExchangeEvents.
    // book is the engine's book after matching (for BookUpdatedEvent).
    std::vector<events::ExchangeEvent> translate_result(const events::OrderMessage& msg,
                                                        const MatchResult& res,
                                                        const OrderBook& book) const;

    static uint64_t now_ns() noexcept;
};

} // namespace lob::gateway
