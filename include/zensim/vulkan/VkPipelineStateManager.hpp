#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>

#include "zensim/ZpcImplPattern.hpp"
#include "zensim/vulkan/VkPipeline.hpp"
#include "zensim/vulkan/VkShaderManager.hpp"

namespace zs {

  struct DescriptorCache;
  struct PipelineCacheManager;

  /// @brief Pipeline State Object (PSO) manager.
  ///
  /// Caches fully-built `Pipeline` objects keyed by a 64-bit hash of the
  /// `GraphicsPipelineDesc` alone.  The desc is the single source of truth for
  /// identity -- runtime Vulkan handles (render pass, shader modules, descriptor
  /// set layouts) are needed only to *build* the pipeline, not to identify it.
  ///
  /// When disk caching is enabled the manager maintains three cache tiers:
  ///   - **Vulkan pipeline cache** (`.pso` file) -- driver-level binary blob
  ///     passed to `vkCreateGraphicsPipelines` so the driver can skip redundant
  ///     shader compilation.  Checked/used on every pipeline build call.
  ///   - **Descriptor cache** (`.bin` / `.json` files) -- serialized
  ///     `GraphicsPipelineDesc` that is loaded when the in-memory cache misses,
  ///     allowing the pipeline to be rebuilt from persisted state.
  ///   - **In-memory cache** -- `Pipeline` objects keyed by desc hash.
  ///
  /// Lookup order on cache miss:
  ///   1. In-memory cache (hash map).
  ///   2. Descriptor cache -- load desc (binary first, JSON fallback), rebuild
  ///      pipeline using the Vulkan pipeline cache.
  ///   3. Build from the supplied desc + runtime handles, using the Vulkan
  ///      pipeline cache, then persist the desc to disk.
  ///
  /// Disk caching is enabled by default.  The constructor derives a cache
  /// tag from the executable path and creates the on-disk structures
  /// automatically.  Call `disableDiskCache()` if persistence is unwanted.
  ///
  /// Typical usage:
  /// @code
  ///   auto& mgr = PipelineStateManager(ctx);
  ///   // disk cache already active -- no explicit enableDiskCache() needed
  ///   auto& pipeline = mgr.getOrCreate(desc, renderPass, shaders, setLayouts);
  /// @endcode
  struct ZPC_CORE_API PipelineStateManager {
    explicit PipelineStateManager(VulkanContext& ctx);
    ~PipelineStateManager();
    PipelineStateManager(PipelineStateManager&&) = default;
    PipelineStateManager& operator=(PipelineStateManager&&) = default;
    PipelineStateManager(const PipelineStateManager&) = delete;
    PipelineStateManager& operator=(const PipelineStateManager&) = delete;

    // ---- disk cache control ----

    /// @brief Enable on-disk caching (both Vulkan pipeline cache and descriptor cache).
    /// @param appTag  Application-specific tag for cache isolation (e.g. exe checksum).
    ///               If empty, a tag is derived from the executable path.
    void enableDiskCache(std::string appTag = {});

    /// @brief Disable on-disk caching and release the Vulkan pipeline cache.
    void disableDiskCache() noexcept;

    /// @brief Check whether disk caching is active.
    bool diskCacheEnabled() const noexcept { return _diskCache != nullptr; }

    /// @brief Flush the Vulkan pipeline cache blob to disk immediately.
    void flushPipelineCache() const;

    // ---- pipeline access ----

    /// @brief Obtain or build a graphics pipeline from a desc + runtime handles.
    ///
    /// Lookup order:
    ///   1. In-memory cache (by hash key).
    ///   2. Descriptor cache -- load desc, build pipeline (with VkPipelineCache).
    ///   3. Build from the supplied desc + runtime handles (with VkPipelineCache).
    ///
    /// After a successful build from (2) or (3) the desc is persisted to disk
    /// when disk caching is enabled.
    Pipeline& getOrCreate(
        const GraphicsPipelineDesc& desc, vk::RenderPass renderPass,
        const std::vector<const ShaderModule*>& shaderModules,
        const std::vector<vk::DescriptorSetLayout>& setLayouts = {});

    /// @brief Build a graphics pipeline from a self-contained desc (with SPIR-V).
    ///
    /// Same lookup order as getOrCreate, but runtime shader modules are taken
    /// from the SPIR-V already embedded in the desc.
    Pipeline& getOrCreateFromDesc(
        const GraphicsPipelineDesc& desc, vk::RenderPass renderPass,
        const std::vector<vk::DescriptorSetLayout>& setLayouts = {});

    /// @brief Check whether a pipeline with the given key is cached.
    bool contains(uint64_t key) const noexcept;

    /// @brief Evict a single cached pipeline.
    bool erase(uint64_t key);

    /// @brief Destroy all cached pipelines.
    void clear() noexcept;

    /// @brief Number of cached pipelines.
    size_t size() const noexcept { return _cache.size(); }

    /// @brief Compute the cache key for a descriptor.
    static uint64_t computeKey(const GraphicsPipelineDesc& desc);

    VulkanContext& context() noexcept { return _ctx; }
    const VulkanContext& context() const noexcept { return _ctx; }

  private:
    void persistToCache(uint64_t key, const GraphicsPipelineDesc& desc) const;
    vk::PipelineCache vkPipelineCache() const noexcept;

    VulkanContext& _ctx;
    std::unordered_map<uint64_t, Owner<Pipeline>> _cache;
    std::unique_ptr<DescriptorCache> _diskCache;
    std::unique_ptr<PipelineCacheManager> _psoCache;
  };

}  // namespace zs
