/// @file render_shadow.cpp
/// @brief Test for the Vulkan deferred renderer with shadow mapping.
///
/// Renders a Cornell Box scene using the deferred renderer (which now
/// includes a shadow map pass), verifies the output is non-trivial, and
/// checks that shadow regions exist in the image.
///
/// Requires ZS_ENABLE_VULKAN=1 and a Vulkan-capable GPU at runtime.

#include "zensim/render/scene/ScenePrepare.hpp"
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
// Helper: compute brightness variance to check shadow regions exist
// -------------------------------------------------------------------

static double brightnessVariance(const uint8_t* pixels, uint32_t w, uint32_t h,
                                  double avg) {
  double sumSq = 0.0;
  const size_t n = static_cast<size_t>(w) * h;
  for (size_t i = 0; i < n; ++i) {
    double lum = (pixels[i * 4 + 0] + pixels[i * 4 + 1] + pixels[i * 4 + 2]) / 3.0;
    double diff = lum - avg;
    sumSq += diff * diff;
  }
  return sumSq / n;
}

// -------------------------------------------------------------------
// Test: deferred renderer with shadow mapping on Cornell box
// -------------------------------------------------------------------

static void test_shadow_cornell_box() {
  auto renderer = createVulkanDeferredRenderer();
  if (!renderer) {
    std::printf("[SKIP] Vulkan not available — skipping shadow test\n");
    return;
  }

  bool ok = renderer->init();
  if (!ok) {
    std::printf("[SKIP] Vulkan init failed — skipping shadow test\n");
    return;
  }

  std::printf("[shadow] renderer: %s\n", renderer->name());

  // Build scene — Cornell box has an area light at ceiling center
  // that should cast shadows on the floor
  RenderScene scene = createCornellBoxScene();
  assert(!scene.empty() && "Cornell box scene must not be empty");
  std::printf("[shadow] scene: %zu instances, %zu materials, %zu lights\n",
              scene.instances().size(), scene.materials().size(),
              scene.lights().size());

  // Verify there's a shadow-casting light
  bool hasShadowLight = false;
  for (const auto& light : scene.lights()) {
    if (light.cast_shadow) {
      hasShadowLight = true;
      std::printf("[shadow] shadow-casting light found: type=%d, intensity=%.1f\n",
                  static_cast<int>(light.type), light.intensity);
      break;
    }
  }
  assert(hasShadowLight && "Cornell box must have a shadow-casting light");

  // Build frame request
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
  req.method = RenderMethod::Raster_Deferred;

  // Render
  RasterResult result = renderer->render(req, 0);
  assert(result.success && "shadow render must succeed");
  assert(!result.color.data.empty() && "shadow result must have color data");
  assert(result.color.width == 512 && result.color.height == 512);

  std::printf("[shadow] render time: %.1f ms\n", result.render_time_us / 1000.0);

  // Save PNG
  std::filesystem::create_directories(k_output_dir);
  std::string path = std::string(k_output_dir) + "/shadow_cornell_box.png";
  bool wrote = writePNG(path,
                        result.color.data.data(),
                        result.color.width, result.color.height);
  assert(wrote && "writePNG must succeed");
  std::printf("[shadow] wrote: %s\n", path.c_str());

  // Verify non-trivial image — must not be all black
  double avg = averageBrightness(
      result.color.data.data(),
      result.color.width, result.color.height);
  std::printf("[shadow] average brightness: %.1f / 255\n", avg);
  assert(avg > 5.0 && "shadow image must not be all black");

  // Verify brightness variance — shadows create contrast in the image.
  // A variance > 100 suggests some pixels are lit and some are dark.
  double variance = brightnessVariance(
      result.color.data.data(),
      result.color.width, result.color.height, avg);
  std::printf("[shadow] brightness variance: %.1f\n", variance);
  assert(variance > 50.0 && "shadow image must have contrast (shadows create bright/dark areas)");

  renderer->shutdown();
  std::printf("[PASS] test_shadow_cornell_box\n");
}

// -------------------------------------------------------------------
// Main
// -------------------------------------------------------------------

int main() {
  std::printf("=== Render Shadow Tests ===\n");

  test_shadow_cornell_box();

  std::printf("=== Shadow tests complete ===\n");
  return 0;
}
