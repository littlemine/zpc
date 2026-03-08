#pragma once
/**
 * @file VkRenderGraphAdvanced.hpp
 * @brief Advanced render-graph features: resource aliasing, multi-queue scheduling,
 *        aliased memory pool, and utility helpers.
 *
 * Extends VkRenderGraph.hpp with:
 *  - ResourceAliasingAnalyzer  – compute resource lifetimes & alias groups
 *  - MultiQueueScheduler       – assign passes to optimal queues, emit sync points
 *  - AdvancedRenderGraphCompiler / Executor
 *  - AliasedMemoryPool         – sub-allocate a single VkDeviceMemory for aliased resources
 *  - rg_util helpers           – hazard detection, stage/queue flag conversions
 */

#include "zensim/vulkan/VkRenderGraph.hpp"
#include "zensim/vulkan/VkCommand.hpp"
#include <unordered_set>

namespace zs {

// ============================================================================
// Resource Aliasing Analysis
// ============================================================================

struct ResourceLifetime {
  ResourceId resourceId;
  u32 firstPass{~0u};
  u32 lastPass{0};
  vk::DeviceSize size{0};
  vk::MemoryRequirements memoryRequirements{};

  bool overlaps(const ResourceLifetime& other) const {
    return !(lastPass < other.firstPass || firstPass > other.lastPass);
  }
};

struct ZPC_CORE_API ResourceAliasingAnalyzer {
  struct AliasGroup {
    std::vector<ResourceId> resources;
    vk::DeviceSize totalSize{0};
    std::vector<std::pair<ResourceId, vk::DeviceSize>> offsets;
  };

  std::vector<ResourceLifetime> computeLifetimes(
      const std::vector<VirtualResource>& resources,
      const std::vector<RenderPassNode>& passes,
      const std::vector<u32>& executionOrder);

  std::vector<AliasGroup> computeAliasing(
      const std::vector<VirtualResource>& resources,
      const std::vector<ResourceLifetime>& lifetimes,
      VulkanContext& ctx);

private:
  bool canAlias(const VirtualResource& a, const VirtualResource& b) const;
};

// ============================================================================
// Multi-Queue Synchronization
// ============================================================================

struct QueueSyncPoint {
  u32 passId{~0u};
  QueueType srcQueue{QueueType::Graphics};
  QueueType dstQueue{QueueType::Graphics};
  std::vector<ResourceId> resources;
  bool needsSemaphore{false};
};

struct ZPC_CORE_API MultiQueueScheduler {
  struct QueueTimeline {
    QueueType type;
    std::vector<u32> passes;
    u64 lastSignalValue{0};
  };

  struct ScheduleResult {
    std::vector<QueueTimeline> timelines;
    std::vector<QueueSyncPoint> syncPoints;
    std::vector<u32> passToTimeline;
    std::vector<u32> passToIndex;
  };

  ScheduleResult schedule(
      const std::vector<RenderPassNode>& passes,
      const std::vector<u32>& topologicalOrder,
      VulkanContext& ctx);

private:
  QueueType selectQueue(const RenderPassNode& pass, VulkanContext& ctx);
};

// ============================================================================
// Advanced Render Graph Compiler with Aliasing and Multi-Queue
// ============================================================================

struct ZPC_CORE_API AdvancedRenderGraphCompiler : RenderGraphCompiler {
  explicit AdvancedRenderGraphCompiler(VulkanContext& ctx, bool enableAliasing = true,
                                       bool enableMultiQueue = true)
      : RenderGraphCompiler(ctx), enableAliasing{enableAliasing}, enableMultiQueue{enableMultiQueue} {}

  struct AdvancedCompilationResult : CompilationResult {
    std::vector<ResourceAliasingAnalyzer::AliasGroup> aliasGroups;
    MultiQueueScheduler::ScheduleResult schedule;
    std::vector<std::vector<BarrierInfo>> queueBarriers;
    std::vector<Owner<TimelineSemaphore>> timelineSemaphores;
  };

  AdvancedCompilationResult compileAdvanced(
      std::vector<VirtualResource>& resources,
      std::vector<RenderPassNode>& passes);

  bool enableAliasing{true};
  bool enableMultiQueue{true};

private:
  void applyAliasing(std::vector<VirtualResource>& resources,
                     std::vector<RenderPassNode>& passes,
                     std::vector<ResourceAliasingAnalyzer::AliasGroup>& aliasGroups);
};

// ============================================================================
// Advanced Render Graph Executor
// ============================================================================

struct ZPC_CORE_API AdvancedRenderGraphExecutor : RenderGraphExecutor {
  explicit AdvancedRenderGraphExecutor(VulkanContext& ctx) : RenderGraphExecutor(ctx) {}

