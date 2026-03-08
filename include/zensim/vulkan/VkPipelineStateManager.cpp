#include "zensim/vulkan/VkPipelineStateManager.hpp"

#include "zensim/io/Filesystem.hpp"
#include "zensim/vulkan/VkPipelineSerialization.hpp"
#include "zensim/vulkan/VkRenderPass.hpp"
#include "zensim/vulkan/VkShader.hpp"
#include "zensim/zpc_tpls/fmt/format.h"
#include "zensim/zpc_tpls/rapidhash/rapidhash.h"

namespace zs {

  // ============================================================================
  // Constructor / Destructor
  // ============================================================================

  PipelineStateManager::PipelineStateManager(VulkanContext& ctx) : _ctx{ctx} {
    try {
      enableDiskCache();
    } catch (...) {
      // Best-effort: if disk cache setup fails (e.g. filesystem issue),
      // the manager is still usable without caching.
    }
  }

  PipelineStateManager::~PipelineStateManager() = default;

  // ============================================================================
  // Disk cache control
  // ============================================================================

  void PipelineStateManager::enableDiskCache(std::string appTag) {
    auto& tfm = default_temp_file_manager();
    tfm.ensureDirectoryExists();

    // Derive appTag if not provided (used by both caches)
    std::string tag = std::move(appTag);
    if (tag.empty()) {
      auto path = abs_exe_path();
      auto h = rapidhash(path.data(), path.size());
      tag = fmt::format("app_{:016x}", h);
    }

    // Create the Vulkan driver-level pipeline cache (loads .pso blob from disk)
    _psoCache = zs::make_unique<PipelineCacheManager>(_ctx, tfm, tag);

    // Create the descriptor-level cache (loads .bin/.json desc files)
    _diskCache = zs::make_unique<DescriptorCache>(tfm, tag);
  }

  void PipelineStateManager::disableDiskCache() noexcept {
    _psoCache.reset();
    _diskCache.reset();
  }

  void PipelineStateManager::flushPipelineCache() const {
    if (_psoCache) {
      try {
        _psoCache->save();
      } catch (...) { /* best-effort */ }
    }
  }

  vk::PipelineCache PipelineStateManager::vkPipelineCache() const noexcept {
    return _psoCache ? _psoCache->handle() : VK_NULL_HANDLE;
  }

  void PipelineStateManager::persistToCache(uint64_t key,
                                            const GraphicsPipelineDesc& desc) const {
    if (_diskCache) {
      try {
        _diskCache->save(key, desc);
      } catch (...) { /* best-effort; failure to persist is non-fatal */ }
    }
  }

  // ============================================================================
  // Key computation
  // ============================================================================

  uint64_t PipelineStateManager::computeKey(const GraphicsPipelineDesc& desc) {
    return hash_desc(desc);
  }

  // ============================================================================
  // getOrCreate (runtime shader handles)
  // ============================================================================

  Pipeline& PipelineStateManager::getOrCreate(
      const GraphicsPipelineDesc& desc, vk::RenderPass renderPass,
      const std::vector<const ShaderModule*>& shaderModules,
      const std::vector<vk::DescriptorSetLayout>& setLayouts) {
    const uint64_t key = computeKey(desc);
    const auto psoCache = vkPipelineCache();

    // 1. in-memory cache hit
    if (auto it = _cache.find(key); it != _cache.end()) return it->second.get();

    // 2. descriptor cache hit -- load desc, build pipeline (with VkPipelineCache)
    if (_diskCache) {
      GraphicsPipelineDesc cached;
      if (_diskCache->load(key, cached)) {
        _cache[key] = _ctx.createGraphicsPipeline(cached, renderPass, setLayouts, psoCache);
        return _cache[key].get();
      }
    }

    // 3. build from provided desc + runtime handles (with VkPipelineCache)
    auto builder = _ctx.pipeline();
    builder.setDesc(desc);
    builder.setRenderPass(renderPass);
    for (const auto* sm : shaderModules) builder.setShader(*sm);
    for (size_t i = 0; i < setLayouts.size(); ++i)
      builder.addDescriptorSetLayout(setLayouts[i], static_cast<int>(i));

    _cache[key] = builder.build(psoCache);
    persistToCache(key, desc);
    return _cache[key].get();
  }

  // ============================================================================
  // getOrCreateFromDesc (self-contained SPIR-V desc)
  // ============================================================================

  Pipeline& PipelineStateManager::getOrCreateFromDesc(
      const GraphicsPipelineDesc& desc, vk::RenderPass renderPass,
      const std::vector<vk::DescriptorSetLayout>& setLayouts) {
    const uint64_t key = computeKey(desc);
    const auto psoCache = vkPipelineCache();

    // 1. in-memory cache hit
    if (auto it = _cache.find(key); it != _cache.end()) return it->second.get();

    // 2. descriptor cache hit (with VkPipelineCache)
    if (_diskCache) {
      GraphicsPipelineDesc cached;
      if (_diskCache->load(key, cached)) {
        _cache[key] = _ctx.createGraphicsPipeline(cached, renderPass, setLayouts, psoCache);
        return _cache[key].get();
      }
    }

    // 3. build from the supplied desc (with VkPipelineCache)
    _cache[key] = _ctx.createGraphicsPipeline(desc, renderPass, setLayouts, psoCache);
    persistToCache(key, desc);
    return _cache[key].get();
  }

  // ============================================================================
  // Bookkeeping
  // ============================================================================

  bool PipelineStateManager::contains(uint64_t key) const noexcept {
    return _cache.find(key) != _cache.end();
  }

  bool PipelineStateManager::erase(uint64_t key) { return _cache.erase(key) > 0; }

  void PipelineStateManager::clear() noexcept { _cache.clear(); }

}  // namespace zs
