/// @file PathTracerTypes.hpp
/// @brief Shared CPU/GPU data structures for the path tracer.
///
/// These structs are laid out for std430 SSBO compatibility and must be
/// kept in sync with the corresponding GLSL definitions in VulkanPathTracer.

#pragma once

#include <cstdint>

namespace zs {
namespace render {

/// BVH node — 32 bytes, std430-compatible.
///
/// Internal nodes: count == 0.
///   - Left child is at index left_first.
///   - Right child is at index left_first + 1.
///   (Children are always allocated adjacently by BVHBuilder.)
///
/// Leaf nodes: count > 0.
///   - Triangles are at tri_indices[left_first .. left_first + count).
struct BVHNodeGPU {
  float min_x, min_y, min_z;
  uint32_t left_first;  ///< Internal: left-child index (right = left_first+1). Leaf: first triangle index.
  float max_x, max_y, max_z;
  uint32_t count;        ///< 0 = internal, >0 = leaf triangle count.
};
static_assert(sizeof(BVHNodeGPU) == 32, "BVHNode must be 32 bytes");

/// Packed vertex — 32 bytes (two vec4s).
struct PackedVertex {
  float px, py, pz, pw;  ///< Position (w = 0).
  float nx, ny, nz, nw;  ///< Normal   (w = 0).
};
static_assert(sizeof(PackedVertex) == 32, "PackedVertex must be 32 bytes");

/// Packed triangle — 16 bytes (uvec4).
struct PackedTriangle {
  uint32_t i0, i1, i2;    ///< Vertex indices.
  uint32_t material_id;   ///< Index into material array.
};
static_assert(sizeof(PackedTriangle) == 16, "PackedTriangle must be 16 bytes");

/// Push constants for the path-trace compute shader — 96 bytes.
struct PathTracerPushConstants {
  float cam_origin[4];   ///< xyz = camera position.
  float cam_forward[4];  ///< xyz = forward direction (normalised).
  float cam_right[4];    ///< xyz = right vector (scaled by half-width in world space).
  float cam_up[4];       ///< xyz = up vector (scaled by half-height in world space).
  uint32_t width;
  uint32_t height;
  uint32_t frame;        ///< Frame index for progressive accumulation.
  uint32_t max_bounces;
  uint32_t spp;           ///< Samples per pixel per dispatch.
  uint32_t num_triangles;
  uint32_t _pad0;
  uint32_t _pad1;
};
static_assert(sizeof(PathTracerPushConstants) == 96, "Push constants must be 96 bytes");

}  // namespace render
}  // namespace zs
