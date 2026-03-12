/// @file BVHBuilder.cpp
/// @brief CPU-side BVH construction — midpoint split on longest axis.

#include "zensim/render/offline/BVHBuilder.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numeric>

namespace zs {
namespace render {

namespace {

struct AABB {
  float min[3]{1e30f, 1e30f, 1e30f};
  float max[3]{-1e30f, -1e30f, -1e30f};

  void grow(const float p[3]) {
    for (int i = 0; i < 3; ++i) {
      if (p[i] < min[i]) min[i] = p[i];
      if (p[i] > max[i]) max[i] = p[i];
    }
  }

  void grow(const AABB& other) {
    for (int i = 0; i < 3; ++i) {
      if (other.min[i] < min[i]) min[i] = other.min[i];
      if (other.max[i] > max[i]) max[i] = other.max[i];
    }
  }

  int longestAxis() const {
    float dx = max[0] - min[0];
    float dy = max[1] - min[1];
    float dz = max[2] - min[2];
    if (dx >= dy && dx >= dz) return 0;
    if (dy >= dz) return 1;
    return 2;
  }

  float midpoint(int axis) const { return 0.5f * (min[axis] + max[axis]); }
};

struct BuildEntry {
  uint32_t node_idx;
  uint32_t begin;
  uint32_t end;
};

AABB triAABB(const std::vector<std::array<float, 3>>& positions,
             const std::vector<std::array<uint32_t, 3>>& triangles,
             uint32_t tri_idx) {
  AABB box;
  const auto& tri = triangles[tri_idx];
  box.grow(positions[tri[0]].data());
  box.grow(positions[tri[1]].data());
  box.grow(positions[tri[2]].data());
  return box;
}

void triCentroid(const std::vector<std::array<float, 3>>& positions,
                 const std::vector<std::array<uint32_t, 3>>& triangles,
                 uint32_t tri_idx, float out[3]) {
  const auto& tri = triangles[tri_idx];
  for (int i = 0; i < 3; ++i)
    out[i] = (positions[tri[0]][i] + positions[tri[1]][i] + positions[tri[2]][i]) / 3.0f;
}

}  // namespace

FlatBVH buildBVH(const std::vector<std::array<float, 3>>& positions,
                 const std::vector<std::array<uint32_t, 3>>& triangles,
                 uint32_t max_leaf_size) {
  const uint32_t num_tris = static_cast<uint32_t>(triangles.size());
  assert(num_tris > 0);

  FlatBVH result;
  // tri_indices: initially [0, 1, 2, ... num_tris-1].  We reorder during build.
  result.tri_indices.resize(num_tris);
  std::iota(result.tri_indices.begin(), result.tri_indices.end(), 0u);

  // Pre-compute centroids.
  std::vector<std::array<float, 3>> centroids(num_tris);
  for (uint32_t i = 0; i < num_tris; ++i)
    triCentroid(positions, triangles, i, centroids[i].data());

  // Reserve generous space for nodes (2*N - 1 in the worst case).
  result.nodes.reserve(2 * num_tris);

  // Iterative build using an explicit stack.
  // Each BuildEntry: (node_idx, begin, end) where [begin, end) is the range
  // in tri_indices owned by this node.
  std::vector<BuildEntry> stack;
  stack.reserve(64);

  // Create root node (placeholder).
  result.nodes.push_back(BVHNodeGPU{});
  stack.push_back({0, 0, num_tris});

  while (!stack.empty()) {
    BuildEntry entry = stack.back();
    stack.pop_back();

    uint32_t node_idx = entry.node_idx;
    uint32_t begin = entry.begin;
    uint32_t end = entry.end;
    uint32_t count = end - begin;

    // Compute AABB for all triangles in [begin, end).
    AABB box;
    for (uint32_t i = begin; i < end; ++i) {
      AABB tb = triAABB(positions, triangles, result.tri_indices[i]);
      box.grow(tb);
    }

    auto& node = result.nodes[node_idx];
    node.min_x = box.min[0]; node.min_y = box.min[1]; node.min_z = box.min[2];
    node.max_x = box.max[0]; node.max_y = box.max[1]; node.max_z = box.max[2];

    if (count <= max_leaf_size) {
      // Leaf.
      node.left_first = begin;
      node.count = count;
      continue;
    }

    // Find split axis and position.
    AABB centroidBounds;
    for (uint32_t i = begin; i < end; ++i)
      centroidBounds.grow(centroids[result.tri_indices[i]].data());

    int axis = centroidBounds.longestAxis();
    float mid = centroidBounds.midpoint(axis);

    // Partition tri_indices around the midpoint.
    auto it = std::partition(
        result.tri_indices.begin() + begin,
        result.tri_indices.begin() + end,
        [&](uint32_t idx) { return centroids[idx][axis] < mid; });

    uint32_t split = static_cast<uint32_t>(it - result.tri_indices.begin());

    // If partition didn't split, fall back to median.
    if (split == begin || split == end) {
      split = begin + count / 2;
      std::nth_element(
          result.tri_indices.begin() + begin,
          result.tri_indices.begin() + split,
          result.tri_indices.begin() + end,
          [&](uint32_t a, uint32_t b) { return centroids[a][axis] < centroids[b][axis]; });
    }

    // Allocate child nodes.
    // Left child is always at (node_idx + 1) in the DFS-ordered flat array.
    // But since we build iteratively and push right first (so left is processed first),
    // left child position might not be node_idx + 1.  We need to fix this.
    //
    // Actually, with iterative build, nodes are NOT in DFS order.
    // To get DFS order, we'd need a post-processing step.
    // Instead, store BOTH child indices.  We use left_first for the right child
    // (the distant one), and the left child is always at node_idx + 1 only in
    // a DFS-ordered tree.
    //
    // Simpler approach: just store right_child in left_first, and ensure left
    // child IS at node_idx + 1 by processing left before right.
    //
    // We achieve this by: pushing right onto stack first, then left.
    // Allocating left node next (so it's at result.nodes.size()),
    // then allocating right node.

    uint32_t left_idx = static_cast<uint32_t>(result.nodes.size());
    result.nodes.push_back(BVHNodeGPU{});  // left child placeholder

    uint32_t right_idx = static_cast<uint32_t>(result.nodes.size());
    result.nodes.push_back(BVHNodeGPU{});  // right child placeholder

    // Internal node: count == 0, left_first == right_child index.
    // Left child is at left_idx.  But it may NOT be node_idx + 1
    // because other nodes may have been inserted between.
    //
    // For the GPU traversal, we need to know both children.  Let's use a
    // convention where left_first stores the LEFT child index, and we
    // know right child = left_first + 1 (because we allocate them adjacently).
    //
    // This works because left_idx and right_idx are always consecutive!

    node.left_first = left_idx;  // left child (right child = left_first + 1)
    node.count = 0;  // internal node

    // Push right first so left is processed first.
    stack.push_back({right_idx, split, end});
    stack.push_back({left_idx, begin, split});
  }

  return result;
}

}  // namespace render
}  // namespace zs
