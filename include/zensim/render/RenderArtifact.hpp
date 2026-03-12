#pragma once
/// @file RenderArtifact.hpp
/// @brief Describes a single output artifact from a render pass
///        (an image file, a depth dump, a timing report, etc.).

#include "zensim/render/RenderTypes.hpp"

#include <cstdint>
#include <string>
#include <chrono>

namespace zs {
namespace render {

  /// What kind of data the artifact carries.
  enum class ArtifactKind : uint8_t {
    ColorImage = 0,   ///< e.g. PNG RGBA
    DepthImage = 1,   ///< e.g. PNG R32F → mapped to grayscale
    TimingJSON = 2,   ///< per-pass timing breakdown
    ManifestJSON = 3, ///< run-level manifest
    Log = 4,          ///< text log
  };

  /// A single artifact produced by a render frame.
  struct RenderArtifact {
    uint64_t frame_id{0};
    std::string view_name;       ///< Which view generated this.
    ArtifactKind kind{ArtifactKind::ColorImage};

    /// Absolute path where the file was (or will be) written.
    std::string file_path;

    /// Image dimensions (zero for non-image artifacts).
    uint32_t width{0};
    uint32_t height{0};

    /// Render time for this artifact's pass (microseconds).
    uint64_t render_time_us{0};

    /// Backend that produced it.
    RenderBackend backend{RenderBackend::Vulkan};
  };

}  // namespace render
}  // namespace zs
