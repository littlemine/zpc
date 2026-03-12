#pragma once
/// @file RenderBenchmarkScenario.hpp
/// @brief Describes a single benchmark scenario (scene + method
///        + backend + camera).

#include "zensim/render/RenderTypes.hpp"
#include "zensim/render/RenderFrameRequest.hpp"

#include <string>

namespace zs {
namespace render {

  /// A benchmark scenario — enough information to produce a
  /// RenderFrameRequest for each benchmark frame.
  struct BenchmarkScenario {
    std::string name;                ///< Human label (e.g. "monkey_lambert_vulkan").
    std::string scene_path;          ///< Path to source asset (OBJ).
    RenderMethod method{RenderMethod::Raster_Forward};
    RenderBackend backend{RenderBackend::Vulkan};
    Camera camera;                   ///< Fixed camera for reproducibility.
    Viewport viewport;               ///< Resolution / format.
    uint32_t spp{1};                 ///< Samples-per-pixel (offline).
  };

}  // namespace render
}  // namespace zs
