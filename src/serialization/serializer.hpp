#pragma once
#include "events/exchange_events.hpp"
#include "events/order_message.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace lob::serialization {

// ---------------------------------------------------------------------------
// Inbound: parse a JSON string from an external client into an OrderMessage.
// Returns std::nullopt on any parse or validation error — never throws.
// ---------------------------------------------------------------------------
std::optional<events::OrderMessage> deserialize_order(std::string_view json);

// ---------------------------------------------------------------------------
// Outbound: serialize an ExchangeEvent into a ZeroMQ-ready message string.
//
// Format:  "<TOPIC> <json_payload>"
//
// Topics:
//   ACCEPTED  — OrderAcceptedEvent
//   REJECTED  — OrderRejectedEvent
//   CANCELED  — OrderCanceledEvent
//   TRADE     — TradeExecutedEvent
//   BOOK      — BookUpdatedEvent
//   SNAPSHOT  — SnapshotEvent
//
// Clients subscribed with zmq_setsockopt(SUB, ZMQ_SUBSCRIBE, "TRADE", 5)
// will receive only trade events, etc.
// ---------------------------------------------------------------------------
std::string serialize_event(const events::ExchangeEvent& ev);

} // namespace lob::serialization
