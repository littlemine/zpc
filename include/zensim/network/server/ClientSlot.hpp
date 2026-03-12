#pragma once
/// @file ClientSlot.hpp
/// @brief Server-side per-client state for the game simulation.
///
/// A ClientSlot pairs a SessionId with gameplay-relevant state:
///   - Input buffer (last N inputs from the client)
///   - Replication cursor (which tick the client last acknowledged)
///   - Permissions / role
///
/// This is the bridge between the session layer (networking) and the
/// replication/simulation layers.

#include <cstdint>
#include <vector>

#include "zensim/network/session/Session.hpp"
#include "zensim/network/protocol/Serialization.hpp"

namespace zs {

  /// @brief A single input command from a client.
  ///
  /// Inputs are intentionally generic: a tick number and a payload
  /// of serialized command data.  The game-specific meaning is
  /// defined by the application.
  struct ClientInput {
    uint32_t              tick = 0;      ///< Server tick this input targets.
    std::vector<uint8_t>  data;          ///< Serialized input payload.
  };

  /// @brief Server-side slot tracking one connected player.
  class ClientSlot {
  public:
    ClientSlot() = default;
    explicit ClientSlot(SessionId sid) : _session_id{sid} {}

    SessionId session_id() const noexcept { return _session_id; }

    // ── input buffer ─────────────────────────────────────────────────

    /// Push a received input (server stores the last N).
    void push_input(ClientInput input) {
      if (_inputs.size() >= _max_buffered_inputs) {
        _inputs.erase(_inputs.begin());
      }
      _inputs.push_back(std::move(input));
    }

    /// Get the most recent input (or nullptr if none).
    const ClientInput *latest_input() const noexcept {
      return _inputs.empty() ? nullptr : &_inputs.back();
    }

    /// Get all buffered inputs.
    const std::vector<ClientInput> &inputs() const noexcept { return _inputs; }

    /// Clear processed inputs up to (and including) the given tick.
    void clear_inputs_through(uint32_t tick) {
      _inputs.erase(
          std::remove_if(_inputs.begin(), _inputs.end(),
                         [tick](const ClientInput &i) { return i.tick <= tick; }),
          _inputs.end());
    }

    // ── replication cursor ───────────────────────────────────────────

    /// The last server tick acknowledged by this client.
    uint32_t last_acked_tick() const noexcept { return _last_acked_tick; }
    void set_last_acked_tick(uint32_t t) noexcept { _last_acked_tick = t; }

    /// Whether this client needs a full snapshot (just joined / fell too far behind).
    bool needs_full_snapshot() const noexcept { return _needs_full_snapshot; }
    void set_needs_full_snapshot(bool v) noexcept { _needs_full_snapshot = v; }

  private:
    SessionId                _session_id        = kInvalidSession;
    std::vector<ClientInput> _inputs;
    size_t                   _max_buffered_inputs = 64;
    uint32_t                 _last_acked_tick     = 0;
    bool                     _needs_full_snapshot = true;
  };

}  // namespace zs
