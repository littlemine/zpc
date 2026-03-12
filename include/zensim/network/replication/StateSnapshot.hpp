#pragma once
/// @file StateSnapshot.hpp
/// @brief World state snapshot and delta encoding for network replication.
///
/// The replication layer's job is to efficiently synchronize game state
/// from the authoritative server to all connected clients.  This header
/// defines:
///   - StateSnapshot: a serialized capture of the world at a given tick
///   - DeltaEncoder:  computes and applies binary diffs between snapshots
///
/// The encoding is intentionally simple (byte-level XOR diff + RLE zeros)
/// so it's easy to debug and fast for small state sizes typical of the
/// initial vertical slice.

#include <cstdint>
#include <cstring>
#include <vector>

#include "zensim/network/protocol/Serialization.hpp"

namespace zs {

  /// @brief A serialized snapshot of the game world at a specific server tick.
  struct StateSnapshot {
    uint32_t              tick = 0;
    std::vector<uint8_t>  data;

    bool empty() const noexcept { return data.empty(); }
    size_t size() const noexcept { return data.size(); }
  };

  /// @brief Computes and applies delta patches between snapshots.
  ///
  /// Algorithm (byte-level XOR + zero-run-length encoding):
  ///   1. XOR old vs new to get a diff buffer.
  ///   2. Encode diff with RLE for zero bytes (most bytes are unchanged).
  ///   3. Patch = RLE-encoded diff + new_tick.
  ///
  /// Applying: XOR the old snapshot with the decoded diff → new snapshot.
  class DeltaEncoder {
  public:
    /// Compute a delta from `old_snap` to `new_snap`.
    /// If old_snap is empty, the delta IS the full new_snap data.
    static std::vector<uint8_t> encode(const StateSnapshot &old_snap,
                                       const StateSnapshot &new_snap) {
      if (old_snap.empty() || old_snap.data.size() != new_snap.data.size()) {
        // Full snapshot — just copy raw data
        return new_snap.data;
      }

      // XOR diff
      std::vector<uint8_t> diff(new_snap.data.size());
      for (size_t i = 0; i < diff.size(); ++i) {
        diff[i] = old_snap.data[i] ^ new_snap.data[i];
      }

      // RLE-encode zero runs
      return rle_encode_(diff);
    }

    /// Apply a delta to `base` snapshot, producing the result.
    /// If `base` is empty, the delta is treated as a full snapshot.
    static StateSnapshot decode(const StateSnapshot &base,
                                const std::vector<uint8_t> &delta,
                                uint32_t new_tick) {
      StateSnapshot result;
      result.tick = new_tick;

      if (base.empty()) {
        result.data = delta;
        return result;
      }

      // RLE-decode to get the XOR diff
      std::vector<uint8_t> diff = rle_decode_(delta, base.data.size());

      // Apply XOR
      result.data.resize(base.data.size());
      for (size_t i = 0; i < result.data.size(); ++i) {
        result.data[i] = base.data[i] ^ diff[i];
      }
      return result;
    }

  private:
    /// Simple RLE for zero runs:
    ///   Non-zero byte: written as-is.
    ///   Zero run:      0x00 followed by run-length as a u16 (LE).
    static std::vector<uint8_t> rle_encode_(const std::vector<uint8_t> &data) {
      std::vector<uint8_t> out;
      out.reserve(data.size());  // worst case same size

      size_t i = 0;
      while (i < data.size()) {
        if (data[i] != 0) {
          out.push_back(data[i]);
          ++i;
        } else {
          // Count zero run
          size_t run = 0;
          while (i < data.size() && data[i] == 0 && run < 65535) {
            ++run;
            ++i;
          }
          out.push_back(0);
          uint16_t r16 = static_cast<uint16_t>(run);
          out.push_back(static_cast<uint8_t>(r16 & 0xFF));
          out.push_back(static_cast<uint8_t>((r16 >> 8) & 0xFF));
        }
      }
      return out;
    }

    static std::vector<uint8_t> rle_decode_(const std::vector<uint8_t> &data,
                                             size_t expected_size) {
      std::vector<uint8_t> out;
      out.reserve(expected_size);

      size_t i = 0;
      while (i < data.size() && out.size() < expected_size) {
        if (data[i] != 0) {
          out.push_back(data[i]);
          ++i;
        } else {
          ++i;  // skip the 0x00 marker
          if (i + 1 >= data.size()) break;
          uint16_t run = static_cast<uint16_t>(data[i])
                       | (static_cast<uint16_t>(data[i + 1]) << 8);
          i += 2;
          for (uint16_t r = 0; r < run && out.size() < expected_size; ++r) {
            out.push_back(0);
          }
        }
      }

      // Pad if needed
      while (out.size() < expected_size) out.push_back(0);
      return out;
    }
  };

}  // namespace zs
