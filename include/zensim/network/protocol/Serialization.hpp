#pragma once
/// @file Serialization.hpp
/// @brief Lightweight binary serialization for network payloads.
///
/// Provides WriteBuffer and ReadBuffer — zero-copy, contiguous byte buffers
/// with cursor-based sequential read/write.  These are the building blocks
/// that every network layer uses to marshal and unmarshal data.
///
/// Design:
///   - WriteBuffer owns its memory (resizable vector).
///   - ReadBuffer is a non-owning view over a byte range.
///   - Both support POD types, length-prefixed strings, and raw byte spans.
///   - No virtual dispatch, no heap allocation on the read path.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace zs {

  /// @brief Growable buffer for serializing outgoing data.
  class WriteBuffer {
  public:
    WriteBuffer() = default;
    explicit WriteBuffer(size_t reserve) { _data.reserve(reserve); }

    /// Current serialized size in bytes.
    size_t size() const noexcept { return _data.size(); }

    /// Raw pointer to the start of the buffer.
    const uint8_t *data() const noexcept { return _data.data(); }
    uint8_t *data() noexcept { return _data.data(); }

    /// Clear all written data (keeps allocated capacity).
    void clear() noexcept { _data.clear(); }

    // ── primitive writes ──────────────────────────────────────────────

    void write_u8(uint8_t v) { _data.push_back(v); }

    void write_u16(uint16_t v) {
      const auto off = _data.size();
      _data.resize(off + 2);
      std::memcpy(_data.data() + off, &v, 2);
    }

    void write_u32(uint32_t v) {
      const auto off = _data.size();
      _data.resize(off + 4);
      std::memcpy(_data.data() + off, &v, 4);
    }

    void write_u64(uint64_t v) {
      const auto off = _data.size();
      _data.resize(off + 8);
      std::memcpy(_data.data() + off, &v, 8);
    }

    void write_i32(int32_t v) {
      const auto off = _data.size();
      _data.resize(off + 4);
      std::memcpy(_data.data() + off, &v, 4);
    }

    void write_f32(float v) {
      const auto off = _data.size();
      _data.resize(off + 4);
      std::memcpy(_data.data() + off, &v, 4);
    }

    void write_f64(double v) {
      const auto off = _data.size();
      _data.resize(off + 8);
      std::memcpy(_data.data() + off, &v, 8);
    }

    /// Write raw bytes.
    void write_bytes(const void *src, size_t len) {
      const auto off = _data.size();
      _data.resize(off + len);
      std::memcpy(_data.data() + off, src, len);
    }

    /// Write a length-prefixed string (u32 length + bytes, no null terminator).
    void write_string(const std::string &s) {
      write_u32(static_cast<uint32_t>(s.size()));
      write_bytes(s.data(), s.size());
    }

    /// Access the underlying vector (e.g. for moving out).
    std::vector<uint8_t> &vec() noexcept { return _data; }
    const std::vector<uint8_t> &vec() const noexcept { return _data; }

  private:
    std::vector<uint8_t> _data;
  };

  // ═══════════════════════════════════════════════════════════════════════

  /// @brief Non-owning view for deserializing incoming data.
  class ReadBuffer {
  public:
    ReadBuffer() noexcept = default;
    ReadBuffer(const uint8_t *data, size_t size) noexcept
        : _data{data}, _size{size}, _cursor{0} {}
    ReadBuffer(const void *data, size_t size) noexcept
        : ReadBuffer{static_cast<const uint8_t *>(data), size} {}

    /// Total size of the buffer.
    size_t size() const noexcept { return _size; }
    /// Current read cursor position.
    size_t cursor() const noexcept { return _cursor; }
    /// Bytes remaining after cursor.
    size_t remaining() const noexcept { return _size - _cursor; }
    /// Whether the cursor has reached the end.
    bool at_end() const noexcept { return _cursor >= _size; }
    /// Whether an error (over-read) has occurred.
    bool error() const noexcept { return _error; }

    // ── primitive reads ───────────────────────────────────────────────

    uint8_t read_u8() {
      if (!can_read_(1)) return 0;
      return _data[_cursor++];
    }

    uint16_t read_u16() {
      uint16_t v = 0;
      if (!can_read_(2)) return 0;
      std::memcpy(&v, _data + _cursor, 2);
      _cursor += 2;
      return v;
    }

    uint32_t read_u32() {
      uint32_t v = 0;
      if (!can_read_(4)) return 0;
      std::memcpy(&v, _data + _cursor, 4);
      _cursor += 4;
      return v;
    }

    uint64_t read_u64() {
      uint64_t v = 0;
      if (!can_read_(8)) return 0;
      std::memcpy(&v, _data + _cursor, 8);
      _cursor += 8;
      return v;
    }

    int32_t read_i32() {
      int32_t v = 0;
      if (!can_read_(4)) return 0;
      std::memcpy(&v, _data + _cursor, 4);
      _cursor += 4;
      return v;
    }

    float read_f32() {
      float v = 0;
      if (!can_read_(4)) return 0;
      std::memcpy(&v, _data + _cursor, 4);
      _cursor += 4;
      return v;
    }

    double read_f64() {
      double v = 0;
      if (!can_read_(8)) return 0;
      std::memcpy(&v, _data + _cursor, 8);
      _cursor += 8;
      return v;
    }

    /// Read raw bytes into `dst`.
    bool read_bytes(void *dst, size_t len) {
      if (!can_read_(len)) return false;
      std::memcpy(dst, _data + _cursor, len);
      _cursor += len;
      return true;
    }

    /// Read a length-prefixed string.
    std::string read_string() {
      uint32_t len = read_u32();
      if (_error || !can_read_(len)) return {};
      std::string s(reinterpret_cast<const char *>(_data + _cursor), len);
      _cursor += len;
      return s;
    }

    /// Peek at raw data at current cursor without advancing.
    const uint8_t *peek() const noexcept {
      return (_cursor < _size) ? _data + _cursor : nullptr;
    }

    /// Skip `n` bytes.
    bool skip(size_t n) {
      if (!can_read_(n)) return false;
      _cursor += n;
      return true;
    }

  private:
    bool can_read_(size_t n) noexcept {
      if (_cursor + n > _size) {
        _error = true;
        return false;
      }
      return true;
    }

    const uint8_t *_data   = nullptr;
    size_t         _size   = 0;
    size_t         _cursor = 0;
    bool           _error  = false;
  };

}  // namespace zs
