#pragma once
/// @file SessionManager.hpp
/// @brief Manages the set of active sessions on a server.
///
/// Responsibilities:
///   - Accept new connections and assign session IDs
///   - Track authentication state per session
///   - Timeout and remove idle sessions
///   - Broadcast messages to all / subset of sessions
///   - Provide iteration over active sessions

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "zensim/network/session/Session.hpp"

namespace zs {

  /// @brief Server-side session manager.
  ///
  /// Not thread-safe — designed to be driven from a single game-loop
  /// thread.  If the accept path runs on a different thread, hand off
  /// the connected socket via a queue.
  class SessionManager {
  public:
    /// Callback signature for session events.
    using SessionCallback = std::function<void(Session &)>;

    explicit SessionManager(uint32_t max_sessions = 64)
        : _max_sessions{max_sessions} {}

    // ── session lifecycle ────────────────────────────────────────────

    /// Create a new session from an accepted connection.
    /// @return The new session's ID, or kInvalidSession if full.
    SessionId add_session(std::unique_ptr<Connection> conn) {
      if (_sessions.size() >= _max_sessions) return kInvalidSession;
      SessionId id = _next_id++;
      _sessions.emplace(id, Session{id, std::move(conn)});
      if (_on_connect) _on_connect(_sessions.at(id));
      return id;
    }

    /// Remove a session by ID.
    void remove_session(SessionId id) {
      auto it = _sessions.find(id);
      if (it == _sessions.end()) return;
      if (_on_disconnect) _on_disconnect(it->second);
      _sessions.erase(it);
    }

    /// Find a session by ID.
    Session *find(SessionId id) noexcept {
      auto it = _sessions.find(id);
      return (it != _sessions.end()) ? &it->second : nullptr;
    }

    const Session *find(SessionId id) const noexcept {
      auto it = _sessions.find(id);
      return (it != _sessions.end()) ? &it->second : nullptr;
    }

    // ── bulk operations ──────────────────────────────────────────────

    /// Number of active sessions.
    size_t count() const noexcept { return _sessions.size(); }

    /// Max allowed sessions.
    uint32_t max_sessions() const noexcept { return _max_sessions; }

    /// Iterate over all sessions.
    template <typename F>
    void for_each(F &&fn) {
      for (auto &[id, session] : _sessions) {
        fn(session);
      }
    }

    /// Broadcast a message to all authenticated sessions.
    void broadcast(MessageType type, MessageFlags flags,
                   const void *payload, size_t len) {
      for (auto &[id, session] : _sessions) {
        if (session.is_authenticated() && session.is_connected()) {
          session.send(type, flags, payload, len);
        }
      }
    }

    void broadcast(MessageType type, MessageFlags flags,
                   const WriteBuffer &buf) {
      broadcast(type, flags, buf.data(), buf.size());
    }

    /// Remove sessions that have been idle longer than `timeout_sec`.
    /// @return Number of sessions removed.
    size_t reap_idle(double timeout_sec) {
      std::vector<SessionId> to_remove;
      for (auto &[id, session] : _sessions) {
        if (session.is_timed_out(timeout_sec) || !session.is_connected()) {
          to_remove.push_back(id);
        }
      }
      for (auto id : to_remove) {
        remove_session(id);
      }
      return to_remove.size();
    }

    // ── event hooks ──────────────────────────────────────────────────

    void on_connect(SessionCallback cb)    { _on_connect    = std::move(cb); }
    void on_disconnect(SessionCallback cb) { _on_disconnect = std::move(cb); }

  private:
    uint32_t     _max_sessions;
    SessionId    _next_id = 1;
    std::unordered_map<SessionId, Session> _sessions;
    SessionCallback _on_connect;
    SessionCallback _on_disconnect;
  };

}  // namespace zs
