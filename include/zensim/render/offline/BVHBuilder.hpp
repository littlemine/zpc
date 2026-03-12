/// @file BVHBuilder.hpp
/// @brief CPU-side BVH construction for the path tracer.
///
/// Builds a flattened bounding-volume hierarchy from triangle meshes.
/// The output is a flat array of BVHNodeGPU structs plus a reordered
/// triangle-index array, both suitable for direct GPU upload.

#pragma once

#include "zensim/render/offline/PathTracerTypes.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace zs {
namespace render {

/// Result of BVH construction — ready for GPU upload.
struct FlatBVH {
  std::vector<BVHNodeGPU> nodes;
  std::vector<uint32_t> tri_indices;  ///< Reordered triangle indices.
};

/// Build a flattened BVH over the given triangle mesh.
///
/// Uses midpoint-split on the longest centroid axis.  Leaf nodes contain
/// at most @p max_leaf_size triangles.
///
/// @param positions  Vertex positions (indexed by triangle indices).
/// @param triangles  Per-triangle vertex-index triples.
/// @param max_leaf_size  Maximum triangles per leaf (default 4).
/// @return A FlatBVH ready for GPU upload.
FlatBVH buildBVH(const std::vector<std::array<float, 3>>& positions,
                 const std::vector<std::array<uint32_t, 3>>& triangles,
                 uint32_t max_leaf_size = 4);

}  // namespace render
}  // namespace zs
