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

  /// Shading model selector.
  enum class ShadingModel : uint8_t {
    Unlit = 0,
    Lambert = 1,
    BlinnPhong = 2,
    PBR_MetallicRoughness = 3,
  };

  /// A minimal material description.  Texture slots use TextureId
  /// handles — the actual GPU resources are resolved at bind time.
  struct Material {
    MaterialId id{MaterialId::Null};
    ShadingModel shading{ShadingModel::Lambert};

    vec<f32, 4> base_color{0.8f, 0.8f, 0.8f, 1.f};
    f32 metallic{0.f};
    f32 roughness{0.5f};
    f32 emissive_strength{0.f};

    TextureId base_color_map{TextureId::Null};
    TextureId normal_map{TextureId::Null};
    TextureId metallic_roughness_map{TextureId::Null};
    TextureId emissive_map{TextureId::Null};

    std::string name;
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
