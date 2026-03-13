/// @file ScenePrepare.cpp
/// @brief Implementation of scene conversion utilities.

#include "zensim/render/scene/ScenePrepare.hpp"
#include "zensim/render/scene/RenderSceneBuilder.hpp"
#include "zensim/render/RenderMaterial.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace zs {
namespace render {

// -----------------------------------------------------------------
// IPathTracer default uploadScene(RenderScene) implementation
// -----------------------------------------------------------------

void IPathTracer::uploadScene(const RenderScene& scene) {
  PathTracerScene ptScene = flattenForPathTracer(scene);
  uploadScene(ptScene);
}

// -----------------------------------------------------------------
// Helper: transform a vec3 position by a 4x4 matrix
// -----------------------------------------------------------------

static std::array<float, 3> transformPoint(const vec<f32, 4, 4>& m,
                                            const std::array<float, 3>& p) {
  // m is row-major, accessed via (row, col)
  float x = m(0, 0) * p[0] + m(0, 1) * p[1] + m(0, 2) * p[2] + m(0, 3);
  float y = m(1, 0) * p[0] + m(1, 1) * p[1] + m(1, 2) * p[2] + m(1, 3);
  float z = m(2, 0) * p[0] + m(2, 1) * p[1] + m(2, 2) * p[2] + m(2, 3);
  return {x, y, z};
}

// -----------------------------------------------------------------
// Helper: transform a vec3 direction (normal) by a 4x4 matrix
//         (upper-left 3x3 only, then renormalise)
// -----------------------------------------------------------------

static std::array<float, 3> transformNormal(const vec<f32, 4, 4>& m,
                                             const std::array<float, 3>& n) {
  // For rigid transforms (no non-uniform scale), the normal transform
  // is the same as the upper-left 3x3.  For non-uniform scale we'd
  // need the inverse-transpose, but we keep it simple for now.
  float x = m(0, 0) * n[0] + m(0, 1) * n[1] + m(0, 2) * n[2];
  float y = m(1, 0) * n[0] + m(1, 1) * n[1] + m(1, 2) * n[2];
  float z = m(2, 0) * n[0] + m(2, 1) * n[1] + m(2, 2) * n[2];
  float len = std::sqrt(x * x + y * y + z * z);
  if (len < 1e-12f) return {0.f, 1.f, 0.f};
  return {x / len, y / len, z / len};
}

// -----------------------------------------------------------------
// Helper: convert a unified Material to the old RenderMaterial
// -----------------------------------------------------------------

static RenderMaterial materialToRenderMaterial(const Material& mat) {
  RenderMaterial rm;
  rm.albedo = {mat.base_color(0), mat.base_color(1), mat.base_color(2)};

  float emR = mat.emission_color(0) * mat.emissive_strength;
  float emG = mat.emission_color(1) * mat.emissive_strength;
  float emB = mat.emission_color(2) * mat.emissive_strength;
  rm.emission = {emR, emG, emB};

  rm.roughness = mat.roughness;
  rm.ior = mat.ior;

  switch (mat.surface_type) {
    case SurfaceType::Opaque:   rm.type = MaterialType::Diffuse;     break;
    case SurfaceType::Mirror:   rm.type = MaterialType::Mirror;      break;
    case SurfaceType::Glass:    rm.type = MaterialType::Dielectric;  break;
    case SurfaceType::Emissive: rm.type = MaterialType::Emissive;    break;
    default:                    rm.type = MaterialType::Diffuse;     break;
  }

  return rm;
}

// -----------------------------------------------------------------
// flattenForPathTracer
// -----------------------------------------------------------------

PathTracerScene flattenForPathTracer(const RenderScene& scene) {
  PathTracerScene pt;

  // Build material palette: one RenderMaterial per scene Material.
  // The index in the palette matches the index in scene.materials().
  const auto& sceneMats = scene.materials();
  pt.materials.reserve(sceneMats.size());
  for (const auto& mat : sceneMats) {
    pt.materials.push_back(materialToRenderMaterial(mat));
  }

  // If the scene has no materials, add a default grey diffuse.
  if (pt.materials.empty()) {
    pt.materials.push_back(RenderMaterial::diffuse(0.8f, 0.8f, 0.8f));
  }

  // Build a map from MaterialId -> palette index for fast lookup.
  std::unordered_map<uint32_t, uint32_t> matIdToIndex;
  for (size_t i = 0; i < sceneMats.size(); ++i) {
    matIdToIndex[static_cast<uint32_t>(sceneMats[i].id)] =
        static_cast<uint32_t>(i);
  }

  // Iterate all visible instances, flatten geometry.
  for (const auto& inst : scene.instances()) {
    if (!inst.visible) continue;

    const TriMesh* mesh = scene.findMeshData(inst.mesh);
    if (!mesh || mesh->nodes.empty()) continue;

    const auto& xform = inst.transform.matrix;
    const uint32_t baseVertex =
        static_cast<uint32_t>(pt.positions.size());

    // Determine material index for this instance.
    uint32_t matIdx = 0;
    auto it = matIdToIndex.find(static_cast<uint32_t>(inst.material));
    if (it != matIdToIndex.end()) {
      matIdx = it->second;
    }

    // Transform and append vertices.
    for (size_t v = 0; v < mesh->nodes.size(); ++v) {
      std::array<float, 3> pos = {
          mesh->nodes[v][0], mesh->nodes[v][1], mesh->nodes[v][2]};
      pt.positions.push_back(transformPoint(xform, pos));

      if (v < mesh->norms.size()) {
        std::array<float, 3> nrm = {
            mesh->norms[v][0], mesh->norms[v][1], mesh->norms[v][2]};
        pt.normals.push_back(transformNormal(xform, nrm));
      } else {
        pt.normals.push_back({0.f, 1.f, 0.f});
      }
    }

    // Append triangles with offset indices and per-tri material.
    for (size_t t = 0; t < mesh->elems.size(); ++t) {
      pt.triangles.push_back({mesh->elems[t][0] + baseVertex,
                               mesh->elems[t][1] + baseVertex,
                               mesh->elems[t][2] + baseVertex});
      pt.material_ids.push_back(matIdx);
    }
  }

  std::printf("[flattenForPathTracer] %zu tris, %zu verts, %zu mats "
              "from %zu instances\n",
              pt.triangles.size(), pt.positions.size(),
              pt.materials.size(), scene.instances().size());

  return pt;
}

// -----------------------------------------------------------------
// createCornellBoxScene
// -----------------------------------------------------------------

/// Helper struct for building geometry groups (one per material).
struct MeshGroup {
  TriMesh mesh;
  MaterialId material_id;
  std::string name;

