#pragma once
/// @file RenderBackendSelection.hpp
/// @brief Queries available render backends and selects the best
///        one based on capability profiles.

#include "zensim/render/RenderTypes.hpp"

#include <string>
#include <vector>

namespace zs {
namespace render {

  /// Capability summary for a single backend.
  struct BackendCapability {
    RenderBackend backend{RenderBackend::Vulkan};
    bool available{false};            ///< Runtime check passed.
    bool supports_raster{false};
    bool supports_ray_tracing{false};
    bool supports_compute{false};
    std::string device_name;          ///< e.g. "NVIDIA RTX 4090"
    uint64_t vram_bytes{0};
  };

  /// Query all backends and return their capabilities.
  /// This is a lightweight probe — it should not create heavyweight
  /// contexts.
  std::vector<BackendCapability> probeBackends();

  /// Select the best backend satisfying the given constraints.
  /// Returns Vulkan by default if nothing is available (the caller
  /// is expected to check `available` on the result).
  BackendCapability selectBackend(bool require_raster = true,
                                  bool require_ray_tracing = false);

}  // namespace render
}  // namespace zs