  void executeMultiQueue(const RenderGraph& graph,
                         const MultiQueueScheduler::ScheduleResult& schedule,
                         const std::vector<Owner<TimelineSemaphore>>& semaphores);
};

// ============================================================================
// Render Graph with Advanced Features
// ============================================================================

struct ZPC_CORE_API AdvancedRenderGraph : RenderGraph {
  std::vector<ResourceAliasingAnalyzer::AliasGroup> aliasGroups;
  MultiQueueScheduler::ScheduleResult schedule;
  std::vector<Owner<TimelineSemaphore>> timelineSemaphores;

  void executeAdvanced();

  static AdvancedRenderGraph build(VulkanContext& ctx,
                                   std::function<void(RenderGraphBuilder&)> setup,
                                   bool enableAliasing = true,
                                   bool enableMultiQueue = true);
};

// ============================================================================
// Memory Pool for Resource Aliasing
// ============================================================================

struct ZPC_CORE_API AliasedMemoryPool {
  explicit AliasedMemoryPool(VulkanContext& ctx, vk::DeviceSize initialSize = 64 * 1024 * 1024);
  ~AliasedMemoryPool();

  struct Allocation {
    vk::DeviceSize offset{0};
    vk::DeviceSize size{0};
    ResourceId resourceId{InvalidResourceId};
  };

  Allocation allocate(ResourceId id, vk::DeviceSize size, vk::DeviceSize alignment);
  void free(ResourceId id);
  void reset();

  vk::DeviceMemory memory() const { return _memory; }
  vk::DeviceSize capacity() const { return _capacity; }
  vk::DeviceSize used() const { return _used; }

private:
  VulkanContext& _ctx;
  vk::DeviceMemory _memory{VK_NULL_HANDLE};
  vk::DeviceSize _capacity{0};
  vk::DeviceSize _used{0};
  std::vector<Allocation> _allocations;
  std::unordered_map<ResourceId, size_t> _resourceToAlloc;
};

// ============================================================================
// Utility Functions
// ============================================================================

namespace rg_util {

inline vk::PipelineStageFlags2 getStageFlags(PassType type) {
  switch (type) {
    case PassType::Compute:
    case PassType::AsyncCompute:
    case PassType::RayTracing:
      return vk::PipelineStageFlagBits2::eComputeShader;
    case PassType::Graphics:
      return vk::PipelineStageFlagBits2::eAllGraphics;
    case PassType::Transfer:
    case PassType::Copy:
    case PassType::Blit:
    case PassType::Clear:
      return vk::PipelineStageFlagBits2::eTransfer;
    case PassType::Present:
      return vk::PipelineStageFlagBits2::eBottomOfPipe;
    default:
      return vk::PipelineStageFlagBits2::eAllCommands;
  }
}

inline vk::QueueFlagBits getQueueFlag(QueueType type) {
  switch (type) {
    case QueueType::Graphics:
      return vk::QueueFlagBits::eGraphics;
    case QueueType::Compute:
      return vk::QueueFlagBits::eCompute;
    case QueueType::Transfer:
      return vk::QueueFlagBits::eTransfer;
    default:
      return vk::QueueFlagBits::eGraphics;
  }
}

inline bool isHazard(const ResourceAccessInfo& prev, const ResourceAccessInfo& next) {
  if (prev.access & vk::AccessFlagBits2::eShaderWrite
      || prev.access & vk::AccessFlagBits2::eColorAttachmentWrite
      || prev.access & vk::AccessFlagBits2::eDepthStencilAttachmentWrite
      || prev.access & vk::AccessFlagBits2::eTransferWrite) {
    return true;
  }
  if (prev.access & vk::AccessFlagBits2::eShaderStorageWrite
      && next.access & vk::AccessFlagBits2::eShaderRead) {
    return true;
  }
  return false;
}

inline bool needsMemoryBarrier(const ResourceAccessInfo& src, const ResourceAccessInfo& dst) {
  if (src.stages == dst.stages && src.access == dst.access) return false;
  if (src.access & vk::AccessFlagBits2::eMemoryWrite
      || dst.access & vk::AccessFlagBits2::eMemoryRead) {
    return true;
  }
  return isHazard(src, dst);
}

}  // namespace rg_util

}  // namespace zs
