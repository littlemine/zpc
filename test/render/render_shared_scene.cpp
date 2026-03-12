/// @file render_shared_scene.cpp
/// @brief Test that a single RenderScene can be rendered by both
///        the path tracer and the rasterizer.
///
/// This validates the PR3 shared scene/material infrastructure:
///  1. createCornellBoxScene() builds a RenderScene.
///  2. The path tracer's uploadScene(RenderScene) overload calls
///     flattenForPathTracer() internally, then renders.
///  3. The rasterizer renders the same RenderScene using per-instance
///     material base_color from push constants.
///  4. Both outputs are saved as PNG and verified non-trivial.
///
/// Requires ZS_ENABLE_VULKAN=1 and a Vulkan-capable GPU at runtime.

#include "zensim/render/scene/ScenePrepare.hpp"
#include "zensim/render/offline/PathTracer.hpp"
#include "zensim/render/realtime/RasterRenderer.hpp"
#include "zensim/render/RenderFrameRequest.hpp"
#include "zensim/render/capture/RenderImageWriter.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

using namespace zs::render;

static const char* k_output_dir = "H:/zpc_render";

// -------------------------------------------------------------------
// Helper: compute average brightness of an RGBA8 pixel buffer
// -------------------------------------------------------------------

static double averageBrightness(const uint8_t* pixels, uint32_t w, uint32_t h) {
  uint64_t sum = 0;
  const size_t n = static_cast<size_t>(w) * h;
  for (size_t i = 0; i < n; ++i) {
    sum += pixels[i * 4 + 0];
    sum += pixels[i * 4 + 1];
    sum += pixels[i * 4 + 2];
  }
  return static_cast<double>(sum) / (n * 3.0);
}

// -------------------------------------------------------------------
// Test: shared scene rendered by path tracer
// -------------------------------------------------------------------

static void test_shared_scene_pathtrace(const RenderScene& scene) {
  auto pt = createVulkanPathTracer();
  if (!pt) {
    std::printf("[SKIP] Vulkan not available — skipping shared scene path trace\n");
    return;
  }

  bool ok = pt->init();
  if (!ok) {
    std::printf("[SKIP] Vulkan init failed — skipping shared scene path trace\n");
    return;
  }

  // Upload via the new RenderScene overload.
  pt->uploadScene(scene);

  // Camera looking from the opening toward the back wall.
  Camera camera;
  camera.position = zs::vec<zs::f32, 3>{2.775f, 2.775f, -5.0f};
  camera.target   = zs::vec<zs::f32, 3>{2.775f, 2.775f,  2.775f};
  camera.up       = zs::vec<zs::f32, 3>{0.f, 1.f, 0.f};
  camera.fov_y_radians = 0.6911f;
  camera.aspect_ratio  = 1.f;
  camera.near_plane    = 0.1f;
  camera.far_plane     = 100.f;

  PathTracerConfig config;
  config.width  = 512;
  config.height = 512;
  config.samples_per_pixel = 32;  // fewer spp for faster test
  config.max_bounces = 6;

  PathTraceResult result = pt->render(camera, config);
  assert(result.success && "shared scene path trace must succeed");
  assert(result.pixels.size() == 512u * 512u * 4u);

  // Save PNG.
  std::filesystem::create_directories(k_output_dir);
  std::string path = std::string(k_output_dir) + "/shared_scene_pathtrace.png";
  bool wrote = writePNG(path, result.pixels.data(), result.width, result.height);
  assert(wrote && "writePNG must succeed");
  std::printf("[pt] wrote: %s\n", path.c_str());

  // Verify non-trivial image.
  double avg = averageBrightness(result.pixels.data(), result.width, result.height);
  std::printf("[pt] average brightness: %.1f / 255\n", avg);
  assert(avg > 5.0 && "path traced image must not be all black");

  pt->shutdown();
  std::printf("[PASS] test_shared_scene_pathtrace\n");
}

// -------------------------------------------------------------------
// Test: shared scene rendered by rasterizer
// -------------------------------------------------------------------

static void test_shared_scene_raster(const RenderScene& scene) {
  auto rast = createVulkanRasterRenderer();
  if (!rast) {
    std::printf("[SKIP] Vulkan not available — skipping shared scene raster\n");
    return;
  }

  bool ok = rast->init();
  if (!ok) {
    std::printf("[SKIP] Vulkan init failed — skipping shared scene raster\n");
    return;
  }

  // Build a frame request around the shared scene.
  Camera camera;
  camera.position = zs::vec<zs::f32, 3>{2.775f, 2.775f, -5.0f};
  camera.target   = zs::vec<zs::f32, 3>{2.775f, 2.775f,  2.775f};
  camera.up       = zs::vec<zs::f32, 3>{0.f, 1.f, 0.f};
  camera.fov_y_radians = 0.6911f;
  camera.aspect_ratio  = 1.f;
  camera.near_plane    = 0.1f;
  camera.far_plane     = 100.f;

  Viewport vp;
  vp.width  = 512;
  vp.height = 512;

  RenderView view;
  view.camera   = camera;
  view.viewport = vp;

  RenderFrameRequest req;
  req.scene = std::make_shared<const RenderScene>(scene);
  req.views.push_back(view);
  req.method = RenderMethod::Raster_Forward;

  RasterResult result = rast->render(req, 0);
  assert(result.success && "shared scene raster must succeed");
  assert(!result.color.data.empty() && "raster result must have color data");

  // Save PNG.
  std::filesystem::create_directories(k_output_dir);
  std::string path = std::string(k_output_dir) + "/shared_scene_raster.png";
  bool wrote = writePNG(path,
                        result.color.data.data(),
                        result.color.width, result.color.height);
  assert(wrote && "writePNG must succeed");
  std::printf("[raster] wrote: %s\n", path.c_str());

  // Verify non-trivial image.
  double avg = averageBrightness(
      result.color.data.data(),
      result.color.width, result.color.height);
  std::printf("[raster] average brightness: %.1f / 255\n", avg);
  assert(avg > 5.0 && "rasterised image must not be all black");

  rast->shutdown();
  std::printf("[PASS] test_shared_scene_raster\n");
}

// -------------------------------------------------------------------
// Main
// -------------------------------------------------------------------

int main() {
  std::printf("=== Render Shared Scene Tests ===\n");

  // Build one scene, render with both backends.
  RenderScene scene = createCornellBoxScene();
  assert(!scene.empty() && "Cornell box scene must not be empty");
  std::printf("[shared] scene: %zu instances, %zu materials, %zu lights\n",
              scene.instances().size(), scene.materials().size(),
              scene.lights().size());

  test_shared_scene_pathtrace(scene);
  test_shared_scene_raster(scene);

  std::printf("=== Shared scene tests complete ===\n");
  return 0;
}
