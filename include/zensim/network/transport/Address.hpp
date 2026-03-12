#pragma once
/// @file Address.hpp
/// @brief Network address abstraction (IPv4/IPv6 + port).
///
/// Thin wrapper around sockaddr_in / sockaddr_in6 that provides
/// uniform construction, comparison, and string conversion.  Used by
/// all transport types.

#include <cstdint>
#include <cstring>
#include <string>

#include "zensim/Platform.hpp"

#if defined(ZS_PLATFORM_WINDOWS)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#endif

namespace zs {

  /// @brief Identifies an address family.
  enum class AddressFamily : uint8_t {
    ipv4 = 0,
    ipv6 = 1,
  };

  /// @brief IPv4 or IPv6 endpoint (address + port).
  ///
  /// Constructible from a string ("127.0.0.1", "::1") or raw sockaddr.
  /// Stores the data in a union of sockaddr_in / sockaddr_in6 so it
  /// can be passed directly to platform socket APIs.
  class NetworkAddress {
  public:
    NetworkAddress() noexcept { std::memset(&_storage, 0, sizeof(_storage)); }

    /// Construct from string address and port.
    /// @param addr  Dotted-decimal (IPv4) or colon-hex (IPv6).
    /// @param port  Host-order port number.
    static NetworkAddress from_string(const char *addr, uint16_t port) noexcept {
      NetworkAddress a;
      // Try IPv4 first
      sockaddr_in sin4;
      std::memset(&sin4, 0, sizeof(sin4));
      sin4.sin_family = AF_INET;
      sin4.sin_port   = htons(port);
      if (inet_pton(AF_INET, addr, &sin4.sin_addr) == 1) {
        std::memcpy(&a._storage, &sin4, sizeof(sin4));
        a._family = AddressFamily::ipv4;
        return a;
      }
      // Try IPv6
      sockaddr_in6 sin6;
      std::memset(&sin6, 0, sizeof(sin6));
      sin6.sin6_family = AF_INET6;
      sin6.sin6_port   = htons(port);
      if (inet_pton(AF_INET6, addr, &sin6.sin6_addr) == 1) {
        std::memcpy(&a._storage, &sin6, sizeof(sin6));
        a._family = AddressFamily::ipv6;
        return a;
      }
      return a;  // invalid — caller should check is_valid()
    }

    /// Construct from raw sockaddr.
    static NetworkAddress from_sockaddr(const struct sockaddr *sa, int sa_len) noexcept {
      NetworkAddress a;
      if (!sa) return a;
      std::memcpy(&a._storage, sa, sa_len > (int)sizeof(a._storage) ? (int)sizeof(a._storage) : sa_len);
      a._family = (sa->sa_family == AF_INET6) ? AddressFamily::ipv6 : AddressFamily::ipv4;
      return a;
    }

    AddressFamily family() const noexcept { return _family; }

    uint16_t port() const noexcept {
      if (_family == AddressFamily::ipv6) {
        const auto *s6 = reinterpret_cast<const sockaddr_in6 *>(&_storage);
        return ntohs(s6->sin6_port);
      }
      const auto *s4 = reinterpret_cast<const sockaddr_in *>(&_storage);
      return ntohs(s4->sin_port);
    }

    /// Return the platform sockaddr pointer (for bind/connect/sendto).
    const struct sockaddr *sockaddr_ptr() const noexcept {
      return reinterpret_cast<const struct sockaddr *>(&_storage);
    }
    int sockaddr_len() const noexcept {
      return (_family == AddressFamily::ipv6)
                 ? static_cast<int>(sizeof(sockaddr_in6))
                 : static_cast<int>(sizeof(sockaddr_in));
    }

    /// Convert to human-readable "addr:port" string.
    std::string to_string() const {
      char buf[INET6_ADDRSTRLEN + 1] = {};
      if (_family == AddressFamily::ipv6) {
        const auto *s6 = reinterpret_cast<const sockaddr_in6 *>(&_storage);
        inet_ntop(AF_INET6, &s6->sin6_addr, buf, sizeof(buf));
        return std::string("[") + buf + "]:" + std::to_string(ntohs(s6->sin6_port));
      }
      const auto *s4 = reinterpret_cast<const sockaddr_in *>(&_storage);
      inet_ntop(AF_INET, &s4->sin_addr, buf, sizeof(buf));
      return std::string(buf) + ":" + std::to_string(ntohs(s4->sin_port));
    }

    bool is_valid() const noexcept {
      const auto *sa = reinterpret_cast<const struct sockaddr *>(&_storage);
      return sa->sa_family == AF_INET || sa->sa_family == AF_INET6;
    }

    bool operator==(const NetworkAddress &o) const noexcept {
      if (_family != o._family) return false;
      return std::memcmp(&_storage, &o._storage, sockaddr_len()) == 0;
    }
    bool operator!=(const NetworkAddress &o) const noexcept { return !(*this == o); }

  private:
    // Storage large enough for either sockaddr_in or sockaddr_in6.
    struct sockaddr_storage _storage = {};
    AddressFamily           _family  = AddressFamily::ipv4;
  };

}  // namespace zs
