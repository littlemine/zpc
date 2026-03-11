#pragma once
/// @file NamedChannel.hpp
/// @brief Cross-process bidirectional byte channel.
///
/// Implemented over:
///   - Windows: Named pipes (CreateNamedPipeA / CreateFileA)
///   - POSIX:   Unix domain sockets (AF_UNIX, SOCK_STREAM)
///
/// Provides a minimal blocking read/write API.  For non-blocking or async
/// I/O, layer ASIO on top (via make_asio_endpoint).
///
/// @note This is a *lightweight process-level transport* — suitable for
///       signaling and small payloads.  For bulk data, use SharedMemoryRegion
///       and signal readiness through NamedChannel.

#include <cstddef>
#include <cstring>

#include "zensim/TypeAlias.hpp"
#include "zensim/Platform.hpp"

#if defined(ZS_PLATFORM_WINDOWS)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <unistd.h>
#endif

namespace zs {

  /// @brief A single endpoint (either server-accepted or client-connected).
  class NamedChannelConnection {
  public:
    NamedChannelConnection() noexcept = default;

    NamedChannelConnection(NamedChannelConnection &&o) noexcept : _handle{o._handle} {
      o._handle = kInvalid;
    }
    NamedChannelConnection &operator=(NamedChannelConnection &&o) noexcept {
      if (this != &o) { close_(); _handle = o._handle; o._handle = kInvalid; }
      return *this;
    }
    ~NamedChannelConnection() noexcept { close_(); }

    NamedChannelConnection(const NamedChannelConnection &) = delete;
    NamedChannelConnection &operator=(const NamedChannelConnection &) = delete;

    bool valid() const noexcept { return _handle != kInvalid; }
    explicit operator bool() const noexcept { return valid(); }

    /// Blocking write.  Returns bytes written, or -1 on error.
    i64 write(const void *data, size_t len) const {
      if (!valid()) return -1;
#if defined(ZS_PLATFORM_WINDOWS)
      DWORD written = 0;
      if (!WriteFile(_handle, data, (DWORD)len, &written, nullptr)) return -1;
      return (i64)written;
#else
      return ::write(_handle, data, len);
#endif
    }

    /// Blocking read.  Returns bytes read, or -1 on error, 0 on EOF.
    i64 read(void *buf, size_t len) const {
      if (!valid()) return -1;
#if defined(ZS_PLATFORM_WINDOWS)
      DWORD bytesRead = 0;
      if (!ReadFile(_handle, buf, (DWORD)len, &bytesRead, nullptr)) return -1;
      return (i64)bytesRead;
#else
      return ::read(_handle, buf, len);
#endif
    }

  private:
    friend class NamedChannel;

#if defined(ZS_PLATFORM_WINDOWS)
    using handle_t = HANDLE;
    static constexpr handle_t kInvalid = INVALID_HANDLE_VALUE;
#else
    using handle_t = int;
    static constexpr handle_t kInvalid = -1;
#endif

    explicit NamedChannelConnection(handle_t h) noexcept : _handle{h} {}

    void close_() noexcept {
      if (_handle == kInvalid) return;
#if defined(ZS_PLATFORM_WINDOWS)
      CloseHandle(_handle);
#else
      ::close(_handle);
#endif
      _handle = kInvalid;
    }

    handle_t _handle{kInvalid};
  };

  /// @brief Named channel — server (listen + accept) or client (connect).
  ///
  /// Typical usage:
  /// @code
  ///   // Server side:
  ///   auto server = NamedChannel::listen("zpc_sim_control");
  ///   auto conn = server.accept();
  ///   conn.read(buf, len);
  ///
  ///   // Client side:
  ///   auto client = NamedChannel::connect("zpc_sim_control");
  ///   client.write(buf, len);
  /// @endcode
  class NamedChannel {
  public:
    NamedChannel() noexcept = default;

    NamedChannel(NamedChannel &&o) noexcept : _handle{o._handle} {
      o._handle = kInvalid;
#if !defined(ZS_PLATFORM_WINDOWS)
      std::memcpy(_path, o._path, sizeof(_path));
      o._path[0] = '\0';
#endif
    }
    NamedChannel &operator=(NamedChannel &&o) noexcept {
      if (this != &o) {
        close_();
        _handle = o._handle;
        o._handle = kInvalid;
#if !defined(ZS_PLATFORM_WINDOWS)
        std::memcpy(_path, o._path, sizeof(_path));
        o._path[0] = '\0';
#endif
      }
      return *this;
    }
    ~NamedChannel() noexcept { close_(); }

