#pragma once
/// @file RenderFrameRequest.hpp
/// @brief A frame request bundles a scene, one or more views, and
///        configuration flags into a single submission unit.

#include "zensim/render/RenderScene.hpp"
#include "zensim/render/RenderView.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zs {
namespace render {

  /// Render method selector.
  enum class RenderMethod : uint8_t {
    Raster_Forward = 0,
    Raster_Deferred = 1,
    PathTrace = 2,
    Hybrid = 3,
    // future: BDPT, MLT, VCM, ReSTIR, ...
  };

  /// A complete, self-contained request that can be submitted
  /// to the render runtime.  It is an immutable snapshot — once
  /// built it must not be mutated while in flight.
  struct RenderFrameRequest {
    /// Unique monotonic frame id (assigned by the submitter).
    uint64_t frame_id{0};

    /// Human-readable label for artifacts / logs.
    std::string label;

    /// The scene to render (shared ownership — the scene may be
    /// reused across multiple requests).
    std::shared_ptr<const RenderScene> scene;

    /// One or more views (camera + viewport).  The renderer
    /// produces one output image per view.
    std::vector<RenderView> views;

    /// Which render method to use.
    RenderMethod method{RenderMethod::Raster_Forward};

    /// Which backend to target.
    RenderBackend backend{RenderBackend::Vulkan};

    /// Path-tracer sample count (ignored for raster methods).
    uint32_t spp{1};

    /// If true, the renderer should produce a depth buffer
    /// alongside the color output.
    bool capture_depth{false};

    /// If true, the renderer will write artifacts to disk
    /// (PNG + manifest JSON) after the frame completes.
    bool write_artifacts{true};

    /// Root directory for artifact output (e.g. "H:/zpc_render").
    /// The renderer appends a structured sub-path per run.
    std::string artifact_root;
  };

}  // namespace render
}  // namespace zs
