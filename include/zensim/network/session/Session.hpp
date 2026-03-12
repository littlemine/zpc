#pragma once
/// @file Session.hpp
/// @brief Network session — authenticated, stateful link between server and client.
///
/// A Session wraps a Connection and adds:
///   - Unique session ID (server-assigned)
///   - Authentication state
///   - Heartbeat / timeout tracking
///   - Per-session metadata (player name, permissions, etc.)
///
/// Sessions are managed by SessionManager; game code interacts with
/// sessions rather than raw connections.

#include <cstdint>
#include <string>
#include <chrono>

#include "zensim/network/transport/Connection.hpp"

namespace zs {

  /// @brief Opaque session identifier (server-assigned, unique per server lifetime).
  using SessionId = uint32_t;
  static constexpr SessionId kInvalidSession = 0;

  /// @brief Authentication state of a session.
  enum class AuthState : uint8_t {
    unauthenticated = 0,
    challenge_sent,
    authenticated,
    rejected,
  };

  /// @brief A network session between server and one client.
  ///
  /// Owns (or borrows) a Connection and layers authentication,
  /// heartbeat tracking, and session metadata on top.
  class Session {
  public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    Session() = default;

    Session(SessionId id, std::unique_ptr<Connection> conn)
        : _id{id}, _conn{std::move(conn)},
          _created{clock::now()}, _last_activity{_created} {}

    // Move-only
    Session(Session &&) = default;
    Session &operator=(Session &&) = default;
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    // ── identity ─────────────────────────────────────────────────────

    SessionId id() const noexcept { return _id; }
    const std::string &player_name() const noexcept { return _player_name; }
    void set_player_name(const std::string &name) { _player_name = name; }

    // ── authentication ───────────────────────────────────────────────

    AuthState auth_state() const noexcept { return _auth; }
    void set_auth_state(AuthState s) noexcept { _auth = s; }
    bool is_authenticated() const noexcept { return _auth == AuthState::authenticated; }

    // ── connection access ────────────────────────────────────────────

    Connection *connection() noexcept { return _conn.get(); }
    const Connection *connection() const noexcept { return _conn.get(); }

    bool is_connected() const noexcept {
      return _conn && _conn->state() == ConnectionState::connected;
    }

    // ── timing ───────────────────────────────────────────────────────

    time_point created_at() const noexcept { return _created; }
    time_point last_activity() const noexcept { return _last_activity; }

    void touch() noexcept { _last_activity = clock::now(); }

    /// Seconds since last activity.
    double idle_seconds() const noexcept {
      auto now = clock::now();
      return std::chrono::duration<double>(now - _last_activity).count();
    }

    /// Check if session has exceeded a timeout (seconds).
    bool is_timed_out(double timeout_seconds) const noexcept {
      return idle_seconds() > timeout_seconds;
    }

    // ── messaging helpers ────────────────────────────────────────────

    bool send(MessageType type, MessageFlags flags,
              const void *payload, size_t len) {
      if (!_conn) return false;
      touch();
      return _conn->send_message(type, flags, payload, len);
    }

    bool send(MessageType type, MessageFlags flags = MessageFlags::none) {
      return send(type, flags, nullptr, 0);
    }

    bool send(MessageType type, MessageFlags flags, const WriteBuffer &buf) {
      return send(type, flags, buf.data(), buf.size());
    }

  private:
    SessionId                     _id          = kInvalidSession;
    std::unique_ptr<Connection>   _conn;
    AuthState                     _auth        = AuthState::unauthenticated;
    std::string                   _player_name;
    time_point                    _created     = {};
    time_point                    _last_activity = {};
  };

}  // namespace zs
