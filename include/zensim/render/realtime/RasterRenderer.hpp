#pragma once
/// @file RasterRenderer.hpp
/// @brief Stub interface for the Vulkan-based offscreen rasteriser.
///
/// This is the first concrete renderer.  It consumes a
/// RenderFrameRequest and produces pixel data that can be
/// captured via the capture subsystem.

#include "zensim/render/RenderFrameRequest.hpp"
#include "zensim/render/capture/RenderReadback.hpp"

#include <memory>
#include <string>

namespace zs {
namespace render {

  /// Result of a single raster render call.
  struct RasterResult {
    bool success{false};
    std::string error;
    ReadbackBuffer color;          ///< RGBA8 readback.
    ReadbackBuffer depth;          ///< Optional depth readback.
    uint64_t render_time_us{0};    ///< GPU time (microseconds).
  };

  /// Abstract interface for a raster renderer.
  ///
  /// Concrete implementations (e.g. VulkanRasterRenderer) are
  /// created via factory functions and own the GPU context
  /// for their lifetime.
  class IRasterRenderer {
  public:
    virtual ~IRasterRenderer() = default;

    /// Initialise the renderer.  Must be called once before render().
    /// @return true on success.
    virtual bool init() = 0;

    /// Render a single frame described by `request`, for the
    /// view at index `view_index`.
    virtual RasterResult render(const RenderFrameRequest& request,
                                uint32_t view_index = 0) = 0;

    /// Shut down and release GPU resources.
    virtual void shutdown() = 0;

    /// Human-readable name (e.g. "VulkanRasterRenderer").
    virtual const char* name() const noexcept = 0;
  };

  /// Factory: create a Vulkan-based offscreen raster renderer.
  /// Returns nullptr if Vulkan is not available.
  std::unique_ptr<IRasterRenderer> createVulkanRasterRenderer();

}  // namespace render
}  // namespace zs
