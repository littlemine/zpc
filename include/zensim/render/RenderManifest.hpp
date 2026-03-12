#pragma once
/// @file RenderManifest.hpp
/// @brief A manifest collects all artifacts from a single render
///        run and can serialise them to a JSON summary file.

#include "zensim/render/RenderArtifact.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace zs {
namespace render {

  /// Collects metadata + artifact list for one render run.
  struct RenderManifest {
    /// Unique run identifier (ISO-8601 timestamp + sequence).
    std::string run_id;

    /// Scene name (from RenderScene::name).
    std::string scene_name;

    /// Render method used.
    RenderMethod method{RenderMethod::Raster_Forward};

    /// Backend used.
    RenderBackend backend{RenderBackend::Vulkan};

    /// Total wall-clock time for the run (microseconds).
    uint64_t total_time_us{0};

    /// Artifacts produced.
    std::vector<RenderArtifact> artifacts;

    /// Root output directory.
    std::string output_dir;

    // -- serialisation helpers (implemented in .cpp) -----------------

    /// Write the manifest as a JSON file to `output_dir/manifest.json`.
    /// Returns the absolute path written, or empty string on failure.
    std::string writeJSON() const;

    /// Convenience: build a structured output directory path.
    /// Pattern: <root>/runs/<date>/<scene>/<method>/<backend>/<run_id>/
    static std::string buildOutputDir(const std::string& artifact_root,
                                      const std::string& scene_name,
                                      RenderMethod method,
                                      RenderBackend backend,
                                      const std::string& run_id);
  };

}  // namespace render
}  // namespace zs
