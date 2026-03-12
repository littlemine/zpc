#pragma once
/// @file SequenceBuffer.hpp
/// @brief Fixed-size circular buffer indexed by sequence numbers.
///
/// Essential for reliable-over-UDP networking: tracks which sequence
/// numbers have been acknowledged, stores pending packets for
/// retransmission, and detects duplicates — all in O(1) time with
/// bounded memory.
///
/// The buffer stores entries indexed by `(sequence % capacity)`.
/// Sequence comparison uses modular arithmetic (wraps at 2^16).

#include <cstdint>
#include <cstring>
#include <array>

namespace zs {

  /// @brief Compare two 16-bit sequence numbers with wrap-around.
  ///
  /// Returns true if `a` is "more recent" than `b`, handling the
  /// wrap-around at 65535 -> 0.
  inline bool sequence_more_recent(uint16_t a, uint16_t b) noexcept {
    return ((a > b) && (a - b <= 32768)) ||
           ((a < b) && (b - a >  32768));
  }

  /// @brief Fixed-capacity circular buffer keyed by 16-bit sequence number.
  ///
  /// @tparam T      The type stored per entry.
  /// @tparam Cap    Number of entries (power of 2 recommended).
  template <typename T, size_t Cap = 1024>
  class SequenceBuffer {
  public:
    static constexpr size_t capacity = Cap;

    SequenceBuffer() noexcept { reset(); }

    /// Clear all entries.
    void reset() noexcept {
      std::memset(_valid, 0, sizeof(_valid));
      _sequence = 0;
    }

    /// Current (latest inserted) sequence number.
    uint16_t sequence() const noexcept { return _sequence; }

    /// Insert an entry at the given sequence number.
    ///
    /// If `seq` is ahead of the current sequence, all entries in the
    /// gap are invalidated (they were skipped/lost).
    /// If `seq` is too old (more than Cap behind current), it is ignored.
    ///
    /// @return Pointer to the stored entry, or nullptr if too old.
    T *insert(uint16_t seq) {
      if (sequence_more_recent(seq, _sequence)) {
        // Advance: invalidate entries in the gap
        advance_to_(seq);
      } else if (sequence_more_recent(_sequence, seq)) {
        // Check if seq is within our window
        uint16_t diff = _sequence - seq;
        if (diff >= Cap) return nullptr;  // too old
      }
      size_t idx = seq % Cap;
      _valid[idx] = true;
      _entries[idx] = T{};
      return &_entries[idx];
    }

    /// Check if an entry exists for the given sequence number.
    bool exists(uint16_t seq) const noexcept {
      if (!in_window_(seq)) return false;
      size_t idx = seq % Cap;
      return _valid[idx];
    }

    /// Retrieve a stored entry (nullptr if not present or out of window).
    T *find(uint16_t seq) noexcept {
      if (!exists(seq)) return nullptr;
      return &_entries[seq % Cap];
    }

    const T *find(uint16_t seq) const noexcept {
      if (!exists(seq)) return nullptr;
      return &_entries[seq % Cap];
    }

    /// Remove an entry (mark as invalid).
    void remove(uint16_t seq) noexcept {
      if (in_window_(seq)) {
        _valid[seq % Cap] = false;
      }
    }

  private:
    void advance_to_(uint16_t seq) {
      if (sequence_more_recent(seq, static_cast<uint16_t>(_sequence + Cap))) {
        // Jumped far ahead — invalidate everything
        std::memset(_valid, 0, sizeof(_valid));
      } else {
        // Invalidate entries between old and new sequence
        for (uint16_t s = _sequence + 1; s != static_cast<uint16_t>(seq + 1); ++s) {
          _valid[s % Cap] = false;
        }
      }
      _sequence = seq;
    }

    bool in_window_(uint16_t seq) const noexcept {
      uint16_t diff = _sequence - seq;
      return diff < Cap;
    }

    uint16_t _sequence = 0;
    bool     _valid[Cap] = {};
    T        _entries[Cap] = {};
  };

}  // namespace zs
