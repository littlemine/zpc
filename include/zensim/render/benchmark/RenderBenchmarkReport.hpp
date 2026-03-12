#pragma once
/// @file RenderBenchmarkReport.hpp
/// @brief Data model for benchmark results — timing stats,
///        quality metrics, artifact paths.

#include "zensim/render/RenderManifest.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace zs {
namespace render {

  /// Per-frame timing sample.
  struct FrameTiming {
    uint32_t frame_index{0};
    uint64_t render_us{0};   ///< GPU render time (microseconds).
    uint64_t total_us{0};    ///< Wall-clock time (microseconds).
  };

  /// Aggregate timing statistics.
  struct TimingStats {
    uint64_t min_us{0};
    uint64_t max_us{0};
    uint64_t mean_us{0};
    uint64_t median_us{0};
    uint64_t p95_us{0};
    uint64_t p99_us{0};
    uint32_t frame_count{0};
  };

  /// A complete benchmark report for one scenario.
  struct BenchmarkReport {
    std::string scenario_name;
    bool success{false};
    std::string error_message;        ///< Non-empty on failure.

    TimingStats render_stats;         ///< GPU-side timing.
    TimingStats total_stats;          ///< Wall-clock timing.
    std::vector<FrameTiming> frames;  ///< Per-frame detail (if enabled).

    RenderManifest manifest;          ///< Artifact manifest.

    /// Write the report as JSON to the manifest output directory.
    /// Returns the absolute path written, or empty on failure.
    std::string writeJSON() const;
  };

}  // namespace render
}  // namespace zs
