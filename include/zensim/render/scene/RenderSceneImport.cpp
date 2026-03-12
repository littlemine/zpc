/// @file RenderSceneImport.cpp
/// @brief Bridges zpc's load_obj() into the render scene system.

#include "zensim/render/scene/RenderSceneImport.hpp"
#include "zensim/io/MeshIO.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace zs {
namespace render {

RenderScene importOBJ(const std::string& path,
                      const std::string& name,
                      const SceneImportOptions& opts) {
  // Derive scene name from path if not provided.
  std::string scene_name = name;
  if (scene_name.empty()) {
    auto stem = std::filesystem::path(path).stem().string();
    scene_name = stem.empty() ? "imported" : stem;
  }

  RenderSceneBuilder builder(scene_name);

  // Load raw geometry via zpc's OBJ loader.
  std::vector<std::array<float, 3>> positions;
  std::vector<std::array<float, 3>> normals;
  std::vector<std::array<float, 2>> uvs;
  std::vector<std::array<uint32_t, 3>> triangles;

  bool ok = zs::load_obj(path, &positions, &normals, &uvs, &triangles);
  if (!ok) {
    std::fprintf(stderr, "[RenderSceneImport] failed to load OBJ '%s'\n",
                 path.c_str());
    return builder.build();  // return empty scene
  }

  std::printf("[RenderSceneImport] loaded '%s': %zu verts, %zu tris\n",
              path.c_str(), positions.size(), triangles.size());

  // Apply uniform scale.
  if (opts.uniform_scale != 1.f) {
    for (auto& p : positions) {
      p[0] *= opts.uniform_scale;
      p[1] *= opts.uniform_scale;
      p[2] *= opts.uniform_scale;
    }
  }

  // Optionally re-centre at origin.
  if (opts.recenter && !positions.empty()) {
    float cx = 0.f, cy = 0.f, cz = 0.f;
    for (const auto& p : positions) {
      cx += p[0]; cy += p[1]; cz += p[2];
    }
    float inv_n = 1.f / static_cast<float>(positions.size());
    cx *= inv_n; cy *= inv_n; cz *= inv_n;
    for (auto& p : positions) {
      p[0] -= cx; p[1] -= cy; p[2] -= cz;
    }
  }

  // Create a MeshRef.
  MeshRef mesh;
  mesh.id = static_cast<MeshId>(1);
  mesh.name = scene_name;
  mesh.vertex_count = static_cast<uint32_t>(positions.size());
  mesh.index_count = static_cast<uint32_t>(triangles.size() * 3);
  builder.addMesh(mesh);

  // Create default material if requested.
  if (opts.auto_default_material) {
    Material mat;
    mat.id = static_cast<MaterialId>(1);
    mat.name = "default_lambert";
    mat.shading = ShadingModel::Lambert;
    mat.base_color = {0.8f, 0.8f, 0.8f, 1.f};
    builder.addMaterial(mat);
  }

  // Create a single instance referencing the mesh + material.
  InstanceRef inst;
  inst.id = static_cast<SceneObjectId>(1);
  inst.mesh = static_cast<MeshId>(1);
  inst.material = opts.auto_default_material ? static_cast<MaterialId>(1)
                                             : MaterialId::Null;
  inst.visible = true;
  builder.addInstance(inst);

  // Add a default directional light if requested.
  if (opts.auto_default_light) {
    Light light;
    light.type = LightType::Directional;
    light.color = {1.f, 1.f, 1.f};
    light.intensity = 1.f;
    light.direction = {-0.5774f, -0.5774f, -0.5774f};  // roughly (-1,-1,-1) normalised
    light.cast_shadow = true;
    builder.addLight(light);
  }

  return builder.build();
}

}  // namespace render
}  // namespace zs
