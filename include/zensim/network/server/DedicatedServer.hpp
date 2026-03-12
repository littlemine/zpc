#pragma once
/// @file DedicatedServer.hpp
/// @brief Minimal authoritative dedicated game server.
///
/// Ties together:
///   - TcpListener for accepting client connections
///   - SessionManager for tracking active clients
///   - A fixed-timestep game loop
///   - Heartbeat and timeout management
///
/// This is the "vertical slice" entry point: a single-threaded server
/// that accepts connections, authenticates clients, and runs a tick loop.

#include <cstdint>
#include <functional>
#include <chrono>
#include <thread>

#include "zensim/network/transport/TcpTransport.hpp"
#include "zensim/network/session/SessionManager.hpp"

namespace zs {

  /// @brief Configuration for DedicatedServer.
  struct ServerConfig {
    std::string bind_address = "0.0.0.0";
    uint16_t    port         = 7777;
    uint32_t    max_clients  = 32;
    double      tick_rate    = 60.0;   ///< Server ticks per second.
    double      timeout_sec  = 30.0;   ///< Idle timeout before dropping a client.
  };

  /// @brief Server state machine.
  enum class ServerState : uint8_t {
    stopped = 0,
    starting,
    running,
    stopping,
  };

  /// @brief Minimal authoritative dedicated server.
  ///
  /// Usage:
  /// @code
  ///   DedicatedServer srv;
  ///   srv.configure({.port = 7777, .max_clients = 16});
  ///   srv.on_tick([](double dt, SessionManager &sm) {
  ///       // game logic here
  ///   });
  ///   srv.run();  // blocks until stop() is called
  /// @endcode
  class DedicatedServer {
  public:
    using TickCallback = std::function<void(double dt, SessionManager &sessions)>;
    using MessageCallback = std::function<void(Session &sender,
                                               const IncomingMessage &msg)>;

    DedicatedServer() = default;

    /// Apply configuration (must be called before run).
    void configure(const ServerConfig &cfg) { _config = cfg; }

    const ServerConfig &config() const noexcept { return _config; }
    ServerState state() const noexcept { return _state; }
    SessionManager &sessions() noexcept { return _sessions; }

    // ── event hooks ──────────────────────────────────────────────────

    /// Called every server tick with delta time.
    void on_tick(TickCallback cb) { _on_tick = std::move(cb); }

    /// Called when a framed message arrives from any session.
    void on_message(MessageCallback cb) { _on_message = std::move(cb); }

    // ── lifecycle ────────────────────────────────────────────────────

    /// Start the server (non-blocking).  Returns false on bind failure.
    bool start() {
      auto addr = NetworkAddress::from_string(_config.bind_address.c_str(),
                                              _config.port);
      if (!addr.is_valid()) return false;

      if (!_listener.start(addr)) return false;
      _listener.set_nonblocking(true);

      _sessions = SessionManager{_config.max_clients};
      _state = ServerState::running;
      return true;
    }

    /// Run the server loop (blocks the calling thread).
    void run() {
      if (_state != ServerState::running) {
        if (!start()) return;
      }

      using clock = std::chrono::steady_clock;
      const auto tick_interval = std::chrono::duration<double>(1.0 / _config.tick_rate);
      auto previous = clock::now();

      while (_state == ServerState::running) {
        auto now = clock::now();
        double dt = std::chrono::duration<double>(now - previous).count();
        previous = now;

        // Accept new connections
        accept_pending_();

        // Receive messages from all sessions
        poll_messages_();

        // Reap timed-out sessions
        _sessions.reap_idle(_config.timeout_sec);

        // User tick
        if (_on_tick) _on_tick(dt, _sessions);

        // Sleep until next tick
        auto elapsed = clock::now() - now;
        if (elapsed < tick_interval) {
          std::this_thread::sleep_for(tick_interval - elapsed);
        }
      }

      shutdown_();
    }

    /// Signal the server to stop (can be called from another thread).
    void stop() noexcept { _state = ServerState::stopping; }

    /// Number of connected clients.
    size_t client_count() const noexcept { return _sessions.count(); }

  private:
    void accept_pending_() {
      // Accept up to a batch of connections per tick
      for (int i = 0; i < 8; ++i) {
        auto conn = _listener.accept();
        if (!conn) break;
        conn->socket().set_nonblocking(true);
        _sessions.add_session(std::move(conn));
      }
    }

    void poll_messages_() {
      _sessions.for_each([this](Session &session) {
        if (!session.is_connected()) return;
        Connection *conn = session.connection();
        if (!conn) return;

        IncomingMessage msg;
        while (conn->recv_message(msg)) {
          session.touch();
          handle_system_message_(session, msg);
          if (_on_message) _on_message(session, msg);
        }
      });
    }

    void handle_system_message_(Session &session, const IncomingMessage &msg) {
      switch (msg.header.type) {
        case MessageType::heartbeat:
          session.send(MessageType::heartbeat_ack);
          break;
        case MessageType::disconnect:
          session.connection()->disconnect();
          break;
        case MessageType::ping: {
          // Echo payload back as pong
          session.send(MessageType::pong, MessageFlags::none,
                       msg.payload.data(), msg.payload.size());
          break;
        }
        default:
          break;
      }
    }

    void shutdown_() {
      _state = ServerState::stopping;
      // Notify all clients
      _sessions.for_each([](Session &s) {
        s.send(MessageType::shutdown_notice);
        if (s.connection()) s.connection()->disconnect();
      });
      _listener.stop();
      _state = ServerState::stopped;
    }

    ServerConfig    _config;
    ServerState     _state = ServerState::stopped;
    TcpListener     _listener;
    SessionManager  _sessions;
    TickCallback    _on_tick;
    MessageCallback _on_message;
  };

}  // namespace zs