    NamedChannel(const NamedChannel &) = delete;
    NamedChannel &operator=(const NamedChannel &) = delete;

    bool valid() const noexcept { return _handle != kInvalid; }
    explicit operator bool() const noexcept { return valid(); }

    /// Create a listening server channel.
    static NamedChannel listen(const char *name) {
      NamedChannel ch;
#if defined(ZS_PLATFORM_WINDOWS)
      // Build pipe name: \\.\pipe\<name>
      char pipeName[256];
      snprintf(pipeName, sizeof(pipeName), "\\\\.\\pipe\\%s", name);

      ch._handle = CreateNamedPipeA(
          pipeName,
          PIPE_ACCESS_DUPLEX,
          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
          1,       // max instances
          65536,   // out buffer
          65536,   // in buffer
          0,       // default timeout
          nullptr);
#else
      ch._handle = socket(AF_UNIX, SOCK_STREAM, 0);
      if (ch._handle < 0) return {};

      struct sockaddr_un addr{};
      addr.sun_family = AF_UNIX;
      // Use abstract namespace on Linux (prepend \0), filesystem path otherwise
      snprintf(ch._path, sizeof(ch._path), "/tmp/zpc_%s.sock", name);
      ::unlink(ch._path);  // remove stale socket
      std::strncpy(addr.sun_path, ch._path, sizeof(addr.sun_path) - 1);

      if (bind(ch._handle, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ::close(ch._handle);
        ch._handle = kInvalid;
        return {};
      }
      if (::listen(ch._handle, 1) < 0) {
        ::close(ch._handle);
        ::unlink(ch._path);
        ch._handle = kInvalid;
        return {};
      }
#endif
      return ch;
    }

    /// Accept a single incoming connection.  Blocks until a client connects.
    NamedChannelConnection accept() {
      if (!valid()) return {};
#if defined(ZS_PLATFORM_WINDOWS)
      if (!ConnectNamedPipe(_handle, nullptr)) {
        if (GetLastError() != ERROR_PIPE_CONNECTED) return {};
      }
      // On Windows named pipes, the server handle IS the connection
      auto conn = NamedChannelConnection{_handle};
      _handle = kInvalid;  // transfer ownership
      return conn;
#else
      int fd = ::accept(_handle, nullptr, nullptr);
      if (fd < 0) return {};
      return NamedChannelConnection{fd};
#endif
    }

    /// Connect to an existing server channel.
    static NamedChannelConnection connect(const char *name) {
#if defined(ZS_PLATFORM_WINDOWS)
      char pipeName[256];
      snprintf(pipeName, sizeof(pipeName), "\\\\.\\pipe\\%s", name);

      HANDLE h = CreateFileA(
          pipeName, GENERIC_READ | GENERIC_WRITE,
          0, nullptr, OPEN_EXISTING, 0, nullptr);
      if (h == INVALID_HANDLE_VALUE) return {};
      return NamedChannelConnection{h};
#else
      int fd = socket(AF_UNIX, SOCK_STREAM, 0);
      if (fd < 0) return {};

      struct sockaddr_un addr{};
      addr.sun_family = AF_UNIX;
      char path[108];
      snprintf(path, sizeof(path), "/tmp/zpc_%s.sock", name);
      std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

      if (::connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ::close(fd);
        return {};
      }
      return NamedChannelConnection{fd};
#endif
    }

  private:
#if defined(ZS_PLATFORM_WINDOWS)
    using handle_t = HANDLE;
    static constexpr handle_t kInvalid = INVALID_HANDLE_VALUE;
#else
    using handle_t = int;
    static constexpr handle_t kInvalid = -1;
    char _path[108]{};
#endif

    void close_() noexcept {
      if (_handle == kInvalid) return;
#if defined(ZS_PLATFORM_WINDOWS)
      CloseHandle(_handle);
#else
      ::close(_handle);
      if (_path[0] != '\0') {
        ::unlink(_path);
        _path[0] = '\0';
      }
#endif
      _handle = kInvalid;
    }

    handle_t _handle{kInvalid};
  };

}  // namespace zs
