/// @file render_capture.cpp
/// @brief Test for capture/readback/image-writer subsystem.
///        Generates synthetic pixel data, writes to PNG, and
///        verifies the file exists.

#include "zensim/render/capture/RenderReadback.hpp"
#include "zensim/render/capture/RenderImageWriter.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define access _access
#define F_OK 0
#else
#include <unistd.h>
#endif

using namespace zs::render;

static void test_readback_buffer() {
  // Create a small 4x4 RGBA8 synthetic image.
  const uint32_t W = 4, H = 4;
  std::vector<uint8_t> pixels(W * H * 4);
  for (uint32_t y = 0; y < H; ++y) {
    for (uint32_t x = 0; x < W; ++x) {
      size_t idx = (y * W + x) * 4;
      pixels[idx + 0] = static_cast<uint8_t>(x * 64);   // R
      pixels[idx + 1] = static_cast<uint8_t>(y * 64);   // G
      pixels[idx + 2] = 128;                              // B
      pixels[idx + 3] = 255;                              // A
    }
  }

  ReadbackBuffer rb = createReadback(pixels.data(), W, H, 4, 1);
  assert(rb.width == W);
  assert(rb.height == H);
  assert(rb.channels == 4);
  assert(rb.byteSize() == W * H * 4);
  assert(std::memcmp(rb.data.data(), pixels.data(), rb.byteSize()) == 0);

  std::printf("[PASS] test_readback_buffer\n");
}

static void test_write_png() {
  const uint32_t W = 8, H = 8;
  std::vector<uint8_t> pixels(W * H * 4, 0);
  // Red/green gradient
  for (uint32_t y = 0; y < H; ++y) {
    for (uint32_t x = 0; x < W; ++x) {
      size_t idx = (y * W + x) * 4;
      pixels[idx + 0] = static_cast<uint8_t>(x * 32);
      pixels[idx + 1] = static_cast<uint8_t>(y * 32);
      pixels[idx + 2] = 0;
      pixels[idx + 3] = 255;
    }
  }

  const char* path = "H:/zpc_render/test_capture_output.png";
  bool ok = writePNG(path, pixels.data(), W, H);
  assert(ok);

  // Verify file exists.
  assert(access(path, F_OK) == 0);

  std::printf("[PASS] test_write_png (%s)\n", path);
}

static void test_write_depth_png() {
  const uint32_t W = 4, H = 4;
  std::vector<float> depth(W * H);
  for (uint32_t i = 0; i < W * H; ++i)
    depth[i] = static_cast<float>(i) / (W * H - 1);

  const char* path = "H:/zpc_render/test_depth_output.png";
  bool ok = writeDepthPNG(path, depth.data(), W, H, 0.f, 1.f);
  assert(ok);
  assert(access(path, F_OK) == 0);

  std::printf("[PASS] test_write_depth_png (%s)\n", path);
}

int main() {
  std::printf("=== Render Capture Tests ===\n");
  test_readback_buffer();
  test_write_png();
  test_write_depth_png();
  std::printf("=== All render capture tests passed ===\n");
  return 0;
}
