/// @file render_postprocess.cpp
/// @brief Test for the Vulkan deferred renderer with post-processing
///        (bloom, tone mapping, FXAA).
///
/// Renders a Cornell Box scene using the full deferred pipeline (shadow
/// mapping + SSAO + bloom + tone mapping + FXAA), verifies the output is
/// non-trivial, and checks that the post-processing pipeline produces
/// reasonable results (correct brightness range, spatial variance, etc.).
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
// Helper: compute brightness variance
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
// Helper: count pixels that are not completely black
// -------------------------------------------------------------------

static size_t countNonBlackPixels(const uint8_t* pixels, uint32_t w, uint32_t h) {
  size_t count = 0;
  const size_t n = static_cast<size_t>(w) * h;
  for (size_t i = 0; i < n; ++i) {
    if (pixels[i * 4 + 0] > 0 || pixels[i * 4 + 1] > 0 || pixels[i * 4 + 2] > 0) {
      ++count;
    }
  }
  return count;
}

// -------------------------------------------------------------------
// Helper: check that no pixel exceeds a given brightness
//         (tone mapping should prevent clipping)
// -------------------------------------------------------------------

static size_t countClippedPixels(const uint8_t* pixels, uint32_t w, uint32_t h) {
  size_t count = 0;
  const size_t n = static_cast<size_t>(w) * h;
  for (size_t i = 0; i < n; ++i) {
    // A pixel is "clipped" if all RGB channels are exactly 255
    if (pixels[i * 4 + 0] == 255 &&
        pixels[i * 4 + 1] == 255 &&
        pixels[i * 4 + 2] == 255) {
      ++count;
    }
  }
  return count;
}

// -------------------------------------------------------------------
// Test: deferred renderer with full post-processing on Cornell box
// -------------------------------------------------------------------

static void test_postprocess_cornell_box() {
  auto renderer = createVulkanDeferredRenderer();
  if (!renderer) {
    std::printf("[SKIP] Vulkan not available — skipping post-process test\n");
    return;
  }

  bool ok = renderer->init();
  if (!ok) {
    std::printf("[SKIP] Vulkan init failed — skipping post-process test\n");
    return;
  }

  std::printf("[postprocess] renderer: %s\n", renderer->name());

  // Build scene — Cornell box with area light
  RenderScene scene = createCornellBoxScene();
  assert(!scene.empty() && "Cornell box scene must not be empty");
  std::printf("[postprocess] scene: %zu instances, %zu materials, %zu lights\n",
              scene.instances().size(), scene.materials().size(),
              scene.lights().size());

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

  // Render with full post-processing pipeline
  RasterResult result = renderer->render(req, 0);
  assert(result.success && "Post-process render must succeed");
  assert(!result.color.data.empty() && "Post-process result must have color data");
  assert(result.color.width == 512 && result.color.height == 512);

  std::printf("[postprocess] render time: %.1f ms\n", result.render_time_us / 1000.0);

  // Save PNG
  std::filesystem::create_directories(k_output_dir);
  std::string path = std::string(k_output_dir) + "/postprocess_cornell_box.png";
  bool wrote = writePNG(path,
                        result.color.data.data(),
                        result.color.width, result.color.height);
  assert(wrote && "writePNG must succeed");
  std::printf("[postprocess] wrote: %s\n", path.c_str());

  // --- Verification ---

  const uint8_t* pixels = result.color.data.data();
  const uint32_t w = result.color.width;
  const uint32_t h = result.color.height;

  // 1. Average brightness should be in a reasonable range
  //    (tone mapping compresses HDR to LDR, so values should be moderate)
  double avg = averageBrightness(pixels, w, h);
  std::printf("[postprocess] average brightness: %.1f / 255\n", avg);
  assert(avg > 5.0 && "Post-process image must not be all black");
  assert(avg < 250.0 && "Post-process image must not be all white");

  // 2. Brightness variance — should have contrast
  double variance = brightnessVariance(pixels, w, h, avg);
  std::printf("[postprocess] brightness variance: %.1f\n", variance);
  assert(variance > 50.0 && "Post-process image must have contrast");

  // 3. Non-black pixel coverage — most pixels should be non-black
  size_t nonBlack = countNonBlackPixels(pixels, w, h);
  double nonBlackRatio = static_cast<double>(nonBlack) / (w * h);
  std::printf("[postprocess] non-black pixel ratio: %.2f%%\n", nonBlackRatio * 100.0);
  assert(nonBlackRatio > 0.5 && "At least 50%% of pixels should be non-black");

  // 4. Clipped (pure white) pixel count — tone mapping should minimize clipping
  size_t clipped = countClippedPixels(pixels, w, h);
  double clippedRatio = static_cast<double>(clipped) / (w * h);
  std::printf("[postprocess] clipped pixel ratio: %.2f%%\n", clippedRatio * 100.0);
  // ACES tone mapping should prevent most pure-white clipping (< 5% is generous)
  assert(clippedRatio < 0.05 && "Tone mapping should prevent most pure-white clipping");

  renderer->shutdown();
  std::printf("[PASS] test_postprocess_cornell_box\n");
}

// -------------------------------------------------------------------
// Main
// -------------------------------------------------------------------

int main() {
  std::printf("=== Render Post-Processing Tests ===\n");

  test_postprocess_cornell_box();

  std::printf("=== Post-processing tests complete ===\n");
  return 0;
}
