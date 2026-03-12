#pragma once
/// @file UdpTransport.hpp
/// @brief UDP transport — connectionless, unreliable datagram socket.
///
/// UdpSocket wraps a bound UDP socket for sending and receiving
/// datagrams.  Each datagram is a complete [MessageHeader + payload].
/// No reliability, ordering, or congestion control at this layer.
///
/// Higher layers (session, replication) add selective ACKs and
/// retransmission when needed.

#include <cstdint>
#include <vector>

#include "zensim/network/protocol/MessageHeader.hpp"
#include "zensim/network/transport/Address.hpp"
#include "zensim/network/transport/Socket.hpp"

namespace zs {

  /// @brief Maximum safe UDP payload (avoid IP fragmentation).
  static constexpr size_t kMaxUdpPayload = 1200;

  /// @brief A received UDP datagram with sender address.
  struct UdpDatagram {
    NetworkAddress         sender;
    MessageHeader          header;
    std::vector<uint8_t>   payload;
  };

  /// @brief UDP socket for sending and receiving datagrams.
  ///
  /// Bind once, then sendto/recvfrom any number of peers.
  /// Each datagram is a self-contained [8-byte header + payload].
  class UdpSocket {
  public:
    UdpSocket() = default;

    /// Bind to a local address.  Returns false on failure.
    bool bind(const NetworkAddress &local_addr) {
      _socket = Socket::create(SocketProtocol::udp, local_addr.family());
      if (!_socket) return false;
      _socket.set_reuse_addr();
      if (!_socket.bind(local_addr)) {
        _socket.close();
        return false;
      }
      _bound = true;
      return true;
    }

    /// Send a framed message to a destination.
    bool send_message(const NetworkAddress &dest, MessageType type,
                      MessageFlags flags,
                      const void *payload, size_t payload_size) {
      if (!_bound) return false;
      if (MessageHeader::kSize + payload_size > sizeof(_send_buf)) return false;

      MessageHeader hdr;
      hdr.type         = type;
      hdr.flags        = flags;
      hdr.payload_size = static_cast<uint32_t>(payload_size);
      hdr.write_to(_send_buf);

      if (payload_size > 0 && payload) {
        std::memcpy(_send_buf + MessageHeader::kSize, payload, payload_size);
      }

      int64_t n = _socket.sendto(_send_buf, MessageHeader::kSize + payload_size, dest);
      return n == static_cast<int64_t>(MessageHeader::kSize + payload_size);
    }

    /// Convenience: send with WriteBuffer payload.
    bool send_message(const NetworkAddress &dest, MessageType type,
                      MessageFlags flags, const WriteBuffer &payload) {
      return send_message(dest, type, flags, payload.data(), payload.size());
    }

    /// Convenience: send with no payload.
    bool send_message(const NetworkAddress &dest, MessageType type,
                      MessageFlags flags = MessageFlags::none) {
      return send_message(dest, type, flags, nullptr, 0);
    }

    /// Receive one datagram.
    /// @return true if a valid datagram was received.
    bool recv_datagram(UdpDatagram &out) {
      if (!_bound) return false;

      NetworkAddress sender;
      int64_t n = _socket.recvfrom(_recv_buf, sizeof(_recv_buf), &sender);
      if (n < static_cast<int64_t>(MessageHeader::kSize)) return false;

      out.header = MessageHeader::read_from(_recv_buf);
      if (!out.header.is_valid()) return false;

      size_t expected = MessageHeader::kSize + out.header.payload_size;
      if (static_cast<size_t>(n) < expected) return false;

      out.sender = sender;
      out.payload.assign(
          _recv_buf + MessageHeader::kSize,
          _recv_buf + MessageHeader::kSize + out.header.payload_size);
      return true;
    }

    /// Set socket to non-blocking mode.
    bool set_nonblocking(bool enable = true) {
      return _socket.set_nonblocking(enable);
    }

    /// Close the socket.
    void close() {
      _bound = false;
      _socket.close();
    }

    bool is_bound() const noexcept { return _bound; }

    Socket &socket() noexcept { return _socket; }

  private:
    Socket  _socket;
    bool    _bound = false;

    // Internal send/recv buffers (stack-allocated, fits one datagram).
    uint8_t _send_buf[MessageHeader::kSize + kMaxUdpPayload] = {};
    uint8_t _recv_buf[MessageHeader::kSize + kMaxUdpPayload] = {};
  };

}  // namespace zs
