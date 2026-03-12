#pragma once
/// @file Socket.hpp
/// @brief Minimal cross-platform socket wrapper (RAII, non-copyable).
///
/// Wraps the platform socket handle (SOCKET on Windows, int on POSIX)
/// with creation, bind, listen, accept, connect, send, recv, and close.
/// This is the lowest-level building block; TcpTransport and UdpTransport
/// build on top of it.

#include <cstdint>
#include <cstring>

#include "zensim/Platform.hpp"
#include "zensim/network/transport/Address.hpp"

#if defined(ZS_PLATFORM_WINDOWS)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace zs {

#if defined(ZS_PLATFORM_WINDOWS)
  using socket_t = SOCKET;
  static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
  static constexpr int kSocketError = SOCKET_ERROR;
#else
  using socket_t = int;
  static constexpr socket_t kInvalidSocket = -1;
  static constexpr int kSocketError = -1;
#endif

  /// @brief Protocol type for socket creation.
  enum class SocketProtocol : uint8_t {
    tcp = 0,
    udp = 1,
  };

  /// @brief Initialize platform socket subsystem (Windows WSAStartup).
  ///
  /// Safe to call multiple times; uses a static init guard.
  /// On POSIX this is a no-op.
  inline bool socket_init() noexcept {
#if defined(ZS_PLATFORM_WINDOWS)
    static bool s_init = [] {
      WSADATA wsa;
      return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    return s_init;
#else
    return true;
#endif
  }

  /// @brief RAII wrapper around a platform socket descriptor.
  ///
  /// Non-copyable, movable.  Automatically closes the socket on destruction.
  class Socket {
  public:
    Socket() noexcept = default;
    explicit Socket(socket_t fd) noexcept : _fd{fd} {}

    /// Create a new socket for the given protocol and address family.
    static Socket create(SocketProtocol proto, AddressFamily af = AddressFamily::ipv4) noexcept {
      socket_init();
      int domain = (af == AddressFamily::ipv6) ? AF_INET6 : AF_INET;
      int type   = (proto == SocketProtocol::tcp) ? SOCK_STREAM : SOCK_DGRAM;
      int p      = (proto == SocketProtocol::tcp) ? IPPROTO_TCP : IPPROTO_UDP;
      socket_t fd = ::socket(domain, type, p);
      return Socket{fd};
    }

    ~Socket() noexcept { close(); }

    // Move-only
    Socket(Socket &&o) noexcept : _fd{o._fd} { o._fd = kInvalidSocket; }
    Socket &operator=(Socket &&o) noexcept {
      if (this != &o) { close(); _fd = o._fd; o._fd = kInvalidSocket; }
      return *this;
    }
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;

    bool valid() const noexcept { return _fd != kInvalidSocket; }
    explicit operator bool() const noexcept { return valid(); }
    socket_t handle() const noexcept { return _fd; }

    /// Release ownership without closing.
    socket_t release() noexcept {
      socket_t fd = _fd;
      _fd = kInvalidSocket;
      return fd;
    }

    // ── server operations ────────────────────────────────────────────

    bool set_reuse_addr(bool enable = true) noexcept {
      int val = enable ? 1 : 0;
      return ::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR,
                          reinterpret_cast<const char *>(&val), sizeof(val)) == 0;
    }

    bool bind(const NetworkAddress &addr) noexcept {
      return ::bind(_fd, addr.sockaddr_ptr(), addr.sockaddr_len()) == 0;
    }

    bool listen(int backlog = 128) noexcept {
      return ::listen(_fd, backlog) == 0;
    }

    /// Accept a new connection.  Returns an invalid Socket on failure.
    Socket accept(NetworkAddress *remote = nullptr) noexcept {
      struct sockaddr_storage ss;
      int len = static_cast<int>(sizeof(ss));
#if defined(ZS_PLATFORM_WINDOWS)
      socket_t fd = ::accept(_fd, reinterpret_cast<struct sockaddr *>(&ss), &len);
#else
      socklen_t slen = static_cast<socklen_t>(len);
      socket_t fd = ::accept(_fd, reinterpret_cast<struct sockaddr *>(&ss), &slen);
      len = static_cast<int>(slen);
#endif
      if (fd == kInvalidSocket) return Socket{};
      if (remote) {
        *remote = NetworkAddress::from_sockaddr(
            reinterpret_cast<struct sockaddr *>(&ss), len);
      }
      return Socket{fd};
    }

    // ── client operations ────────────────────────────────────────────

    bool connect(const NetworkAddress &addr) noexcept {
      return ::connect(_fd, addr.sockaddr_ptr(), addr.sockaddr_len()) == 0;
    }

    // ── data transfer (TCP) ──────────────────────────────────────────

    /// Send up to `len` bytes.  Returns bytes sent, or -1 on error.
    int64_t send(const void *data, size_t len) noexcept {
      return ::send(_fd, static_cast<const char *>(data),
                    static_cast<int>(len), 0);
    }

    /// Receive up to `len` bytes.  Returns bytes received, 0 on close, -1 on error.
    int64_t recv(void *buf, size_t len) noexcept {
      return ::recv(_fd, static_cast<char *>(buf),
                    static_cast<int>(len), 0);
    }

    // ── data transfer (UDP) ──────────────────────────────────────────

    int64_t sendto(const void *data, size_t len, const NetworkAddress &dest) noexcept {
      return ::sendto(_fd, static_cast<const char *>(data),
                      static_cast<int>(len), 0,
                      dest.sockaddr_ptr(), dest.sockaddr_len());
    }

    int64_t recvfrom(void *buf, size_t len, NetworkAddress *src = nullptr) noexcept {
      struct sockaddr_storage ss;
      int slen = static_cast<int>(sizeof(ss));
#if defined(ZS_PLATFORM_WINDOWS)
      int64_t n = ::recvfrom(_fd, static_cast<char *>(buf),
                             static_cast<int>(len), 0,
                             reinterpret_cast<struct sockaddr *>(&ss), &slen);
#else
      socklen_t sl = static_cast<socklen_t>(slen);
      int64_t n = ::recvfrom(_fd, static_cast<char *>(buf),
                             static_cast<int>(len), 0,
                             reinterpret_cast<struct sockaddr *>(&ss), &sl);
      slen = static_cast<int>(sl);
#endif
      if (n > 0 && src) {
        *src = NetworkAddress::from_sockaddr(
            reinterpret_cast<struct sockaddr *>(&ss), slen);
      }
      return n;
    }

    // ── non-blocking mode ────────────────────────────────────────────

    bool set_nonblocking(bool enable = true) noexcept {
#if defined(ZS_PLATFORM_WINDOWS)
      u_long mode = enable ? 1 : 0;
      return ioctlsocket(_fd, FIONBIO, &mode) == 0;
#else
      int flags = fcntl(_fd, F_GETFL, 0);
      if (flags < 0) return false;
      flags = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
      return fcntl(_fd, F_SETFL, flags) == 0;
#endif
    }

    // ── lifecycle ────────────────────────────────────────────────────

    void close() noexcept {
      if (_fd == kInvalidSocket) return;
#if defined(ZS_PLATFORM_WINDOWS)
      ::closesocket(_fd);
#else
      ::close(_fd);
#endif
      _fd = kInvalidSocket;
    }

  private:
    socket_t _fd = kInvalidSocket;
  };

}  // namespace zs
