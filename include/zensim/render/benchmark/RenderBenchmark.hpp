#pragma once
/// @file RenderBenchmark.hpp
/// @brief Benchmark harness that runs a set of scenarios,
///        collects timing/quality metrics, and produces reports.

#include "zensim/render/benchmark/RenderBenchmarkScenario.hpp"
#include "zensim/render/benchmark/RenderBenchmarkReport.hpp"

#include <string>
#include <vector>

namespace zs {
namespace render {

  /// Configuration for a benchmark session.
  struct BenchmarkConfig {
    std::string artifact_root;            ///< Output root (e.g. "H:/zpc_render").
    uint32_t warmup_frames{2};            ///< Frames to render before measuring.
    uint32_t measurement_frames{10};      ///< Frames to measure.
    bool capture_first_frame{true};       ///< Write PNG for first measured frame.
    bool capture_last_frame{true};        ///< Write PNG for last measured frame.
    bool write_per_frame_timings{true};   ///< Include per-frame timing in report.
  };

  /// Run a single benchmark scenario.
  /// @param scenario   Scenario description (scene + method + backend).
  /// @param config     Harness configuration.
  /// @return           Benchmark report with timing and quality data.
  BenchmarkReport runBenchmark(const BenchmarkScenario& scenario,
                               const BenchmarkConfig& config);

  /// Run all provided scenarios sequentially and return reports.
  std::vector<BenchmarkReport> runBenchmarks(
      const std::vector<BenchmarkScenario>& scenarios,
      const BenchmarkConfig& config);

}  // namespace render
}  // namespace zs
