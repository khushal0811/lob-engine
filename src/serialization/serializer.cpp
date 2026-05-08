#include "serialization/serializer.hpp"
#include <nlohmann/json.hpp>

namespace lob::serialization {

using json = nlohmann::json;

static const char* side_to_str(Side s) noexcept {
    return s == Side::Buy ? "BUY" : "SELL";
}

static const char* order_type_to_str(OrderType t) noexcept {
    switch (t) {
    case OrderType::Limit:     return "LIMIT";
    case OrderType::Market:    return "MARKET";
    case OrderType::Stop:      return "STOP";
    case OrderType::StopLimit: return "STOP_LIMIT";
    case OrderType::Iceberg:   return "ICEBERG";
    }
    return "UNKNOWN";
}

static const char* reject_reason_to_str(RejectReason r) noexcept {
    switch (r) {
    case RejectReason::InvalidPrice:           return "INVALID_PRICE";
    case RejectReason::InvalidQuantity:        return "INVALID_QUANTITY";
    case RejectReason::DuplicateOrderId:       return "DUPLICATE_ORDER_ID";
    case RejectReason::OrderNotFound:          return "ORDER_NOT_FOUND";
    case RejectReason::OrderNotCancellable:    return "ORDER_NOT_CANCELLABLE";
    case RejectReason::InvalidModify:          return "INVALID_MODIFY";
    case RejectReason::StopPriceInconsistent:  return "STOP_PRICE_INCONSISTENT";
    case RejectReason::PriceBandViolation:     return "PRICE_BAND_VIOLATION";
    case RejectReason::MaxSizeViolation:       return "MAX_SIZE_VIOLATION";
    case RejectReason::MarketExhausted:        return "MARKET_EXHAUSTED";
    case RejectReason::None:                   return "NONE";
    }
    return "NONE";
}

static Side parse_side(const std::string& s) {
    if (s == "BUY")  return Side::Buy;
    if (s == "SELL") return Side::Sell;
    throw std::invalid_argument("invalid side: " + s);
}

static OrderType parse_order_type(const std::string& s) {
    if (s == "LIMIT")      return OrderType::Limit;
    if (s == "MARKET")     return OrderType::Market;
    if (s == "STOP")       return OrderType::Stop;
    if (s == "STOP_LIMIT") return OrderType::StopLimit;
    if (s == "ICEBERG")    return OrderType::Iceberg;
    throw std::invalid_argument("invalid type: " + s);
}

static events::OrderAction parse_action(const std::string& s) {
    if (s == "NEW_ORDER")    return events::OrderAction::NewOrder;
    if (s == "CANCEL_ORDER") return events::OrderAction::CancelOrder;
    throw std::invalid_argument("invalid action: " + s);
}

// ---------------------------------------------------------------------------
// Deserialization
// ---------------------------------------------------------------------------

std::optional<events::OrderMessage> deserialize_order(std::string_view json_str) {
    try {
        auto j = json::parse(json_str);

        events::OrderMessage msg;
        msg.order_id   = j.at("order_id").get<uint64_t>();
        msg.client_id  = j.at("client_id").get<uint64_t>();
        msg.symbol     = j.at("symbol").get<std::string>();
        msg.quantity   = j.at("quantity").get<uint64_t>();
        msg.price      = j.value("price",      int64_t{0});
        msg.stop_price = j.value("stop_price", int64_t{0});
        msg.peak_qty   = j.value("peak_qty",   uint64_t{0});
        msg.timestamp  = j.value("timestamp",  uint64_t{0});
        msg.action     = parse_action(j.at("action").get<std::string>());
        msg.side       = parse_side(j.at("side").get<std::string>());
        msg.type       = parse_order_type(j.at("type").get<std::string>());

        return msg;
    } catch (...) {
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

std::string serialize_event(const events::ExchangeEvent& ev) {
    return std::visit(
        [](const auto& e) -> std::string {
            using T = std::decay_t<decltype(e)>;

            if constexpr (std::is_same_v<T, events::TradeExecutedEvent>) {
                json j{{"ts",      e.timestamp},
                       {"type",    "TRADE"},
                       {"symbol",  e.symbol},
                       {"buy_id",  e.buy_order_id},
                       {"sell_id", e.sell_order_id},
                       {"price",   e.price},
                       {"qty",     e.quantity}};
                return "TRADE " + j.dump();

            } else if constexpr (std::is_same_v<T, events::BookUpdatedEvent>) {
                json j{{"ts",         e.timestamp},
                       {"type",       "BOOK"},
                       {"symbol",     e.symbol},
                       {"best_bid",   e.best_bid},
                       {"best_ask",   e.best_ask},
                       {"spread",     e.spread},
                       {"bid_levels", e.bid_levels},
                       {"ask_levels", e.ask_levels}};
                return "BOOK " + j.dump();

            } else if constexpr (std::is_same_v<T, events::SnapshotEvent>) {
                json bids = json::array();
                for (const auto& [px, qty] : e.bids)
                    bids.push_back({{"price", px}, {"qty", qty}});
                json asks = json::array();
                for (const auto& [px, qty] : e.asks)
                    asks.push_back({{"price", px}, {"qty", qty}});
                json j{{"ts",                e.timestamp},
                       {"type",              "SNAPSHOT"},
                       {"symbol",            e.symbol},
                       {"last_trade_price",  e.last_trade_price},
                       {"best_bid",          e.best_bid},
                       {"best_ask",          e.best_ask},
                       {"bids",              bids},
                       {"asks",              asks}};
                return "SNAPSHOT " + j.dump();

            } else if constexpr (std::is_same_v<T, events::OrderAcceptedEvent>) {
                json j{{"ts",       e.timestamp},
                       {"type",     "ACCEPTED"},
                       {"symbol",   e.symbol},
                       {"order_id", e.order_id},
                       {"cid",      e.client_id},
                       {"price",    e.price},
                       {"qty",      e.quantity},
                       {"side",     side_to_str(e.side)},
                       {"otype",    order_type_to_str(e.type)}};
                return "ACCEPTED " + j.dump();

            } else if constexpr (std::is_same_v<T, events::OrderRejectedEvent>) {
                json j{{"ts",       e.timestamp},
                       {"type",     "REJECTED"},
                       {"symbol",   e.symbol},
                       {"order_id", e.order_id},
                       {"cid",      e.client_id},
                       {"reason",   reject_reason_to_str(e.reason)}};
                return "REJECTED " + j.dump();

            } else if constexpr (std::is_same_v<T, events::OrderCanceledEvent>) {
                json j{{"ts",            e.timestamp},
                       {"type",          "CANCELED"},
                       {"symbol",        e.symbol},
                       {"order_id",      e.order_id},
                       {"remaining_qty", e.remaining_qty}};
                return "CANCELED " + j.dump();
            }

            return {};
        },
        ev);
}

} // namespace lob::serialization
