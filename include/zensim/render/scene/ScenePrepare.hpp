#pragma once
/// @file ScenePrepare.hpp
/// @brief Conversion utilities between the shared RenderScene and
///        renderer-specific scene formats.
///
/// Both the real-time rasteriser and the offline path tracer consume
/// the same RenderScene, but each requires a different GPU layout.
/// This header provides the conversion/flattening functions.

#include "zensim/render/RenderScene.hpp"
#include "zensim/render/offline/PathTracer.hpp"

namespace zs {
namespace render {

// -----------------------------------------------------------------
// Path-tracer scene preparation
// -----------------------------------------------------------------

/// Flatten a RenderScene into a PathTracerScene.
///
/// This function:
///  1. Iterates all visible instances.
///  2. For each instance, applies the instance transform to the mesh
///     vertex positions and normals.
///  3. Merges all geometry into a single flat vertex/triangle array.
///  4. Assigns per-triangle material IDs by looking up the instance's
///     material in the scene's material palette and converting the
///     unified Material to the old RenderMaterial format.
///  5. Converts emissive lights from the scene into additional
///     emissive triangles (area lights) if they aren't already
///     represented as geometry.
///
/// @param scene  Source RenderScene (must outlive the call; data is
///               copied into the result).
/// @return       A self-contained PathTracerScene ready for
///               IPathTracer::uploadScene().
PathTracerScene flattenForPathTracer(const RenderScene& scene);

// -----------------------------------------------------------------
// Procedural scenes built as RenderScene
// -----------------------------------------------------------------

/// Create the classic Cornell Box as a RenderScene.
///
/// Returns a scene with:
///  - 1 mesh containing all Cornell Box geometry (walls, boxes, light)
///  - 5 materials (white diffuse, red diffuse, green diffuse,
///    emissive light, white diffuse for boxes)
///  - 1 instance with identity transform
///  - 1 area light corresponding to the ceiling light quad
///
/// The scene spans approximately [0, 5.55] in all axes.
RenderScene createCornellBoxScene();

}  // namespace render
}  // namespace zs
