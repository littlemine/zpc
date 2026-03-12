/// @file render_e2e.cpp
/// @brief End-to-end smoke test: loads monkey.obj, renders with
///        VulkanRasterRenderer (headless offscreen), captures the
///        output to PNG, and verifies the artifact.
///
/// Requires ZS_ENABLE_VULKAN=1 and a Vulkan-capable GPU at runtime.

#include "zensim/render/realtime/RasterRenderer.hpp"
#include "zensim/render/scene/RenderSceneImport.hpp"
#include "zensim/render/capture/RenderImageWriter.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

using namespace zs::render;

// -------------------------------------------------------------------
// Paths
// -------------------------------------------------------------------

// The OBJ file is in the zpc_assets submodule.
// At build time the working directory is typically the build dir,
// so we use an absolute path derived from the source tree.
// Override via RENDER_ASSET_DIR env var if needed.
static std::string asset_path() {
#ifdef RENDER_ASSET_DIR
  return std::string(RENDER_ASSET_DIR) + "/TriMesh/monkey.obj";
#else
  return "zpc_assets/TriMesh/monkey.obj";
#endif
}

static const char* k_output_dir = "H:/zpc_render";

// -------------------------------------------------------------------
// Test: full pipeline
// -------------------------------------------------------------------

static void test_e2e_vulkan_raster() {
  // 1. Create renderer.
  auto renderer = createVulkanRasterRenderer();
  if (!renderer) {
    std::printf("[SKIP] Vulkan not available — skipping e2e test\n");
    return;
  }

  // 2. Initialise.
  bool ok = renderer->init();
  if (!ok) {
    std::printf("[SKIP] Vulkan init failed — skipping e2e test\n");
    return;
  }
  std::printf("[e2e] renderer: %s\n", renderer->name());

  // 3. Load scene from OBJ.
  std::string objPath = asset_path();
  std::printf("[e2e] loading scene from: %s\n", objPath.c_str());

  SceneImportOptions importOpts;
  importOpts.uniform_scale = 1.f;
  importOpts.recenter = true;
  importOpts.auto_default_material = true;
  importOpts.auto_default_light = true;

  auto scene = std::make_shared<RenderScene>(
      importOBJ(objPath, "monkey", importOpts));

  if (scene->empty()) {
    std::printf("[SKIP] failed to load OBJ '%s' — skipping e2e test\n",
                objPath.c_str());
    renderer->shutdown();
    return;
  }
  std::printf("[e2e] scene: %zu meshes, %zu instances\n",
              scene->meshes().size(), scene->instanceCount());

  // 4. Build frame request.
  RenderFrameRequest request;
  request.frame_id = 1;
  request.label = "e2e_smoke";
  request.scene = scene;
  request.method = RenderMethod::Raster_Forward;
  request.backend = RenderBackend::Vulkan;
  request.write_artifacts = false;  // we write manually below
  request.artifact_root = k_output_dir;

  RenderView view;
  view.name = "main";
  view.viewport.width = 512;
  view.viewport.height = 512;
  view.camera.position = zs::vec<zs::f32, 3>{0.f, 0.f, 3.f};
  view.camera.target   = zs::vec<zs::f32, 3>{0.f, 0.f, 0.f};
  view.camera.up       = zs::vec<zs::f32, 3>{0.f, 1.f, 0.f};
  view.camera.fov_y_radians = 0.7854f;  // 45 degrees
  view.camera.aspect_ratio  = 1.f;      // square
  view.camera.near_plane    = 0.1f;
  view.camera.far_plane     = 100.f;
  request.views.push_back(view);

  // 5. Render.
  std::printf("[e2e] rendering %ux%u ...\n",
              view.viewport.width, view.viewport.height);

  RasterResult result = renderer->render(request, 0);

  if (!result.success) {
    std::printf("[FAIL] render failed: %s\n", result.error.c_str());
    renderer->shutdown();
    assert(false && "render must succeed");
    return;
  }

  std::printf("[e2e] render OK — %llu us, %zu bytes readback\n",
              static_cast<unsigned long long>(result.render_time_us),
              result.color.byteSize());

  // Sanity checks on readback.
  assert(result.color.width == 512);
  assert(result.color.height == 512);
  assert(result.color.channels == 4);
  assert(result.color.byteSize() == 512u * 512u * 4u);
  assert(!result.color.data.empty());

  // 6. Write PNG artifact.
  std::filesystem::create_directories(k_output_dir);
  std::string colorPath = std::string(k_output_dir) + "/e2e_monkey_color.png";

  bool wrote = writePNG(colorPath, result.color.data.data(),
                        result.color.width, result.color.height);
  assert(wrote && "writePNG must succeed");
  std::printf("[e2e] wrote: %s\n", colorPath.c_str());

  // Verify file exists and has non-trivial size.
  assert(std::filesystem::exists(colorPath));
  auto fileSize = std::filesystem::file_size(colorPath);
  assert(fileSize > 100);  // a real PNG header is ~67 bytes minimum
  std::printf("[e2e] file size: %llu bytes\n",
              static_cast<unsigned long long>(fileSize));

  // 7. Check that the image isn't all black (at least some non-zero pixels).
  bool hasNonZero = false;
  for (size_t i = 0; i < result.color.data.size(); i += 4) {
    if (result.color.data[i] > 0 || result.color.data[i+1] > 0 ||
        result.color.data[i+2] > 0) {
      hasNonZero = true;
      break;
    }
  }
  assert(hasNonZero && "rendered image must not be all black");

  // 8. Shutdown.
  renderer->shutdown();

  std::printf("[PASS] test_e2e_vulkan_raster\n");
}

// -------------------------------------------------------------------
// Main
// -------------------------------------------------------------------

int main() {
  std::printf("=== Render End-to-End Tests ===\n");
  test_e2e_vulkan_raster();
  std::printf("=== End-to-end test complete ===\n");
  return 0;
}
