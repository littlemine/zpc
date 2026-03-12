#pragma once
/// @file MessageTypes.hpp
/// @brief Defines the core message type enumeration for the network protocol.
///
/// All network messages are identified by a MessageType tag.  The enum is
/// intentionally kept in a header-only protocol layer so that both client
/// and server code can share the same vocabulary without pulling in
/// transport or session dependencies.

#include <cstdint>

namespace zs {

  /// @brief Identifies the semantic type of a network message.
  ///
  /// Values are grouped by function:
  ///   0x00–0x0F  Connection lifecycle
  ///   0x10–0x1F  Session / authentication
  ///   0x20–0x2F  Replication (state sync)
  ///   0x30–0x3F  Input / prediction
  ///   0x40–0x4F  Server control
  ///   0xF0–0xFF  Diagnostics / debug
  enum class MessageType : uint8_t {
    // ── connection lifecycle ───────────────────────────────────────────
    connect_request     = 0x00,
    connect_accept      = 0x01,
    connect_reject      = 0x02,
    disconnect          = 0x03,
    heartbeat           = 0x04,
    heartbeat_ack       = 0x05,

    // ── session / authentication ──────────────────────────────────────
    auth_challenge      = 0x10,
    auth_response       = 0x11,
    session_established = 0x12,
    session_expired     = 0x13,

    // ── replication ───────────────────────────────────────────────────
    state_snapshot      = 0x20,   ///< Full state for a joining client.
    state_delta         = 0x21,   ///< Incremental state update.
    state_ack           = 0x22,   ///< Client acknowledges state tick.
    entity_spawn        = 0x23,
    entity_destroy      = 0x24,

    // ── input / prediction ────────────────────────────────────────────
    client_input        = 0x30,   ///< Input commands from client.
    input_ack           = 0x31,   ///< Server acknowledges processed input.
    correction          = 0x32,   ///< Server-authoritative correction.

    // ── server control ────────────────────────────────────────────────
    server_info         = 0x40,   ///< Server metadata broadcast.
    kick                = 0x41,
    shutdown_notice     = 0x42,

    // ── diagnostics ───────────────────────────────────────────────────
    ping                = 0xF0,
    pong                = 0xF1,
    debug_text          = 0xFE,
  };

  /// @brief Returns a human-readable name for a MessageType.
  inline const char *message_type_name(MessageType t) noexcept {
    switch (t) {
      case MessageType::connect_request:      return "connect_request";
      case MessageType::connect_accept:       return "connect_accept";
      case MessageType::connect_reject:       return "connect_reject";
      case MessageType::disconnect:           return "disconnect";
      case MessageType::heartbeat:            return "heartbeat";
      case MessageType::heartbeat_ack:        return "heartbeat_ack";
      case MessageType::auth_challenge:       return "auth_challenge";
      case MessageType::auth_response:        return "auth_response";
      case MessageType::session_established:  return "session_established";
      case MessageType::session_expired:      return "session_expired";
      case MessageType::state_snapshot:       return "state_snapshot";
      case MessageType::state_delta:          return "state_delta";
      case MessageType::state_ack:            return "state_ack";
      case MessageType::entity_spawn:         return "entity_spawn";
      case MessageType::entity_destroy:       return "entity_destroy";
      case MessageType::client_input:         return "client_input";
      case MessageType::input_ack:            return "input_ack";
      case MessageType::correction:           return "correction";
      case MessageType::server_info:          return "server_info";
      case MessageType::kick:                 return "kick";
      case MessageType::shutdown_notice:      return "shutdown_notice";
      case MessageType::ping:                 return "ping";
      case MessageType::pong:                 return "pong";
      case MessageType::debug_text:           return "debug_text";
      default:                                return "unknown";
    }
  }

}  // namespace zs
