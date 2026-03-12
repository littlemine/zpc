/// @file network_transport_smoke.cpp
/// @brief Smoke tests for the transport layer.
///
/// Tests:
///   1. NetworkAddress construction and string conversion
///   2. Socket creation and cleanup
///   3. TCP loopback: server listen, client connect, send/recv framed message
///   4. UDP loopback: bind, sendto, recvfrom framed datagram

#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

#include "zensim/network/transport/TcpTransport.hpp"
#include "zensim/network/transport/UdpTransport.hpp"

static void test_address() {
  auto addr = zs::NetworkAddress::from_string("127.0.0.1", 7777);
  assert(addr.is_valid());
  assert(addr.port() == 7777);
  assert(addr.family() == zs::AddressFamily::ipv4);
  auto s = addr.to_string();
  assert(s.find("127.0.0.1") != std::string::npos);
  assert(s.find("7777") != std::string::npos);

  std::printf("[PASS] test_address\n");
}

static void test_socket_create_close() {
  zs::socket_init();
  auto sock = zs::Socket::create(zs::SocketProtocol::tcp);
  assert(sock.valid());
  sock.close();
  assert(!sock.valid());

  auto udp = zs::Socket::create(zs::SocketProtocol::udp);
  assert(udp.valid());
  // Destructor closes

  std::printf("[PASS] test_socket_create_close\n");
}

static void test_tcp_loopback() {
  zs::socket_init();

  // Start server
  zs::TcpListener listener;
  auto bind_addr = zs::NetworkAddress::from_string("127.0.0.1", 0);
  // Use port 0 to let OS assign — but TcpListener doesn't expose the port.
  // Use a fixed port for testing.
  bind_addr = zs::NetworkAddress::from_string("127.0.0.1", 17771);
  bool ok = listener.start(bind_addr);
  assert(ok);
  listener.set_nonblocking(true);

  // Client thread
  std::thread client_thread([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto sock = zs::Socket::create(zs::SocketProtocol::tcp);
    assert(sock.valid());
    auto addr = zs::NetworkAddress::from_string("127.0.0.1", 17771);
    bool connected = sock.connect(addr);
    assert(connected);

    zs::TcpConnection conn(std::move(sock), addr);

    // Send a ping message with payload "test"
    const char payload[] = "test";
    bool sent = conn.send_message(zs::MessageType::ping, zs::MessageFlags::none,
                                  payload, 4);
    assert(sent);

    // Receive pong
    zs::IncomingMessage reply;
    // Wait for response
    for (int i = 0; i < 100; ++i) {
      if (conn.recv_message(reply)) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(reply.header.type == zs::MessageType::pong);
    assert(reply.payload.size() == 5);
    assert(std::memcmp(reply.payload.data(), "reply", 5) == 0);

    conn.disconnect();
  });

  // Server: accept and echo
  std::unique_ptr<zs::TcpConnection> server_conn;
  for (int i = 0; i < 200; ++i) {
    server_conn = listener.accept();
    if (server_conn) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(server_conn != nullptr);
  server_conn->socket().set_nonblocking(false);

  zs::IncomingMessage msg;
  bool received = server_conn->recv_message(msg);
  assert(received);
  assert(msg.header.type == zs::MessageType::ping);
  assert(msg.payload.size() == 4);
  assert(std::memcmp(msg.payload.data(), "test", 4) == 0);

  // Reply with pong
  server_conn->send_message(zs::MessageType::pong, zs::MessageFlags::none,
                            "reply", 5);

  client_thread.join();
  server_conn->disconnect();
  listener.stop();

  std::printf("[PASS] test_tcp_loopback\n");
}

static void test_udp_loopback() {
  zs::socket_init();

  auto addr_a = zs::NetworkAddress::from_string("127.0.0.1", 17772);
  auto addr_b = zs::NetworkAddress::from_string("127.0.0.1", 17773);

  zs::UdpSocket sock_a, sock_b;
  assert(sock_a.bind(addr_a));
  assert(sock_b.bind(addr_b));

  // A sends to B
  const char payload[] = "udp_test";
  bool sent = sock_a.send_message(addr_b, zs::MessageType::debug_text,
                                  zs::MessageFlags::none, payload, 8);
  assert(sent);

  // B receives
  zs::UdpDatagram dgram;
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  bool received = sock_b.recv_datagram(dgram);
  assert(received);
  assert(dgram.header.type == zs::MessageType::debug_text);
  assert(dgram.payload.size() == 8);
  assert(std::memcmp(dgram.payload.data(), "udp_test", 8) == 0);

  sock_a.close();
  sock_b.close();

  std::printf("[PASS] test_udp_loopback\n");
}

int main() {
  std::printf("=== Network Transport Smoke Tests ===\n");
  test_address();
  test_socket_create_close();
  test_tcp_loopback();
  test_udp_loopback();
  std::printf("=== All transport smoke tests passed ===\n");
  return 0;
}
