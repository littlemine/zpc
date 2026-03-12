/// @file RenderCapture.cpp
/// @brief RenderCaptureSession PIMPL implementation.

#include "zensim/render/capture/RenderCapture.hpp"
#include "zensim/render/capture/RenderImageWriter.hpp"
#include "zensim/render/RenderManifest.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>

namespace zs {
namespace render {

// ---------------------------------------------------------------
// PIMPL
// ---------------------------------------------------------------

struct RenderCaptureSession::Impl {
  RenderFrameRequest request;
  RenderManifest manifest;

  /// Per-view captured color buffers (stored until endCapture).
  struct BufferEntry {
    std::string view_name;
    std::vector<uint8_t> color_pixels;   // RGBA8
    std::vector<float> depth_pixels;     // f32 per pixel
    uint32_t width{0};
    uint32_t height{0};
    uint64_t render_us{0};
    bool has_color{false};
    bool has_depth{false};
  };
  std::vector<BufferEntry> entries;

  explicit Impl(const RenderFrameRequest& req) : request(req) {
    // Build the manifest skeleton.
    auto now = std::chrono::system_clock::now();
    auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch())
                        .count();
    manifest.run_id = std::to_string(epoch_ms);
    manifest.scene_name = request.scene ? request.scene->name() : "unknown";
    manifest.method = request.method;
    manifest.backend = request.backend;

    if (!request.artifact_root.empty()) {
      manifest.output_dir = RenderManifest::buildOutputDir(
          request.artifact_root,
          manifest.scene_name,
          manifest.method,
          manifest.backend,
          manifest.run_id);
    }
  }

  BufferEntry& findOrCreateEntry(const std::string& view_name) {
    for (auto& e : entries) {
      if (e.view_name == view_name) return e;
    }
    entries.push_back({});
    entries.back().view_name = view_name;
    return entries.back();
  }
};

// ---------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------

RenderCaptureSession::RenderCaptureSession(const RenderFrameRequest& request)
    : impl_(std::make_unique<Impl>(request)) {}

RenderCaptureSession::~RenderCaptureSession() = default;

// ---------------------------------------------------------------
// Capture colour
// ---------------------------------------------------------------

void RenderCaptureSession::captureColorBuffer(
    const std::string& view_name,
    const uint8_t* pixels,
    uint32_t width, uint32_t height,
    uint64_t render_us) {
  if (!impl_ || !pixels || width == 0 || height == 0) return;

  auto& entry = impl_->findOrCreateEntry(view_name);
  const size_t byte_count = static_cast<size_t>(width) * height * 4;
  entry.color_pixels.assign(pixels, pixels + byte_count);
  entry.width = width;
  entry.height = height;
  entry.render_us = render_us;
  entry.has_color = true;
}

// ---------------------------------------------------------------
// Capture depth
// ---------------------------------------------------------------

void RenderCaptureSession::captureDepthBuffer(
    const std::string& view_name,
    const float* depth,
    uint32_t width, uint32_t height,
    uint64_t render_us) {
  if (!impl_ || !depth || width == 0 || height == 0) return;

  auto& entry = impl_->findOrCreateEntry(view_name);
  const size_t count = static_cast<size_t>(width) * height;
  entry.depth_pixels.assign(depth, depth + count);
  entry.width = width;
  entry.height = height;
  if (render_us > entry.render_us) entry.render_us = render_us;
  entry.has_depth = true;
}

// ---------------------------------------------------------------
// End capture — flush to disk
// ---------------------------------------------------------------

RenderManifest RenderCaptureSession::endCapture() {
  if (!impl_) return {};

  auto start = std::chrono::steady_clock::now();

  // Ensure output directory exists.
  if (!impl_->manifest.output_dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(impl_->manifest.output_dir, ec);
    if (ec) {
      std::fprintf(stderr,
                   "[RenderCapture] failed to create output dir '%s': %s\n",
                   impl_->manifest.output_dir.c_str(),
                   ec.message().c_str());
    }
  }

  // Write each buffer entry to disk.
  for (const auto& entry : impl_->entries) {
    if (entry.has_color && !impl_->manifest.output_dir.empty()) {
      std::string color_path =
          impl_->manifest.output_dir + "/" + entry.view_name + "_color.png";
      bool ok = writePNG(color_path,
                         entry.color_pixels.data(),
                         entry.width, entry.height);
      if (ok) {
        RenderArtifact art;
        art.frame_id = impl_->request.frame_id;
        art.view_name = entry.view_name;
        art.kind = ArtifactKind::ColorImage;
        art.file_path = color_path;
        art.width = entry.width;
        art.height = entry.height;
        art.render_time_us = entry.render_us;
        art.backend = impl_->request.backend;
        impl_->manifest.artifacts.push_back(std::move(art));
      }
    }

    if (entry.has_depth && !impl_->manifest.output_dir.empty()) {
      std::string depth_path =
          impl_->manifest.output_dir + "/" + entry.view_name + "_depth.png";
      bool ok = writeDepthPNG(depth_path,
                              entry.depth_pixels.data(),
                              entry.width, entry.height);
      if (ok) {
        RenderArtifact art;
        art.frame_id = impl_->request.frame_id;
        art.view_name = entry.view_name;
        art.kind = ArtifactKind::DepthImage;
        art.file_path = depth_path;
        art.width = entry.width;
        art.height = entry.height;
        art.render_time_us = entry.render_us;
        art.backend = impl_->request.backend;
        impl_->manifest.artifacts.push_back(std::move(art));
      }
    }
  }

  auto end = std::chrono::steady_clock::now();
  impl_->manifest.total_time_us =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start)
          .count();

  // Write the manifest JSON.
  impl_->manifest.writeJSON();

  return impl_->manifest;
}

}  // namespace render
}  // namespace zs
