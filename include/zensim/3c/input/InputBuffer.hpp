#pragma once
/// @file InputBuffer.hpp
/// @brief RingBuffer-backed input history for replay and validation.
///
/// Stores N frames of ActionSnapshot history, enabling:
/// - Input replay for deterministic validation scenarios
/// - Jump buffering (look back N frames for jump press)
/// - Combo detection (sequence of actions over time)

#include "zensim/3c/input/InputTypes.hpp"

namespace zs::threeC {

  /// Default number of frames to store in the input buffer.
  inline constexpr int k_defaultInputBufferSize = 16;

  /// Fixed-size circular buffer of ActionSnapshots.
  /// Uses a manual ring buffer rather than zs::RingBuffer because
  /// ActionSnapshot is a large struct and we need random access by
  /// frame index relative to the current frame.
  ///
  /// @tparam Size  Number of frames to store (must be > 0)
  template <int Size = k_defaultInputBufferSize>
  struct InputBuffer {
    static_assert(Size > 0, "InputBuffer size must be positive");

    /// Push a new frame's snapshot into the buffer.
    constexpr void push(ActionSnapshot const& snap) noexcept {
      _head = (_head + 1) % Size;
      _buffer[_head] = snap;
      if (_count < Size) ++_count;
    }

    /// Get the most recent snapshot (frame 0 = current).
    constexpr ActionSnapshot const& current() const noexcept {
      return _buffer[_head];
    }

    /// Get a snapshot N frames ago (0 = current, 1 = previous, ...).
    /// Returns the oldest available snapshot if N >= count.
    constexpr ActionSnapshot const& ago(int n) const noexcept {
      if (n >= _count) n = _count - 1;
      if (n < 0) n = 0;
      int idx = _head - n;
      if (idx < 0) idx += Size;
      return _buffer[idx];
    }

    /// Number of frames currently stored.
    constexpr int count() const noexcept { return _count; }

    /// Whether the buffer is full (has Size frames).
    constexpr bool full() const noexcept { return _count >= Size; }

    /// Clear all stored frames.
    constexpr void clear() noexcept {
      _head = -1;
      _count = 0;
    }

    /// Check if a specific action was pressed within the last N frames.
    /// This is the core primitive for jump buffering.
    ///
    /// @param id       Action to search for
    /// @param frames   Number of frames to look back (inclusive of current)
    /// @return         True if the action was in pressed phase within the window
    constexpr bool wasPressed(ActionId id, int frames) const noexcept {
      if (frames > _count) frames = _count;
      for (int i = 0; i < frames; ++i) {
        auto const& snap = ago(i);
        auto const* state = snap.find(id);
        if (state && state->justPressed()) return true;
      }
      return false;
    }

    /// Check if a specific action has been held for at least N frames.
    ///
    /// @param id       Action to check
    /// @param frames   Minimum number of consecutive held frames
    /// @return         True if the action was active for the entire window
    constexpr bool heldFor(ActionId id, int frames) const noexcept {
      if (frames > _count) return false;
      for (int i = 0; i < frames; ++i) {
        auto const& snap = ago(i);
        auto const* state = snap.find(id);
        if (!state || !state->isActive()) return false;
      }
      return true;
    }

    /// Get the total held duration of an action from the current frame.
    /// Returns 0 if the action is not currently active.
    constexpr f32 heldDuration(ActionId id) const noexcept {
      auto const& snap = current();
      auto const* state = snap.find(id);
      if (!state || !state->isActive()) return 0.0f;
      return state->heldDuration;
    }

  private:
    ActionSnapshot _buffer[Size]{};
    int _head = -1;
    int _count = 0;
  };

}  // namespace zs::threeC
