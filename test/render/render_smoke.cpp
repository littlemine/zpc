/// @file render_smoke.cpp
/// @brief Smoke test for render infrastructure: constructs a scene
///        from RenderTypes, builds a frame request, and verifies
///        the data-model round-trips correctly (no GPU needed).

#include "zensim/render/RenderTypes.hpp"
#include "zensim/render/RenderScene.hpp"
#include "zensim/render/RenderView.hpp"
#include "zensim/render/RenderFrameRequest.hpp"
#include "zensim/render/RenderArtifact.hpp"
#include "zensim/render/RenderManifest.hpp"
#include "zensim/render/RenderBackendSelection.hpp"
#include "zensim/render/scene/RenderSceneBuilder.hpp"

#include <cassert>
#include <cstdio>
#include <memory>

using namespace zs::render;

// -------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------
static void test_render_types() {
  // Camera defaults
  Camera cam;
  assert(cam.position[0] == 0.f && cam.position[1] == 0.f && cam.position[2] == 5.f);
  assert(cam.projection == ProjectionType::Perspective);
  assert(cam.near_plane > 0.f);
  assert(cam.far_plane > cam.near_plane);

  // Material defaults
  Material mat;
  mat.id = static_cast<MaterialId>(1);
  mat.name = "test_material";
  assert(mat.shading == ShadingModel::Lambert);
  assert(mat.metallic == 0.f);

  // Light defaults
  Light light;
  assert(light.type == LightType::Directional);
  assert(light.cast_shadow == true);

  // Viewport defaults
  Viewport vp;
  assert(vp.width == 1920 && vp.height == 1080);

  std::printf("[PASS] test_render_types\n");
}

static void test_scene_builder() {
  MeshRef mesh;
  mesh.id = static_cast<MeshId>(1);
  mesh.name = "monkey";
  mesh.vertex_count = 512;
  mesh.index_count = 1024;

  Material mat;
  mat.id = static_cast<MaterialId>(1);
  mat.name = "default_lambert";

  InstanceRef inst;
  inst.id = static_cast<SceneObjectId>(1);
  inst.mesh = mesh.id;
  inst.material = mat.id;

  Light sun;
  sun.type = LightType::Directional;
  sun.direction = zs::vec<zs::f32, 3>{0.f, -1.f, -0.5f};

  auto scene = RenderSceneBuilder("smoke_scene")
    .addMesh(mesh)
    .addMaterial(mat)
    .addInstance(inst)
    .addLight(sun)
    .build();

  assert(scene.name() == "smoke_scene");
  assert(scene.instanceCount() == 1);
  assert(!scene.empty());
  assert(scene.meshes().size() == 1);
  assert(scene.materials().size() == 1);
  assert(scene.lights().size() == 1);

  // Look-ups
  auto* found_mesh = scene.findMesh(static_cast<MeshId>(1));
  assert(found_mesh != nullptr);
  assert(found_mesh->name == "monkey");

  auto* found_mat = scene.findMaterial(static_cast<MaterialId>(1));
  assert(found_mat != nullptr);

  auto* found_inst = scene.findInstance(static_cast<SceneObjectId>(1));
  assert(found_inst != nullptr);

  // Miss
  assert(scene.findMesh(static_cast<MeshId>(999)) == nullptr);

  std::printf("[PASS] test_scene_builder\n");
}

static void test_frame_request() {
  auto scene = std::make_shared<RenderScene>(
    RenderSceneBuilder("frame_test").build());

  RenderView view;
  view.name = "main";
  view.viewport.width = 800;
  view.viewport.height = 600;

  RenderFrameRequest req;
  req.frame_id = 42;
  req.label = "smoke_frame";
  req.scene = scene;
  req.views.push_back(view);
  req.method = RenderMethod::Raster_Forward;
  req.backend = RenderBackend::Vulkan;
  req.write_artifacts = false;

  assert(req.frame_id == 42);
  assert(req.views.size() == 1);
  assert(req.views[0].viewport.width == 800);

  std::printf("[PASS] test_frame_request\n");
}

static void test_artifact_types() {
  RenderArtifact art;
  art.frame_id = 1;
  art.view_name = "main";
  art.kind = ArtifactKind::ColorImage;
  art.width = 1920;
  art.height = 1080;
  art.file_path = "H:/zpc_render/runs/test/color.png";

  assert(art.width == 1920);
  assert(art.kind == ArtifactKind::ColorImage);

  RenderManifest manifest;
  manifest.run_id = "20260313_001";
  manifest.scene_name = "smoke";
  manifest.artifacts.push_back(art);
  assert(manifest.artifacts.size() == 1);

  std::printf("[PASS] test_artifact_types\n");
}

int main() {
  std::printf("=== Render Smoke Tests ===\n");
  test_render_types();
  test_scene_builder();
  test_frame_request();
  test_artifact_types();
  std::printf("=== All render smoke tests passed ===\n");
  return 0;
}
