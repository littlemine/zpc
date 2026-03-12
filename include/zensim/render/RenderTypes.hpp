#pragma once
/// @file RenderTypes.hpp
/// @brief Fundamental value types shared across all render subsystems.
///
/// Everything here is a plain aggregate — no virtual dispatch, no heap
/// allocation, no Vulkan/CUDA headers.  The types are meant to be
/// cheaply copyable and serialisable so they can flow through the
/// execution-graph as ordinary payloads.

#include "zensim/math/Vec.h"
#include "zensim/types/SmallVector.hpp"

#include <cstdint>
#include <string>

namespace zs {
namespace render {

  // ---------------------------------------------------------------
  // Identifiers
  // ---------------------------------------------------------------

  /// Opaque handle for scene objects.  Zero is "null".
  enum class SceneObjectId : uint32_t { Null = 0 };

  /// Opaque handle for materials.
  enum class MaterialId : uint32_t { Null = 0 };

  /// Opaque handle for meshes stored in the scene.
  enum class MeshId : uint32_t { Null = 0 };

  /// Opaque handle for textures/images.
  enum class TextureId : uint32_t { Null = 0 };

  /// Render backend selector.
  enum class RenderBackend : uint8_t {
    Vulkan = 0,
    CUDA = 1,
    // future: DX12, Metal, ...
  };

  /// Pixel format for render targets / readback.
  enum class PixelFormat : uint8_t {
    RGBA8_Unorm = 0,
    RGBA16_Float = 1,
    RGBA32_Float = 2,
    R32_Float = 3,  // depth-only
    D32_Float = 4,  // depth attachment
  };

  // ---------------------------------------------------------------
  // Camera
  // ---------------------------------------------------------------

  /// Projection mode.
  enum class ProjectionType : uint8_t { Perspective = 0, Orthographic = 1 };

  /// A camera definition — enough to derive both view and projection
  /// matrices on any backend.
  struct Camera {
    vec<f32, 3> position{0.f, 0.f, 5.f};   ///< Eye position (world).
    vec<f32, 3> target{0.f, 0.f, 0.f};     ///< Look-at point (world).
    vec<f32, 3> up{0.f, 1.f, 0.f};         ///< World-up vector.

    ProjectionType projection{ProjectionType::Perspective};
    f32 fov_y_radians{0.7854f};  ///< Vertical FOV (perspective only).
    f32 ortho_size{10.f};        ///< Half-height   (ortho only).
    f32 near_plane{0.1f};
    f32 far_plane{1000.f};
    f32 aspect_ratio{16.f / 9.f};

    /// Build a right-handed look-at view matrix (column-major 4x4).
    constexpr vec<f32, 4, 4> viewMatrix() const noexcept;

    /// Build a projection matrix (column-major 4x4).
    constexpr vec<f32, 4, 4> projectionMatrix() const noexcept;
  };

  // ---------------------------------------------------------------
  // Transform
  // ---------------------------------------------------------------

  /// Per-instance 4x4 transform (column-major, right-handed).
  struct Transform {
    vec<f32, 4, 4> matrix = vec<f32, 4, 4>::identity();
  };

  // ---------------------------------------------------------------
  // Material  (render-facing, NOT physics material)
  // ---------------------------------------------------------------

  /// Shading model selector (controls rasteriser lighting).
  enum class ShadingModel : uint8_t {
    Unlit = 0,
    Lambert = 1,
    BlinnPhong = 2,
    PBR_MetallicRoughness = 3,
  };

  /// Surface type (controls path-tracer BSDF selection).
  enum class SurfaceType : uint8_t {
    Opaque = 0,       ///< Standard opaque (Lambertian/GGX depending on roughness).
    Mirror = 1,       ///< Perfect specular reflection.
    Glass = 2,        ///< Dielectric (Fresnel + refraction via IOR).
    Emissive = 3,     ///< Pure emitter (no reflection).
  };

  /// GPU-packed material (48 bytes, std430-friendly).
  ///
  /// Layout matches the GLSL struct GPUMaterial:
  ///   vec4 albedo;    // rgb + alpha
  ///   vec4 emission;  // rgb + intensity
  ///   vec4 params;    // x=roughness, y=ior, z=surface_type(uint as float), w=metallic
  struct MaterialGPU {
    float albedo[4];
    float emission[4];
    float params[4];
  };
  static_assert(sizeof(MaterialGPU) == 48, "GPU material must be 48 bytes");

  /// A unified material description serving both real-time and offline
  /// renderers.  Texture slots use TextureId handles — the actual GPU
  /// resources are resolved at bind time.
  struct Material {
    MaterialId id{MaterialId::Null};
    ShadingModel shading{ShadingModel::Lambert};
    SurfaceType surface_type{SurfaceType::Opaque};

