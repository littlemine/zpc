#pragma once
/// @file RenderCapture.hpp
/// @brief Orchestrates capture of a rendered frame: readback from
///        GPU, image encoding, and artifact registration.

#include "zensim/render/RenderArtifact.hpp"
#include "zensim/render/RenderManifest.hpp"
#include "zensim/render/RenderFrameRequest.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace zs {
namespace render {

  /// Callback invoked when a capture operation completes.
  using CaptureCallback = std::function<void(const RenderManifest&)>;

  /// High-level capture controller.
  ///
  /// Usage:
  /// 1. Call `beginCapture(request)` before rendering.
  /// 2. After rendering, call `captureColorBuffer(pixels, ...)`.
  /// 3. Call `endCapture()` to flush artifacts to disk.
  class RenderCaptureSession {
  public:
    explicit RenderCaptureSession(const RenderFrameRequest& request);
    ~RenderCaptureSession();

    // Non-copyable, movable.
    RenderCaptureSession(const RenderCaptureSession&) = delete;
    RenderCaptureSession& operator=(const RenderCaptureSession&) = delete;
    RenderCaptureSession(RenderCaptureSession&&) noexcept = default;
    RenderCaptureSession& operator=(RenderCaptureSession&&) noexcept = default;

    /// Register a completed color-buffer readback.
    /// @param view_name   Which view this belongs to.
    /// @param pixels      Row-major RGBA8 pixel data (width*height*4 bytes).
    /// @param width       Image width.
    /// @param height      Image height.
    /// @param render_us   Render time in microseconds.
    void captureColorBuffer(const std::string& view_name,
                            const uint8_t* pixels,
                            uint32_t width, uint32_t height,
                            uint64_t render_us = 0);

    /// Register a completed depth-buffer readback.
    void captureDepthBuffer(const std::string& view_name,
                            const float* depth,
                            uint32_t width, uint32_t height,
                            uint64_t render_us = 0);

    /// Finalise: write all artifacts + manifest to disk.
    /// Returns the manifest.
    RenderManifest endCapture();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

}  // namespace render
}  // namespace zs