  void addQuad(std::array<float, 3> v0, std::array<float, 3> v1,
               std::array<float, 3> v2, std::array<float, 3> v3,
               std::array<float, 3> color,
               std::array<float, 3> normal) {
    uint32_t base = static_cast<uint32_t>(mesh.nodes.size());
    mesh.nodes.push_back(v0);
    mesh.nodes.push_back(v1);
    mesh.nodes.push_back(v2);
    mesh.nodes.push_back(v3);

    mesh.norms.push_back(normal);
    mesh.norms.push_back(normal);
    mesh.norms.push_back(normal);
    mesh.norms.push_back(normal);

    mesh.colors.push_back(color);
    mesh.colors.push_back(color);
    mesh.colors.push_back(color);
    mesh.colors.push_back(color);

    mesh.elems.push_back({base, base + 1, base + 2});
    mesh.elems.push_back({base, base + 2, base + 3});
  }
};

RenderScene createCornellBoxScene() {
  RenderSceneBuilder builder("cornell_box");
  const float S = 5.55f;

  // --- Materials ---
  auto matWhite = Material::diffuse(static_cast<MaterialId>(1),
                                     0.73f, 0.73f, 0.73f);
  matWhite.name = "white";

  auto matRed = Material::diffuse(static_cast<MaterialId>(2),
                                   0.65f, 0.05f, 0.05f);
  matRed.name = "red";

  auto matGreen = Material::diffuse(static_cast<MaterialId>(3),
                                     0.12f, 0.45f, 0.15f);
  matGreen.name = "green";

  auto matLight = Material::emissive(static_cast<MaterialId>(4),
                                      1.0f, 1.0f, 1.0f, 15.0f);
  matLight.name = "light";

  auto matBox = Material::diffuse(static_cast<MaterialId>(5),
                                   0.73f, 0.73f, 0.73f);
  matBox.name = "box";

  builder.addMaterial(matWhite)
         .addMaterial(matRed)
         .addMaterial(matGreen)
         .addMaterial(matLight)
         .addMaterial(matBox);

  // --- Build geometry groups (one per material) ---

  // Group 0: white surfaces (floor, ceiling, back wall)
  MeshGroup whiteGroup;
  whiteGroup.material_id = matWhite.id;
  whiteGroup.name = "white_surfaces";
  {
    std::array<float, 3> c = {0.73f, 0.73f, 0.73f};
    // Floor
    whiteGroup.addQuad({0, 0, 0}, {S, 0, 0}, {S, 0, S}, {0, 0, S},
                       c, {0, 1, 0});
    // Ceiling
    whiteGroup.addQuad({0, S, S}, {S, S, S}, {S, S, 0}, {0, S, 0},
                       c, {0, -1, 0});
    // Back wall
    whiteGroup.addQuad({0, 0, S}, {S, 0, S}, {S, S, S}, {0, S, S},
                       c, {0, 0, -1});
  }

  // Group 1: left wall (red)
  MeshGroup redGroup;
  redGroup.material_id = matRed.id;
  redGroup.name = "left_wall";
  {
    std::array<float, 3> c = {0.65f, 0.05f, 0.05f};
    redGroup.addQuad({0, 0, 0}, {0, 0, S}, {0, S, S}, {0, S, 0},
                     c, {1, 0, 0});
  }

  // Group 2: right wall (green)
  MeshGroup greenGroup;
  greenGroup.material_id = matGreen.id;
  greenGroup.name = "right_wall";
  {
    std::array<float, 3> c = {0.12f, 0.45f, 0.15f};
    greenGroup.addQuad({S, 0, S}, {S, 0, 0}, {S, S, 0}, {S, S, S},
                       c, {-1, 0, 0});
  }

  // Group 3: ceiling light (emissive)
  MeshGroup lightGroup;
  lightGroup.material_id = matLight.id;
  lightGroup.name = "ceiling_light";
  {
    float lx0 = S * 0.35f, lx1 = S * 0.65f;
    float lz0 = S * 0.35f, lz1 = S * 0.65f;
    float ly = S - 0.01f;
    std::array<float, 3> c = {1.0f, 1.0f, 1.0f};
    lightGroup.addQuad({lx0, ly, lz0}, {lx1, ly, lz0},
                       {lx1, ly, lz1}, {lx0, ly, lz1},
                       c, {0, -1, 0});
  }

  // Group 4: boxes (white diffuse)
  MeshGroup boxGroup;
  boxGroup.material_id = matBox.id;
  boxGroup.name = "boxes";
  {
    std::array<float, 3> c = {0.73f, 0.73f, 0.73f};

    // Tall box (rotated ~15 degrees)
    {
      float cx = 3.68f, cz = 3.51f;
      float hw = 0.83f, h = 3.30f;
      float angle = 15.0f * 3.14159265f / 180.0f;
      float cosA = std::cos(angle), sinA = std::sin(angle);

      auto rotPt = [&](float dx, float dz, float y) -> std::array<float, 3> {
        return {cx + dx * cosA - dz * sinA, y, cz + dx * sinA + dz * cosA};
      };

      auto b0 = rotPt(-hw, -hw, 0.0f), b1 = rotPt(hw, -hw, 0.0f);
      auto b2 = rotPt(hw, hw, 0.0f),   b3 = rotPt(-hw, hw, 0.0f);
      auto t0 = rotPt(-hw, -hw, h),     t1 = rotPt(hw, -hw, h);
      auto t2 = rotPt(hw, hw, h),       t3 = rotPt(-hw, hw, h);

      boxGroup.addQuad(t0, t1, t2, t3, c, {0, 1, 0});
      boxGroup.addQuad(b0, b1, t1, t0, c, {-sinA, 0, cosA});
      boxGroup.addQuad(b1, b2, t2, t1, c, {cosA, 0, sinA});
      boxGroup.addQuad(b2, b3, t3, t2, c, {sinA, 0, -cosA});
      boxGroup.addQuad(b3, b0, t0, t3, c, {-cosA, 0, -sinA});
    }

    // Short box (rotated ~-18 degrees)
    {
      float cx = 1.86f, cz = 1.69f;
      float hw = 0.83f, h = 1.65f;
      float angle = -18.0f * 3.14159265f / 180.0f;
      float cosA = std::cos(angle), sinA = std::sin(angle);

      auto rotPt = [&](float dx, float dz, float y) -> std::array<float, 3> {
        return {cx + dx * cosA - dz * sinA, y, cz + dx * sinA + dz * cosA};
      };

      auto b0 = rotPt(-hw, -hw, 0.0f), b1 = rotPt(hw, -hw, 0.0f);
      auto b2 = rotPt(hw, hw, 0.0f),   b3 = rotPt(-hw, hw, 0.0f);
      auto t0 = rotPt(-hw, -hw, h),     t1 = rotPt(hw, -hw, h);
      auto t2 = rotPt(hw, hw, h),       t3 = rotPt(-hw, hw, h);

      boxGroup.addQuad(t0, t1, t2, t3, c, {0, 1, 0});
      boxGroup.addQuad(b0, b1, t1, t0, c, {-sinA, 0, cosA});
      boxGroup.addQuad(b1, b2, t2, t1, c, {cosA, 0, sinA});
      boxGroup.addQuad(b2, b3, t3, t2, c, {sinA, 0, -cosA});
      boxGroup.addQuad(b3, b0, t0, t3, c, {-cosA, 0, -sinA});
    }
  }

  // --- Register meshes and instances ---
  MeshGroup* groups[] = {&whiteGroup, &redGroup, &greenGroup,
                          &lightGroup, &boxGroup};

  uint32_t totalVerts = 0;
  for (uint32_t i = 0; i < 5; ++i) {
    auto& grp = *groups[i];
    MeshId meshId = static_cast<MeshId>(i + 1);

    MeshRef ref;
    ref.id = meshId;
    ref.name = grp.name;
    ref.vertex_count = static_cast<uint32_t>(grp.mesh.nodes.size());
    ref.index_count = static_cast<uint32_t>(grp.mesh.elems.size() * 3);
    totalVerts += ref.vertex_count;

    builder.addMesh(ref, std::move(grp.mesh));

    InstanceRef inst;
    inst.id = static_cast<SceneObjectId>(i + 1);
    inst.mesh = meshId;
    inst.material = grp.material_id;
    inst.visible = true;
    // Only boxes (group 4) cast shadows — room enclosure and light panel
    // should not occlude scene interior from the shadow map camera.
    inst.cast_shadow = (i == 4);

    builder.addInstance(inst);
  }

  // Add an area light corresponding to the ceiling light quad.
  Light areaLight;
  areaLight.type = LightType::Area;
  areaLight.color = {1.0f, 1.0f, 1.0f};
  areaLight.intensity = 15.0f;
  areaLight.position = {S * 0.5f, S - 0.01f, S * 0.5f};
  areaLight.direction = {0.f, -1.f, 0.f};
  areaLight.cast_shadow = true;

  builder.addLight(areaLight);

  std::printf("[createCornellBoxScene] RenderScene: %u verts, 5 mats, "
              "5 instances, 1 light\n", totalVerts);

  return builder.build();
}

}  // namespace render
}  // namespace zs
