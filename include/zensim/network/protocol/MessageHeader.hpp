#pragma once
/// @file MessageHeader.hpp
/// @brief Fixed-size binary message header for the network protocol.
///
/// Every message on the wire begins with a MessageHeader.  The header is
/// deliberately small (8 bytes) and fixed-layout so it can be read with a
/// single recv() before the payload is consumed.
///
/// Wire layout (little-endian):
///   [0]      u8   magic          — 0x5A ('Z' for zensim)
///   [1]      u8   version        — protocol version (currently 1)
///   [2]      u8   type           — MessageType enum value
///   [3]      u8   flags          — per-message flags (compressed, reliable, etc.)
///   [4..7]   u32  payload_size   — byte length of the payload following the header

#include <cstdint>
#include <cstring>

#include "zensim/network/protocol/MessageTypes.hpp"

namespace zs {

  /// @brief Per-message flags packed into a single byte.
  enum class MessageFlags : uint8_t {
    none       = 0,
    reliable   = 1 << 0,  ///< Must be acknowledged by the receiver.
    compressed = 1 << 1,  ///< Payload is LZ4 / zstd compressed.
    encrypted  = 1 << 2,  ///< Payload is encrypted.
    fragmented = 1 << 3,  ///< This is part of a multi-part message.
  };

  /// Bitwise OR for MessageFlags.
  inline constexpr MessageFlags operator|(MessageFlags a, MessageFlags b) noexcept {
    return static_cast<MessageFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
  }
  /// Bitwise AND for MessageFlags.
  inline constexpr MessageFlags operator&(MessageFlags a, MessageFlags b) noexcept {
    return static_cast<MessageFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
  }
  /// Test if a flag is set.
  inline constexpr bool has_flag(MessageFlags flags, MessageFlags test) noexcept {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(test)) != 0;
  }

  /// @brief Fixed 8-byte header prepended to every network message.
  ///
  /// This struct is trivially-copyable and has no padding, so it can be
  /// sent/received directly via memcpy or reinterpret_cast on a byte
  /// buffer, provided endianness matches (we mandate little-endian on
  /// the wire, which is the native format on x86/ARM).
  struct MessageHeader {
    static constexpr uint8_t kMagic   = 0x5A;  // 'Z'
    static constexpr uint8_t kVersion = 1;
    static constexpr size_t  kSize    = 8;

    uint8_t      magic        = kMagic;
    uint8_t      version      = kVersion;
    MessageType  type         = MessageType::heartbeat;
    MessageFlags flags        = MessageFlags::none;
    uint32_t     payload_size = 0;

    /// Validate that magic and version fields are acceptable.
    bool is_valid() const noexcept {
      return magic == kMagic && version == kVersion;
    }

    /// Serialize the header into an 8-byte buffer.
    void write_to(void *dst) const noexcept {
      auto *p = static_cast<uint8_t *>(dst);
      p[0] = magic;
      p[1] = version;
      p[2] = static_cast<uint8_t>(type);
      p[3] = static_cast<uint8_t>(flags);
      std::memcpy(p + 4, &payload_size, 4);
    }

    /// Deserialize a header from an 8-byte buffer.
    static MessageHeader read_from(const void *src) noexcept {
      MessageHeader h;
      auto *p = static_cast<const uint8_t *>(src);
      h.magic        = p[0];
      h.version      = p[1];
      h.type         = static_cast<MessageType>(p[2]);
      h.flags        = static_cast<MessageFlags>(p[3]);
      std::memcpy(&h.payload_size, p + 4, 4);
      return h;
    }
  };

  static_assert(sizeof(MessageHeader) == MessageHeader::kSize,
                "MessageHeader must be exactly 8 bytes");

}  // namespace zs
