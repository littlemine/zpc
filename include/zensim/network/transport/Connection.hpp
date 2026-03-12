#pragma once
/// @file Connection.hpp
/// @brief Abstract connection interface for network transports.
///
/// A Connection represents a logical link between two endpoints.  It
/// knows how to send and receive framed messages (MessageHeader + payload).
/// Concrete implementations: TcpConnection, UdpPeer.

#include <cstdint>
#include <vector>
#include <functional>

#include "zensim/network/protocol/MessageHeader.hpp"
#include "zensim/network/protocol/Serialization.hpp"
#include "zensim/network/transport/Address.hpp"

namespace zs {

  /// @brief Connection state machine.
  enum class ConnectionState : uint8_t {
    disconnected = 0,
    connecting,
    connected,
    disconnecting,
  };

  /// @brief Statistics tracked per connection.
  struct ConnectionStats {
    uint64_t bytes_sent      = 0;
    uint64_t bytes_received  = 0;
    uint64_t packets_sent    = 0;
    uint64_t packets_received = 0;
    uint64_t packets_lost    = 0;
    float    rtt_ms          = 0.0f;  ///< Smoothed round-trip time.
    float    jitter_ms       = 0.0f;  ///< RTT variance.
  };

  /// @brief A received message: header + owned payload bytes.
  struct IncomingMessage {
    MessageHeader          header;
    std::vector<uint8_t>   payload;

    /// Convenience: get a ReadBuffer over the payload.
    ReadBuffer reader() const noexcept {
      return ReadBuffer{payload.data(), payload.size()};
    }
  };

  /// @brief Abstract interface for a network connection.
  ///
  /// Implementations must provide send/receive for framed messages.
  /// The connection is non-copyable; callers hold it via unique_ptr or
  /// by value in a container.
  class Connection {
  public:
    virtual ~Connection() = default;

    /// Current state of this connection.
    virtual ConnectionState state() const noexcept = 0;

    /// Remote endpoint address.
    virtual NetworkAddress remote_address() const noexcept = 0;

    /// Accumulated statistics.
    virtual const ConnectionStats &stats() const noexcept = 0;

    /// Send a message (header is built from type/flags/payload size).
    /// @return true on success.
    virtual bool send_message(MessageType type, MessageFlags flags,
                              const void *payload, size_t payload_size) = 0;

    /// Convenience: send with WriteBuffer payload.
    bool send_message(MessageType type, MessageFlags flags,
                      const WriteBuffer &payload) {
      return send_message(type, flags, payload.data(), payload.size());
    }

    /// Convenience: send with no payload.
    bool send_message(MessageType type,
                      MessageFlags flags = MessageFlags::none) {
      return send_message(type, flags, nullptr, 0);
    }

    /// Poll for an incoming message.
    /// @return true if a message was received and written to `out`.
    virtual bool recv_message(IncomingMessage &out) = 0;

    /// Gracefully close the connection.
    virtual void disconnect() = 0;

  protected:
    Connection() = default;
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    Connection(Connection &&) = default;
    Connection &operator=(Connection &&) = default;
  };

}  // namespace zs
