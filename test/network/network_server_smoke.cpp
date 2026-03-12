/// @file network_server_smoke.cpp
/// @brief Smoke tests for the session + server vertical slice.
///
/// Tests:
///   1. SessionManager add/find/remove
///   2. DedicatedServer start/accept/stop with a connecting client
///   3. StateSnapshot delta encode/decode round-trip

#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>

#include "zensim/network/server/DedicatedServer.hpp"
#include "zensim/network/server/ClientSlot.hpp"
#include "zensim/network/replication/StateSnapshot.hpp"

static void test_session_manager_basic() {
  zs::SessionManager sm(4);
  assert(sm.count() == 0);
  assert(sm.max_sessions() == 4);

  // Can't test add_session without a real Connection, but we can verify
  // the manager handles nullptr gracefully and find returns nullptr.
  auto *s = sm.find(1);
  assert(s == nullptr);

  std::printf("[PASS] test_session_manager_basic\n");
}

static void test_client_slot() {
  zs::ClientSlot slot(1);
  assert(slot.session_id() == 1);
  assert(slot.needs_full_snapshot());
  assert(slot.latest_input() == nullptr);

  // Push some inputs
  slot.push_input({10, {1, 2, 3}});
  slot.push_input({11, {4, 5, 6}});
  assert(slot.inputs().size() == 2);
  assert(slot.latest_input()->tick == 11);

  // Clear through tick 10
  slot.clear_inputs_through(10);
  assert(slot.inputs().size() == 1);
  assert(slot.inputs()[0].tick == 11);

  slot.set_last_acked_tick(5);
  assert(slot.last_acked_tick() == 5);

  std::printf("[PASS] test_client_slot\n");
}

static void test_delta_encode_decode() {
  // Create two snapshots
  zs::StateSnapshot snap1;
  snap1.tick = 1;
  snap1.data = {10, 20, 30, 40, 50, 60, 70, 80};

  zs::StateSnapshot snap2;
  snap2.tick = 2;
  snap2.data = {10, 20, 30, 40, 55, 60, 70, 80};  // byte 4 changed

  // Encode delta
  auto delta = zs::DeltaEncoder::encode(snap1, snap2);
  // Delta should be smaller or equal since most bytes unchanged (zeros)
  assert(!delta.empty());

  // Decode
  auto result = zs::DeltaEncoder::decode(snap1, delta, 2);
  assert(result.tick == 2);
  assert(result.data.size() == snap2.data.size());
  assert(result.data == snap2.data);

  std::printf("[PASS] test_delta_encode_decode\n");
}

static void test_delta_full_snapshot() {
  // Empty base → delta is the full snapshot
  zs::StateSnapshot empty;
  zs::StateSnapshot snap;
  snap.tick = 1;
  snap.data = {1, 2, 3, 4, 5};

  auto delta = zs::DeltaEncoder::encode(empty, snap);
  assert(delta == snap.data);

  auto result = zs::DeltaEncoder::decode(empty, delta, 1);
  assert(result.data == snap.data);

  std::printf("[PASS] test_delta_full_snapshot\n");
}

static void test_dedicated_server_lifecycle() {
  zs::DedicatedServer server;
  zs::ServerConfig cfg;
  cfg.bind_address = "127.0.0.1";
  cfg.port         = 17774;
  cfg.max_clients  = 4;
  cfg.tick_rate    = 30.0;
  cfg.timeout_sec  = 5.0;
  server.configure(cfg);

  assert(server.state() == zs::ServerState::stopped);

  std::atomic<bool> got_message{false};
  server.on_message([&](zs::Session &, const zs::IncomingMessage &msg) {
    if (msg.header.type == zs::MessageType::debug_text) {
      got_message = true;
    }
  });

  // Start server in a thread
  std::thread server_thread([&] {
    server.run();
  });

  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  assert(server.state() == zs::ServerState::running);

  // Connect a client
  zs::socket_init();
  auto sock = zs::Socket::create(zs::SocketProtocol::tcp);
  assert(sock.valid());
  auto addr = zs::NetworkAddress::from_string("127.0.0.1", 17774);
  bool connected = sock.connect(addr);
  assert(connected);

  zs::TcpConnection client_conn(std::move(sock), addr);

  // Send a debug_text message
  client_conn.send_message(zs::MessageType::debug_text, zs::MessageFlags::none,
                           "hello", 5);

  // Wait for server to process
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  assert(got_message.load());
  assert(server.client_count() == 1);

  // Stop
  client_conn.disconnect();
  server.stop();
  server_thread.join();

  assert(server.state() == zs::ServerState::stopped);

  std::printf("[PASS] test_dedicated_server_lifecycle\n");
}

int main() {
  std::printf("=== Network Server Smoke Tests ===\n");
  test_session_manager_basic();
  test_client_slot();
  test_delta_encode_decode();
  test_delta_full_snapshot();
  test_dedicated_server_lifecycle();
  std::printf("=== All server smoke tests passed ===\n");
  return 0;
}
