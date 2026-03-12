/// @file render_pathtrace.cpp
/// @brief Test for the Vulkan compute path tracer.
///
/// Creates a procedural Cornell Box scene, builds a BVH, uploads to
/// the GPU, path-traces 512x512 at 64 spp, saves to PNG, and verifies
/// the output is non-trivial.
///
/// Requires ZS_ENABLE_VULKAN=1 and a Vulkan-capable GPU at runtime.

#include "zensim/render/offline/PathTracer.hpp"
#include "zensim/render/capture/RenderImageWriter.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

using namespace zs::render;

static const char* k_output_dir = "H:/zpc_render";

// -------------------------------------------------------------------
// Test: Cornell Box path trace
// -------------------------------------------------------------------

static void test_pathtrace_cornell_box() {
  // 1. Create path tracer.
  auto pt = createVulkanPathTracer();
  if (!pt) {
    std::printf("[SKIP] Vulkan not available — skipping path trace test\n");
    return;
  }

  // 2. Initialise.
  bool ok = pt->init();
  if (!ok) {
    std::printf("[SKIP] Vulkan init failed — skipping path trace test\n");
    return;
  }
  std::printf("[pt] renderer: %s\n", pt->name());

  // 3. Create Cornell Box scene.
  PathTracerScene scene = createCornellBox();
  assert(!scene.positions.empty() && "Cornell box must have vertices");
  assert(!scene.triangles.empty() && "Cornell box must have triangles");
  assert(!scene.materials.empty() && "Cornell box must have materials");
  std::printf("[pt] scene: %zu tris, %zu verts, %zu mats\n",
              scene.triangles.size(), scene.positions.size(),
              scene.materials.size());

  // 4. Upload scene.
  pt->uploadScene(scene);

  // 5. Configure camera.
  //    Camera looks from the opening of the box toward the back wall.
  //    The Cornell box spans [0, 5.55] in all axes.
  Camera camera;
  camera.position = zs::vec<zs::f32, 3>{2.775f, 2.775f, -5.0f};
  camera.target   = zs::vec<zs::f32, 3>{2.775f, 2.775f,  2.775f};
  camera.up       = zs::vec<zs::f32, 3>{0.f, 1.f, 0.f};
  camera.fov_y_radians = 0.6911f;  // ~39.6 degrees (classic Cornell box FOV)
  camera.aspect_ratio  = 1.f;
  camera.near_plane    = 0.1f;
  camera.far_plane     = 100.f;

  // 6. Render.
  PathTracerConfig config;
  config.width = 512;
  config.height = 512;
  config.samples_per_pixel = 64;
  config.max_bounces = 8;

  std::printf("[pt] rendering %ux%u, %u spp, %u bounces ...\n",
              config.width, config.height,
              config.samples_per_pixel, config.max_bounces);

  PathTraceResult result = pt->render(camera, config);

  if (!result.success) {
    std::printf("[FAIL] render failed: %s\n", result.error.c_str());
    pt->shutdown();
    assert(false && "path trace render must succeed");
    return;
  }

  std::printf("[pt] render OK — %.1f ms\n", result.render_time_us / 1000.0);

  // 7. Verify result dimensions and data.
  assert(result.width == 512);
  assert(result.height == 512);
  assert(result.pixels.size() == 512u * 512u * 4u);

  // 8. Write PNG.
  std::filesystem::create_directories(k_output_dir);
  std::string outPath = std::string(k_output_dir) + "/pathtrace_cornell.png";

  bool wrote = writePNG(outPath, result.pixels.data(),
                        result.width, result.height);
  assert(wrote && "writePNG must succeed");
  std::printf("[pt] wrote: %s\n", outPath.c_str());

  // 9. Verify file exists and has reasonable size.
  assert(std::filesystem::exists(outPath));
  auto fileSize = std::filesystem::file_size(outPath);
  assert(fileSize > 100);
  std::printf("[pt] file size: %llu bytes\n",
              static_cast<unsigned long long>(fileSize));

  // 10. Check that the image isn't all black.
  bool hasNonZero = false;
  for (size_t i = 0; i < result.pixels.size(); i += 4) {
    if (result.pixels[i] > 0 || result.pixels[i + 1] > 0 ||
        result.pixels[i + 2] > 0) {
      hasNonZero = true;
      break;
    }
  }
  assert(hasNonZero && "path traced image must not be all black");

  // 11. Check some pixels have reasonable brightness (not all near-zero).
  //     Sum up all pixel values; for a proper Cornell box render with a
  //     bright light, the average should be well above zero.
  uint64_t pixelSum = 0;
  for (size_t i = 0; i < result.pixels.size(); i += 4) {
    pixelSum += result.pixels[i] + result.pixels[i + 1] + result.pixels[i + 2];
  }
  double avgBrightness = static_cast<double>(pixelSum) / (512.0 * 512.0 * 3.0);
  std::printf("[pt] average brightness: %.1f / 255\n", avgBrightness);
  assert(avgBrightness > 5.0 && "average brightness should be non-trivial");

  // 12. Shutdown.
  pt->shutdown();

  std::printf("[PASS] test_pathtrace_cornell_box\n");
}

// -------------------------------------------------------------------
// Main
// -------------------------------------------------------------------

int main() {
  std::printf("=== Render Path Trace Tests ===\n");
  test_pathtrace_cornell_box();
  std::printf("=== Path trace tests complete ===\n");
  return 0;
}
