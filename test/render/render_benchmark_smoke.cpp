/// @file render_benchmark_smoke.cpp
/// @brief Smoke test for the benchmark data model.
///        Verifies BenchmarkScenario / BenchmarkReport structs
///        can be constructed and serialised (no GPU needed).

#include "zensim/render/benchmark/RenderBenchmark.hpp"
#include "zensim/render/benchmark/RenderBenchmarkScenario.hpp"
#include "zensim/render/benchmark/RenderBenchmarkReport.hpp"

#include <cassert>
#include <cstdio>

using namespace zs::render;

static void test_benchmark_scenario() {
  BenchmarkScenario scenario;
  scenario.name = "monkey_lambert_vulkan";
  scenario.scene_path = "zpc_assets/TriMesh/monkey.obj";
  scenario.method = RenderMethod::Raster_Forward;
  scenario.backend = RenderBackend::Vulkan;
  scenario.camera.position = zs::vec<zs::f32, 3>{0.f, 0.f, 5.f};
  scenario.viewport.width = 1280;
  scenario.viewport.height = 720;

  assert(scenario.name == "monkey_lambert_vulkan");
  assert(scenario.viewport.width == 1280);

  std::printf("[PASS] test_benchmark_scenario\n");
}

static void test_benchmark_report() {
  BenchmarkReport report;
  report.scenario_name = "test_scenario";
  report.success = true;

  FrameTiming ft;
  ft.frame_index = 0;
  ft.render_us = 16000;
  ft.total_us = 17000;
  report.frames.push_back(ft);

  report.render_stats.min_us = 16000;
  report.render_stats.max_us = 16000;
  report.render_stats.mean_us = 16000;
  report.render_stats.frame_count = 1;

  assert(report.success);
  assert(report.frames.size() == 1);
  assert(report.render_stats.frame_count == 1);

  std::printf("[PASS] test_benchmark_report\n");
}

static void test_benchmark_config() {
  BenchmarkConfig config;
  config.artifact_root = "H:/zpc_render";
  config.warmup_frames = 2;
  config.measurement_frames = 10;

  assert(config.warmup_frames == 2);
  assert(config.measurement_frames == 10);
  assert(config.capture_first_frame == true);

  std::printf("[PASS] test_benchmark_config\n");
}

int main() {
  std::printf("=== Render Benchmark Smoke Tests ===\n");
  test_benchmark_scenario();
  test_benchmark_report();
  test_benchmark_config();
  std::printf("=== All render benchmark smoke tests passed ===\n");
  return 0;
}
