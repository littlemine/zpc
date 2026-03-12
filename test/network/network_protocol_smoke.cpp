/// @file network_protocol_smoke.cpp
/// @brief Smoke tests for the network protocol layer.
///
/// Tests:
///   1. MessageHeader serialization round-trip
///   2. WriteBuffer / ReadBuffer round-trip
///   3. SequenceBuffer insert, find, wrap-around
///   4. MessageType name lookup

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "zensim/network/protocol/MessageHeader.hpp"
#include "zensim/network/protocol/Serialization.hpp"
#include "zensim/network/protocol/SequenceBuffer.hpp"

static void test_message_header_roundtrip() {
  zs::MessageHeader hdr;
  hdr.type         = zs::MessageType::state_delta;
  hdr.flags        = zs::MessageFlags::reliable | zs::MessageFlags::compressed;
  hdr.payload_size = 12345;

  uint8_t buf[zs::MessageHeader::kSize];
  hdr.write_to(buf);

  auto hdr2 = zs::MessageHeader::read_from(buf);
  assert(hdr2.is_valid());
  assert(hdr2.magic   == zs::MessageHeader::kMagic);
  assert(hdr2.version == zs::MessageHeader::kVersion);
  assert(hdr2.type    == zs::MessageType::state_delta);
  assert(zs::has_flag(hdr2.flags, zs::MessageFlags::reliable));
  assert(zs::has_flag(hdr2.flags, zs::MessageFlags::compressed));
  assert(!zs::has_flag(hdr2.flags, zs::MessageFlags::encrypted));
  assert(hdr2.payload_size == 12345);

  std::printf("[PASS] test_message_header_roundtrip\n");
}

static void test_write_read_buffer() {
  zs::WriteBuffer wb;
  wb.write_u8(42);
  wb.write_u16(1000);
  wb.write_u32(100000);
  wb.write_u64(9999999999ULL);
  wb.write_i32(-42);
  wb.write_f32(3.14f);
  wb.write_f64(2.718281828);
  wb.write_string("hello world");

  zs::ReadBuffer rb(wb.data(), wb.size());
  assert(!rb.error());
  assert(rb.read_u8()  == 42);
  assert(rb.read_u16() == 1000);
  assert(rb.read_u32() == 100000);
  assert(rb.read_u64() == 9999999999ULL);
  assert(rb.read_i32() == -42);

  float f = rb.read_f32();
  assert(f > 3.13f && f < 3.15f);

  double d = rb.read_f64();
  assert(d > 2.718 && d < 2.719);

  std::string s = rb.read_string();
  assert(s == "hello world");

  assert(rb.at_end());
  assert(!rb.error());

  std::printf("[PASS] test_write_read_buffer\n");
}

static void test_read_buffer_overread() {
  uint8_t data[] = {1, 2, 3};
  zs::ReadBuffer rb(data, sizeof(data));
  rb.read_u8();
  rb.read_u8();
  rb.read_u8();
  assert(rb.at_end());
  // Over-read should set error
  rb.read_u8();
  assert(rb.error());

  std::printf("[PASS] test_read_buffer_overread\n");
}

static void test_sequence_buffer() {
  zs::SequenceBuffer<int, 256> sb;

  // Insert sequential entries
  for (uint16_t i = 0; i < 100; ++i) {
    int *entry = sb.insert(i);
    assert(entry != nullptr);
    *entry = static_cast<int>(i * 10);
  }

  // Find existing entries
  for (uint16_t i = 0; i < 100; ++i) {
    assert(sb.exists(i));
    const int *entry = sb.find(i);
    assert(entry != nullptr);
    assert(*entry == static_cast<int>(i * 10));
  }

  // Remove an entry
  sb.remove(50);
  assert(!sb.exists(50));

  std::printf("[PASS] test_sequence_buffer\n");
}

static void test_sequence_more_recent() {
  // Basic comparison
  assert(zs::sequence_more_recent(10, 5));
  assert(!zs::sequence_more_recent(5, 10));

  // Wrap-around
  assert(zs::sequence_more_recent(0, 65535));  // 0 is "after" 65535
  assert(!zs::sequence_more_recent(65535, 0));

  std::printf("[PASS] test_sequence_more_recent\n");
}

static void test_message_type_name() {
  assert(std::strcmp(zs::message_type_name(zs::MessageType::heartbeat), "heartbeat") == 0);
  assert(std::strcmp(zs::message_type_name(zs::MessageType::ping), "ping") == 0);
  assert(std::strcmp(zs::message_type_name(static_cast<zs::MessageType>(0x99)), "unknown") == 0);

  std::printf("[PASS] test_message_type_name\n");
}

int main() {
  std::printf("=== Network Protocol Smoke Tests ===\n");
  test_message_header_roundtrip();
  test_write_read_buffer();
  test_read_buffer_overread();
  test_sequence_buffer();
  test_sequence_more_recent();
  test_message_type_name();
  std::printf("=== All protocol smoke tests passed ===\n");
  return 0;
}
