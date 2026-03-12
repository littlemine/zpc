/// @file RenderBackendSelection.cpp
/// @brief Stub implementation of backend probing and selection.
///
/// In this initial version we don't actually probe GPU drivers.
/// The functions return hard-coded placeholders so that the data
/// model is exercisable in tests.  Real probing (via VkContext /
/// CUDA runtime queries) will be added in PR 2+.

#include "zensim/render/RenderBackendSelection.hpp"

#include <algorithm>

namespace zs {
namespace render {

std::vector<BackendCapability> probeBackends() {
  std::vector<BackendCapability> caps;

  // Vulkan stub — mark as "available" since we're Vulkan-first.
  {
    BackendCapability vk;
    vk.backend = RenderBackend::Vulkan;
    vk.available = true;
    vk.supports_raster = true;
    vk.supports_ray_tracing = false;  // not yet wired
    vk.supports_compute = true;
    vk.device_name = "Vulkan (stub probe)";
    vk.vram_bytes = 0;
    caps.push_back(vk);
  }

  // CUDA stub — not wired yet.
  {
    BackendCapability cu;
    cu.backend = RenderBackend::CUDA;
    cu.available = false;
    cu.supports_raster = false;
    cu.supports_ray_tracing = false;
    cu.supports_compute = false;
    cu.device_name = "CUDA (not probed)";
    cu.vram_bytes = 0;
    caps.push_back(cu);
  }

  return caps;
}

BackendCapability selectBackend(bool require_raster,
                                bool require_ray_tracing) {
  auto caps = probeBackends();

  // Find best match.
  for (const auto& c : caps) {
    if (!c.available) continue;
    if (require_raster && !c.supports_raster) continue;
    if (require_ray_tracing && !c.supports_ray_tracing) continue;
    return c;
  }

  // Fallback: return first entry (Vulkan) even if unavailable —
  // caller should check `available`.
  return caps.empty() ? BackendCapability{} : caps.front();
}

}  // namespace render
}  // namespace zs
