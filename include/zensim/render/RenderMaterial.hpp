/// @file RenderMaterial.hpp
/// @brief Material types for the zpc render infrastructure.
///
/// Defines both a CPU-side material description (RenderMaterial) and a
/// GPU-side packed representation (RenderMaterialGPU) suitable for upload
/// to storage buffers.

#pragma once

#include <array>
#include <cstdint>

namespace zs {
namespace render {

/// Material surface type.
enum class MaterialType : uint32_t {
  Diffuse = 0,   ///< Lambertian diffuse.
  Mirror = 1,    ///< Perfect specular reflection.
  Dielectric = 2,///< Glass / dielectric (Fresnel + refraction).
  Emissive = 3,  ///< Pure emitter (no reflection).
};

/// GPU-packed material (48 bytes, std430-friendly).
///
/// Layout matches the GLSL struct GPUMaterial:
///   vec4 albedo;    // rgb + pad
///   vec4 emission;  // rgb + intensity
///   vec4 params;    // x=roughness, y=ior, z=type(uint as float), w=pad
struct RenderMaterialGPU {
  float albedo[4];
  float emission[4];
  float params[4];
};
static_assert(sizeof(RenderMaterialGPU) == 48, "GPU material must be 48 bytes");

/// CPU-side material description with convenience conversion to GPU layout.
struct RenderMaterial {
  std::array<float, 3> albedo{0.8f, 0.8f, 0.8f};
  std::array<float, 3> emission{0.0f, 0.0f, 0.0f};
  float roughness{1.0f};
  float ior{1.5f};
  MaterialType type{MaterialType::Diffuse};

  /// Convert to the flat GPU representation.
  RenderMaterialGPU toGPU() const {
    RenderMaterialGPU g{};
    g.albedo[0] = albedo[0];
    g.albedo[1] = albedo[1];
    g.albedo[2] = albedo[2];
    g.albedo[3] = 0.0f;
    g.emission[0] = emission[0];
    g.emission[1] = emission[1];
    g.emission[2] = emission[2];
    g.emission[3] = 0.0f;
    g.params[0] = roughness;
    g.params[1] = ior;
    g.params[2] = static_cast<float>(static_cast<uint32_t>(type));
    g.params[3] = 0.0f;
    return g;
  }

  // --- Convenience factories ---

  static RenderMaterial diffuse(float r, float g, float b) {
    RenderMaterial m;
    m.albedo = {r, g, b};
    m.type = MaterialType::Diffuse;
    return m;
  }

  static RenderMaterial emissive(float r, float g, float b, float intensity = 1.0f) {
    RenderMaterial m;
    m.albedo = {0.0f, 0.0f, 0.0f};
    m.emission = {r * intensity, g * intensity, b * intensity};
    m.type = MaterialType::Emissive;
    return m;
  }

  static RenderMaterial mirror(float r, float g, float b) {
    RenderMaterial m;
    m.albedo = {r, g, b};
    m.type = MaterialType::Mirror;
    return m;
  }
};

}  // namespace render
}  // namespace zs
