#pragma once
/// @file TcpTransport.hpp
/// @brief TCP transport — framed, reliable, ordered byte stream.
///
/// TcpConnection wraps a connected Socket and implements the Connection
/// interface.  Each send/recv is length-prefixed by a MessageHeader so
/// messages are properly framed over the stream.
///
/// TcpListener accepts incoming connections on a server socket.

#include <memory>
#include <vector>

#include "zensim/network/transport/Connection.hpp"
#include "zensim/network/transport/Socket.hpp"

namespace zs {

  /// @brief A TCP connection implementing the Connection interface.
  ///
  /// Framing: every message on the wire is `[8-byte MessageHeader][payload]`.
  /// send_message serializes the header + payload in a single write.
  /// recv_message reads 8 bytes (header), validates, then reads payload.
  class TcpConnection final : public Connection {
  public:
    /// Take ownership of a connected socket.
    explicit TcpConnection(Socket sock, NetworkAddress remote)
        : _socket{std::move(sock)}, _remote{remote},
          _state{ConnectionState::connected} {}

    ConnectionState state() const noexcept override { return _state; }
    NetworkAddress remote_address() const noexcept override { return _remote; }
    const ConnectionStats &stats() const noexcept override { return _stats; }

    bool send_message(MessageType type, MessageFlags flags,
                      const void *payload, size_t payload_size) override {
      if (_state != ConnectionState::connected) return false;

      MessageHeader hdr;
      hdr.type         = type;
      hdr.flags        = flags;
      hdr.payload_size = static_cast<uint32_t>(payload_size);

      // Serialize header
      uint8_t hdr_buf[MessageHeader::kSize];
      hdr.write_to(hdr_buf);

      // Send header
      if (!send_all_(hdr_buf, MessageHeader::kSize)) {
        _state = ConnectionState::disconnected;
        return false;
      }

      // Send payload
      if (payload_size > 0 && payload) {
        if (!send_all_(payload, payload_size)) {
          _state = ConnectionState::disconnected;
          return false;
        }
      }

      _stats.packets_sent++;
      _stats.bytes_sent += MessageHeader::kSize + payload_size;
      return true;
    }

    bool recv_message(IncomingMessage &out) override {
      if (_state != ConnectionState::connected) return false;

      // Read header
      uint8_t hdr_buf[MessageHeader::kSize];
      if (!recv_all_(hdr_buf, MessageHeader::kSize)) return false;

      out.header = MessageHeader::read_from(hdr_buf);
      if (!out.header.is_valid()) {
        _state = ConnectionState::disconnected;
        return false;
      }

      // Read payload
      out.payload.resize(out.header.payload_size);
      if (out.header.payload_size > 0) {
        if (!recv_all_(out.payload.data(), out.header.payload_size)) {
          _state = ConnectionState::disconnected;
          return false;
        }
      }

      _stats.packets_received++;
      _stats.bytes_received += MessageHeader::kSize + out.header.payload_size;
      return true;
    }

    void disconnect() override {
      if (_state == ConnectionState::disconnected) return;
      _state = ConnectionState::disconnected;
      _socket.close();
    }

    /// Access the underlying socket (e.g. for setting options).
    Socket &socket() noexcept { return _socket; }

  private:
    bool send_all_(const void *data, size_t len) {
      const auto *p = static_cast<const uint8_t *>(data);
      size_t sent = 0;
      while (sent < len) {
        int64_t n = _socket.send(p + sent, len - sent);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
      }
      return true;
    }

    bool recv_all_(void *buf, size_t len) {
      auto *p = static_cast<uint8_t *>(buf);
      size_t got = 0;
      while (got < len) {
        int64_t n = _socket.recv(p + got, len - got);
        if (n <= 0) return false;
        got += static_cast<size_t>(n);
      }
      return true;
    }

    Socket          _socket;
    NetworkAddress  _remote;
    ConnectionState _state = ConnectionState::disconnected;
    ConnectionStats _stats;
  };

  // ═══════════════════════════════════════════════════════════════════════

  /// @brief TCP server listener — binds, listens, and accepts connections.
  class TcpListener {
  public:
    TcpListener() = default;

    /// Start listening on the given address.
    bool start(const NetworkAddress &bind_addr) {
      _socket = Socket::create(SocketProtocol::tcp, bind_addr.family());
      if (!_socket) return false;
      _socket.set_reuse_addr();
      if (!_socket.bind(bind_addr)) { _socket.close(); return false; }
      if (!_socket.listen())        { _socket.close(); return false; }
      _listening = true;
      return true;
    }

    /// Accept a pending connection.
    /// @return A connected TcpConnection, or nullptr if none pending.
    std::unique_ptr<TcpConnection> accept() {
      if (!_listening) return nullptr;
      NetworkAddress remote;
      Socket client = _socket.accept(&remote);
      if (!client) return nullptr;
      return std::make_unique<TcpConnection>(std::move(client), remote);
    }

    /// Stop listening and close the server socket.
    void stop() {
      _listening = false;
      _socket.close();
    }

    bool listening() const noexcept { return _listening; }

    /// Set the listen socket to non-blocking mode (for polling accept).
    bool set_nonblocking(bool enable = true) {
      return _socket.set_nonblocking(enable);
    }

  private:
    Socket _socket;
    bool   _listening = false;
  };

}  // namespace zs
