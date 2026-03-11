#pragma once
/// @file ByteStream.hpp
/// @brief Byte-oriented stream abstraction for uniform I/O across files,
///        standard streams (stdin/stdout/stderr), pipes, and sockets.
///
/// Design:
///   - ByteStream is an abstract interface with read/write/seek/flush.
///   - FileStream wraps POSIX file descriptors / Windows HANDLEs.
///   - StdioStream wraps stdin/stdout/stderr (non-seekable).
///   - All streams report capabilities (readable, writable, seekable)
///     so callers can adapt without downcasting.
///
/// This fills the I/O abstraction gap in zpc: previously the library only
/// had path-query utilities (Filesystem.hpp) and format-specific I/O
/// (ParticleIO, MeshIO).  ByteStream provides the missing low-level
/// byte transport that higher-level serialisers can build upon.

#include <cstddef>
#include <cstdint>
#include <string>

#include "zensim/Platform.hpp"

namespace zs {

  /// Seek origin, mirroring POSIX / C lseek semantics.
  enum class SeekOrigin : unsigned char {
    begin,     ///< SEEK_SET — offset from start
    current,   ///< SEEK_CUR — offset from current position
    end        ///< SEEK_END — offset from end
  };

  /// Abstract byte stream interface.
  ///
  /// Concrete implementations: FileStream, StdioStream.
  /// Streams are non-copyable but movable.
  class ZPC_CORE_API ByteStream {
  public:
    virtual ~ByteStream() = default;

    // ── capability queries ─────────────────────────────────────────────

    virtual bool is_readable() const noexcept = 0;
    virtual bool is_writable() const noexcept = 0;
    virtual bool is_seekable() const noexcept = 0;
    virtual bool is_open() const noexcept = 0;

    // ── data transfer ──────────────────────────────────────────────────

    /// Read up to `maxBytes` into `dst`.
    /// @return Number of bytes actually read (0 at EOF, -1 on error).
    virtual int64_t read(void *dst, size_t maxBytes) = 0;

    /// Write `bytes` from `src`.
    /// @return Number of bytes actually written (-1 on error).
    virtual int64_t write(const void *src, size_t bytes) = 0;

    // ── positioning ────────────────────────────────────────────────────

    /// Seek to a position.
    /// @return New absolute position, or -1 on error / not seekable.
    virtual int64_t seek(int64_t offset, SeekOrigin origin = SeekOrigin::begin) = 0;

    /// Current position in the stream (-1 if not seekable).
    virtual int64_t tell() const = 0;

    /// Total size of the underlying resource (-1 if unknown / not seekable).
    virtual int64_t size() const = 0;

    // ── lifecycle ──────────────────────────────────────────────────────

    /// Flush any buffered writes to the underlying transport.
    virtual bool flush() = 0;

    /// Close the stream, releasing the underlying resource.
    virtual void close() = 0;

  protected:
    ByteStream() = default;
    ByteStream(const ByteStream &) = delete;
    ByteStream &operator=(const ByteStream &) = delete;
    ByteStream(ByteStream &&) = default;
    ByteStream &operator=(ByteStream &&) = default;
  };

  // ═══════════════════════════════════════════════════════════════════════
  // FileStream — byte stream backed by a filesystem file
  // ═══════════════════════════════════════════════════════════════════════

  /// How a FileStream should be opened.
  enum class FileOpenMode : unsigned char {
    read,             ///< Open existing file for reading.
    write,            ///< Create/truncate file for writing.
    read_write,       ///< Open existing file for read+write.
    append,           ///< Open/create file, writes go to end.
  };

  /// @brief File-backed byte stream.
  ///
  /// Uses native handles (HANDLE on Windows, int fd on POSIX) for maximum
  /// performance.  No internal buffering — the OS page cache handles that.
  ///
  /// @code
  ///   FileStream fs("output.bin", FileOpenMode::write);
  ///   float data[1024];
  ///   fs.write(data, sizeof(data));
  ///   fs.flush();
  /// @endcode
  class ZPC_CORE_API FileStream : public ByteStream {
  public:
    FileStream() = default;

    /// Open a file.
    /// @param path  Filesystem path (UTF-8).
    /// @param mode  Open mode.
    FileStream(const std::string &path, FileOpenMode mode);

    ~FileStream() override;

    // Move-only.
    FileStream(FileStream &&o) noexcept;
    FileStream &operator=(FileStream &&o) noexcept;

    // ── ByteStream interface ───────────────────────────────────────────

    bool is_readable() const noexcept override;
    bool is_writable() const noexcept override;
    bool is_seekable() const noexcept override { return true; }
    bool is_open() const noexcept override;

    int64_t read(void *dst, size_t maxBytes) override;
    int64_t write(const void *src, size_t bytes) override;
    int64_t seek(int64_t offset, SeekOrigin origin = SeekOrigin::begin) override;
    int64_t tell() const override;
    int64_t size() const override;

    bool flush() override;
    void close() override;

    /// The filesystem path this stream was opened with.
    const std::string &path() const noexcept { return _path; }

  private:
    std::string _path{};
    FileOpenMode _mode{FileOpenMode::read};

#ifdef ZS_PLATFORM_WINDOWS
    void *_handle{nullptr};  // HANDLE (INVALID_HANDLE_VALUE when closed)
#else
    int _fd{-1};             // POSIX file descriptor
#endif
  };

  // ═══════════════════════════════════════════════════════════════════════
  // StdioStream — byte stream wrapping stdin / stdout / stderr
  // ═══════════════════════════════════════════════════════════════════════

  /// Which standard stream to wrap.
  enum class StdioKind : unsigned char {
    stdin_stream,
    stdout_stream,
    stderr_stream,
  };

  /// @brief Non-owning wrapper around a standard I/O stream.
  ///
  /// Non-seekable.  StdioStream never closes the underlying descriptor —
  /// the OS owns stdin/stdout/stderr.
  ///
  /// @code
  ///   StdioStream out(StdioKind::stdout_stream);
  ///   const char *msg = "hello, world\n";
  ///   out.write(msg, 13);
  /// @endcode
  class ZPC_CORE_API StdioStream : public ByteStream {
  public:
    explicit StdioStream(StdioKind kind) noexcept;
    ~StdioStream() override = default;

    // Non-copyable, non-movable (wraps a global resource).
    StdioStream(const StdioStream &) = delete;
    StdioStream &operator=(const StdioStream &) = delete;

    bool is_readable() const noexcept override;
    bool is_writable() const noexcept override;
    bool is_seekable() const noexcept override { return false; }
    bool is_open() const noexcept override { return true; }

    int64_t read(void *dst, size_t maxBytes) override;
    int64_t write(const void *src, size_t bytes) override;

    /// Not seekable — always returns -1.
    int64_t seek(int64_t, SeekOrigin) override { return -1; }
    int64_t tell() const override { return -1; }
    int64_t size() const override { return -1; }

    bool flush() override;

    /// No-op — we don't own standard streams.
    void close() override {}

  private:
    StdioKind _kind;
  };

}  // namespace zs
