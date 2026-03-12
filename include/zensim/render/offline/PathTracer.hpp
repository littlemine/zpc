/// @file PathTracer.hpp
/// @brief Path tracer interface and scene description.
///
/// Provides the IPathTracer abstract interface, PathTracerScene for
/// describing geometry + materials, and a factory for the Vulkan
/// compute implementation.

#pragma once

#include "zensim/render/RenderMaterial.hpp"
#include "zensim/render/RenderScene.hpp"
#include "zensim/render/RenderTypes.hpp"
#include "zensim/render/offline/PathTracerTypes.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zs {
namespace render {

// -----------------------------------------------------------------
// Scene description for the path tracer
// -----------------------------------------------------------------

/// A self-contained scene suitable for path tracing.
struct PathTracerScene {
  std::vector<std::array<float, 3>> positions;   ///< Vertex positions.
  std::vector<std::array<float, 3>> normals;     ///< Per-vertex normals.
  std::vector<std::array<uint32_t, 3>> triangles; ///< Index triples.
  std::vector<uint32_t> material_ids;            ///< Per-triangle material index.
  std::vector<RenderMaterial> materials;          ///< Material palette.
};

/// Create a classic Cornell Box scene (procedural, no external files).
PathTracerScene createCornellBox();

// -----------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------

struct PathTracerConfig {
  uint32_t width = 512;
  uint32_t height = 512;
  uint32_t samples_per_pixel = 64;
  uint32_t max_bounces = 8;
};

// -----------------------------------------------------------------
// Result
// -----------------------------------------------------------------

struct PathTraceResult {
  std::vector<uint8_t> pixels;  ///< RGBA8 pixel data (row-major, top-to-bottom).
  uint32_t width = 0;
  uint32_t height = 0;
  double render_time_us = 0.0;  ///< Wall-clock render time in microseconds.
  bool success = false;
  std::string error;
};

// -----------------------------------------------------------------
// Interface
// -----------------------------------------------------------------

/// Abstract path tracer interface.
class IPathTracer {
public:
  virtual ~IPathTracer() = default;

  /// Initialise GPU resources.  Must be called before render().
  virtual bool init() = 0;

  /// Upload scene data (geometry + materials + BVH) to the GPU.
  virtual void uploadScene(const PathTracerScene& scene) = 0;

  /// Upload a RenderScene by flattening it to a PathTracerScene.
  ///
  /// The default implementation calls flattenForPathTracer() and
  /// delegates to uploadScene(const PathTracerScene&).  Subclasses
  /// may override for a more efficient conversion.
  virtual void uploadScene(const RenderScene& scene);

  /// Render the scene from the given camera.
  virtual PathTraceResult render(const Camera& camera, const PathTracerConfig& config) = 0;

  /// Release GPU resources.
  virtual void shutdown() = 0;

  /// Human-readable name.
  virtual const char* name() const noexcept = 0;
};

/// Create a Vulkan-compute-based path tracer.
/// Returns nullptr if Vulkan is not enabled.
std::unique_ptr<IPathTracer> createVulkanPathTracer();

}  // namespace render
}  // namespace zs