    vec<f32, 4> base_color{0.8f, 0.8f, 0.8f, 1.f};
    f32 metallic{0.f};
    f32 roughness{0.5f};

    /// Emission colour (linear RGB).
    vec<f32, 3> emission_color{0.f, 0.f, 0.f};
    f32 emissive_strength{0.f};

    /// Index of refraction (only meaningful for SurfaceType::Glass).
    f32 ior{1.5f};

    TextureId base_color_map{TextureId::Null};
    TextureId normal_map{TextureId::Null};
    TextureId metallic_roughness_map{TextureId::Null};
    TextureId emissive_map{TextureId::Null};

    std::string name;

    /// Convert to the flat GPU representation (48 bytes).
    MaterialGPU toGPU() const {
      MaterialGPU g{};
      g.albedo[0] = base_color(0);
      g.albedo[1] = base_color(1);
      g.albedo[2] = base_color(2);
      g.albedo[3] = base_color(3);
      g.emission[0] = emission_color(0) * emissive_strength;
      g.emission[1] = emission_color(1) * emissive_strength;
      g.emission[2] = emission_color(2) * emissive_strength;
      g.emission[3] = emissive_strength;
      g.params[0] = roughness;
      g.params[1] = ior;
      g.params[2] = static_cast<float>(static_cast<uint32_t>(surface_type));
      g.params[3] = metallic;
      return g;
    }

    // --- Convenience factories ---

    /// Create a Lambertian diffuse material.
    static Material diffuse(MaterialId mid, float r, float g, float b) {
      Material m;
      m.id = mid;
      m.base_color = {r, g, b, 1.f};
      m.shading = ShadingModel::Lambert;
      m.surface_type = SurfaceType::Opaque;
      m.roughness = 1.0f;
      m.metallic = 0.0f;
      return m;
    }

    /// Create a pure emissive material.
    static Material emissive(MaterialId mid, float r, float g, float b,
                             float intensity = 1.f) {
      Material m;
      m.id = mid;
      m.base_color = {0.f, 0.f, 0.f, 1.f};
      m.shading = ShadingModel::Unlit;
      m.surface_type = SurfaceType::Emissive;
      m.emission_color = {r, g, b};
      m.emissive_strength = intensity;
      return m;
    }

    /// Create a perfect mirror material.
    static Material mirror(MaterialId mid, float r, float g, float b) {
      Material m;
      m.id = mid;
      m.base_color = {r, g, b, 1.f};
      m.shading = ShadingModel::PBR_MetallicRoughness;
      m.surface_type = SurfaceType::Mirror;
      m.roughness = 0.0f;
      m.metallic = 1.0f;
      return m;
    }
  };

  // ---------------------------------------------------------------
  // Light
  // ---------------------------------------------------------------

  enum class LightType : uint8_t {
    Directional = 0,
    Point = 1,
    Spot = 2,
    Area = 3,       // future
    Environment = 4  // IBL
  };

  struct Light {
    LightType type{LightType::Directional};
    vec<f32, 3> color{1.f, 1.f, 1.f};
    f32 intensity{1.f};

    // Directional / spot
    vec<f32, 3> direction{0.f, -1.f, 0.f};

    // Point / spot
    vec<f32, 3> position{0.f, 5.f, 0.f};

    // Spot
    f32 inner_cone_angle{0.3490f};  // ~20 deg
    f32 outer_cone_angle{0.5236f};  // ~30 deg

    // Shadow
    bool cast_shadow{true};
  };

  // ---------------------------------------------------------------
  // Mesh reference (CPU-side, pointing into Mesh.hpp data)
  // ---------------------------------------------------------------

  /// A lightweight reference to an already-loaded CPU mesh
  /// (zs::Mesh from geometry/Mesh.hpp).
  struct MeshRef {
    MeshId id{MeshId::Null};
    std::string name;
    uint32_t vertex_count{0};
    uint32_t index_count{0};  ///< 0 → non-indexed draw
  };

  // ---------------------------------------------------------------
  // Instance (mesh + material + transform)
  // ---------------------------------------------------------------

  struct InstanceRef {
    SceneObjectId id{SceneObjectId::Null};
    MeshId mesh{MeshId::Null};
    MaterialId material{MaterialId::Null};
    Transform transform;
    bool visible{true};
  };

  // ---------------------------------------------------------------
  // Viewport / render-target size
  // ---------------------------------------------------------------

  struct Viewport {
    uint32_t width{1920};
    uint32_t height{1080};
    PixelFormat color_format{PixelFormat::RGBA8_Unorm};
    PixelFormat depth_format{PixelFormat::D32_Float};
    uint32_t sample_count{1};  // MSAA sample count
  };

}  // namespace render
}  // namespace zs
