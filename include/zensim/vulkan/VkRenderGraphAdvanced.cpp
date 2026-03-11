#include "zensim/vulkan/VkRenderGraphAdvanced.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <stdexcept>

namespace zs {

// ============================================================================
// Resource Aliasing Analyzer Implementation
// ============================================================================

std::vector<ResourceLifetime> ResourceAliasingAnalyzer::computeLifetimes(
    const std::vector<VirtualResource>& resources,
    const std::vector<RenderPassNode>& passes,
    const std::vector<u32>& executionOrder) {
  std::unordered_map<ResourceId, size_t> idToIndex;
  for (size_t i = 0; i < resources.size(); ++i) {
    idToIndex[resources[i].id] = i;
  }

  std::vector<ResourceLifetime> lifetimes(resources.size());

  for (size_t i = 0; i < resources.size(); ++i) {
    lifetimes[i].resourceId = resources[i].id;
    lifetimes[i].firstPass = ~0u;
    lifetimes[i].lastPass = 0;

    if (resources[i].type == transient_resource_type_e::buffer) {
      lifetimes[i].size = resources[i].bufferDesc.size;
    } else {
      vk::ImageCreateInfo ci{};
      ci.extent = resources[i].imageDesc.extent;
      ci.format = resources[i].imageDesc.format;
      ci.mipLevels = resources[i].imageDesc.mipLevels;
      ci.arrayLayers = resources[i].imageDesc.arrayLayers;
      ci.usage = resources[i].imageDesc.usage;
      ci.samples = resources[i].imageDesc.samples;
      lifetimes[i].size = 0;
    }
  }

  for (u32 orderIdx = 0; orderIdx < executionOrder.size(); ++orderIdx) {
    u32 passIdx = executionOrder[orderIdx];
    const auto& pass = passes[passIdx];

    for (const auto& ref : pass.reads) {
      auto it = idToIndex.find(ref.resourceId);
      if (it != idToIndex.end()) {
        auto& life = lifetimes[it->second];
        life.firstPass = std::min(life.firstPass, orderIdx);
        life.lastPass = std::max(life.lastPass, orderIdx);
      }
    }
    for (const auto& ref : pass.writes) {
      auto it = idToIndex.find(ref.resourceId);
      if (it != idToIndex.end()) {
        auto& life = lifetimes[it->second];
        life.firstPass = std::min(life.firstPass, orderIdx);
        life.lastPass = std::max(life.lastPass, orderIdx);
      }
    }
  }

  return lifetimes;
}

bool ResourceAliasingAnalyzer::canAlias(const VirtualResource& a, const VirtualResource& b) const {
  if (a.type != b.type) return false;

  if (a.type == transient_resource_type_e::buffer) {
    if ((a.bufferDesc.usage & vk::BufferUsageFlagBits::eUniformBuffer)
        && (b.bufferDesc.usage & vk::BufferUsageFlagBits::eUniformBuffer)) {
      return true;
    }
    if ((a.bufferDesc.usage & vk::BufferUsageFlagBits::eStorageBuffer)
        && (b.bufferDesc.usage & vk::BufferUsageFlagBits::eStorageBuffer)) {
      return true;
    }
    return (a.bufferDesc.usage & b.bufferDesc.usage) != vk::BufferUsageFlags{};
  } else {
    return (a.imageDesc.usage & b.imageDesc.usage) != vk::ImageUsageFlags{};
  }
}

std::vector<ResourceAliasingAnalyzer::AliasGroup> ResourceAliasingAnalyzer::computeAliasing(
    const std::vector<VirtualResource>& resources,
    const std::vector<ResourceLifetime>& lifetimes,
    VulkanContext& ctx) {
  std::vector<AliasGroup> groups;
  std::vector<bool> assigned(resources.size(), false);

  for (size_t i = 0; i < resources.size(); ++i) {
    if (assigned[i]) continue;

    AliasGroup group;
    group.resources.push_back(resources[i].id);
    assigned[i] = true;

    for (size_t j = i + 1; j < resources.size(); ++j) {
      if (assigned[j]) continue;
      if (!canAlias(resources[i], resources[j])) continue;
      if (lifetimes[i].overlaps(lifetimes[j])) continue;

      group.resources.push_back(resources[j].id);
      assigned[j] = true;
    }

    vk::DeviceSize maxSize = 0;
    for (ResourceId id : group.resources) {
      for (const auto& life : lifetimes) {
        if (life.resourceId == id) {
          maxSize = std::max(maxSize, life.size);
          break;
        }
      }
    }
    group.totalSize = maxSize;

    vk::DeviceSize offset = 0;
    for (ResourceId id : group.resources) {
      group.offsets.push_back({id, offset});
    }

    groups.push_back(std::move(group));
  }

  return groups;
}

// ============================================================================
// Multi-Queue Scheduler Implementation
// ============================================================================

QueueType MultiQueueScheduler::selectQueue(const RenderPassNode& pass, VulkanContext& ctx) {
  switch (pass.type) {
    case PassType::Compute:
    case PassType::AsyncCompute:
    case PassType::RayTracing:
      if (ctx.isQueueValid(vk_queue_e::dedicated_compute)) {
        return QueueType::Compute;
      }
      if (ctx.isQueueValid(vk_queue_e::compute)) {
        return QueueType::Compute;
      }
      return QueueType::Graphics;

    case PassType::Transfer:
    case PassType::Copy:
    case PassType::Blit:
    case PassType::Clear:
      if (ctx.isQueueValid(vk_queue_e::dedicated_transfer)) {
        return QueueType::Transfer;
      }
      if (ctx.isQueueValid(vk_queue_e::transfer)) {
        return QueueType::Transfer;
      }
      return QueueType::Graphics;

    case PassType::Graphics:
    case PassType::Present:
    default:
      return QueueType::Graphics;
  }
}

MultiQueueScheduler::ScheduleResult MultiQueueScheduler::schedule(
    const std::vector<RenderPassNode>& passes,
    const std::vector<u32>& topologicalOrder,
    VulkanContext& ctx) {
  ScheduleResult result;
  result.passToTimeline.resize(passes.size());
  result.passToIndex.resize(passes.size());

  std::array<std::vector<u32>, static_cast<size_t>(QueueType::Count)> queuePasses;

  for (u32 passIdx : topologicalOrder) {
    const auto& pass = passes[passIdx];
    QueueType queue = selectQueue(pass, ctx);
    queuePasses[static_cast<size_t>(queue)].push_back(passIdx);
    result.passToTimeline[passIdx] = static_cast<u32>(queue);
    result.passToIndex[passIdx] =
        static_cast<u32>(queuePasses[static_cast<size_t>(queue)].size() - 1);
  }

  for (size_t i = 0; i < static_cast<size_t>(QueueType::Count); ++i) {
    if (!queuePasses[i].empty()) {
      QueueTimeline timeline;
      timeline.type = static_cast<QueueType>(i);
      timeline.passes = std::move(queuePasses[i]);
      result.timelines.push_back(std::move(timeline));
    }
  }

  std::unordered_map<ResourceId, std::pair<u32, QueueType>> lastWriter;

  for (u32 orderIdx : topologicalOrder) {
    u32 passIdx = orderIdx;
    const auto& pass = passes[passIdx];
    QueueType currentQueue = static_cast<QueueType>(result.passToTimeline[passIdx]);

    for (const auto& ref : pass.reads) {
      auto it = lastWriter.find(ref.resourceId);
      if (it != lastWriter.end() && it->second.second != currentQueue) {
        QueueSyncPoint sync;
        sync.passId = passIdx;
        sync.srcQueue = it->second.second;
        sync.dstQueue = currentQueue;
        sync.resources.push_back(ref.resourceId);
        sync.needsSemaphore = true;
        result.syncPoints.push_back(sync);
      }
    }

    for (const auto& ref : pass.writes) {
      lastWriter[ref.resourceId] = {passIdx, currentQueue};
    }
  }

  return result;
}

// ============================================================================
// Advanced Render Graph Compiler Implementation
// ============================================================================

AdvancedRenderGraphCompiler::AdvancedCompilationResult
AdvancedRenderGraphCompiler::compileAdvanced(
    std::vector<VirtualResource>& resources,
    std::vector<RenderPassNode>& passes) {
  AdvancedCompilationResult result;

  auto baseResult = compile(resources, passes);
  if (!baseResult.success) {
    result.success = false;
    result.error = baseResult.error;
    return result;
  }

  result.compiledPasses = std::move(baseResult.compiledPasses);
  result.executionOrder = std::move(baseResult.executionOrder);

  if (enableAliasing && !resources.empty()) {
    ResourceAliasingAnalyzer analyzer;
    auto lifetimes = analyzer.computeLifetimes(resources, passes, result.executionOrder);
    result.aliasGroups = analyzer.computeAliasing(resources, lifetimes, ctx);
    applyAliasing(resources, passes, result.aliasGroups);
  }

  if (enableMultiQueue && passes.size() > 1) {
    MultiQueueScheduler scheduler;
    result.schedule = scheduler.schedule(passes, result.executionOrder, ctx);

    for ([[maybe_unused]] const auto& sync : result.schedule.syncPoints) {
      result.timelineSemaphores.emplace_back(ctx.createTimelineSemaphore(0));
    }
  }

  result.success = true;
  return result;
}

void AdvancedRenderGraphCompiler::applyAliasing(
    std::vector<VirtualResource>& resources,
    [[maybe_unused]] std::vector<RenderPassNode>& passes,
    std::vector<ResourceAliasingAnalyzer::AliasGroup>& aliasGroups) {
  for (auto& group : aliasGroups) {
    if (group.resources.size() <= 1) continue;

    for (ResourceId id : group.resources) {
      for (auto& res : resources) {
        if (res.id == id && res.type == transient_resource_type_e::buffer) {
          res.bufferDesc.size = group.totalSize;
        }
      }
    }
  }
}

// ============================================================================
// Advanced Render Graph Executor Implementation
// ============================================================================

void AdvancedRenderGraphExecutor::executeMultiQueue(
    const RenderGraph& graph,
    const MultiQueueScheduler::ScheduleResult& schedule,
    const std::vector<Owner<TimelineSemaphore>>& semaphores) {
  if (schedule.timelines.size() <= 1) {
    vk_queue_e queueType = vk_queue_e::graphics;
    if (!schedule.timelines.empty()) {
      switch (schedule.timelines[0].type) {
        case QueueType::Compute:  queueType = vk_queue_e::compute;  break;
        case QueueType::Transfer: queueType = vk_queue_e::transfer; break;
        default:                  queueType = vk_queue_e::graphics; break;
      }
    }
    execute(graph, graph.ctx->getQueue(queueType), VK_NULL_HANDLE);
    return;
  }

  std::vector<vk::CommandBuffer> commandBuffers;

  for (const auto& timeline : schedule.timelines) {
    auto& env = graph.ctx->env();
    auto vkQueueType = timeline.type == QueueType::Graphics   ? vk_queue_e::graphics
                       : timeline.type == QueueType::Compute ? vk_queue_e::compute
                                                             : vk_queue_e::transfer;
    auto& pool = env.pools(vkQueueType);

    vk::CommandBuffer cmd = pool.createCommandBuffer(vk::CommandBufferLevel::ePrimary, true,
                                                     nullptr, vk_cmd_usage_e::single_use);
    commandBuffers.push_back(cmd);

    for (u32 passIdx : timeline.passes) {
      const auto& pass = graph.passes[passIdx];
      const auto& compiled = graph.compiledPasses[passIdx];

      injectBarriers(cmd, compiled.preBarriers, graph.resources, const_cast<RenderGraph&>(graph));

      recordPass(cmd, pass, compiled);

      injectBarriers(cmd, compiled.postBarriers, graph.resources, const_cast<RenderGraph&>(graph));
    }

    cmd.end();
  }

  (void)semaphores;

  // Submit each timeline's command buffer to its queue with a per-queue fence
  // to guarantee all GPU work is complete before returning.
  std::vector<Fence> fences;
  fences.reserve(schedule.timelines.size());

  for (size_t i = 0; i < schedule.timelines.size(); ++i) {
    vk::Queue queue;
    switch (schedule.timelines[i].type) {
      case QueueType::Graphics:
        queue = graph.ctx->getQueue(vk_queue_e::graphics);
        break;
      case QueueType::Compute:
        queue = graph.ctx->getQueue(vk_queue_e::compute);
        break;
      case QueueType::Transfer:
        queue = graph.ctx->getQueue(vk_queue_e::transfer);
        break;
      default:
        queue = graph.ctx->getQueue(vk_queue_e::graphics);
        break;
    }

    vk::CommandBufferSubmitInfo cmdInfo{commandBuffers[i]};
    vk::SubmitInfo2 submitInfo{};
    submitInfo.setCommandBufferInfoCount(1)
             .setPCommandBufferInfos(&cmdInfo);

    fences.emplace_back(*graph.ctx, false);
    (void)queue.submit2(1, &submitInfo, *fences.back(), graph.ctx->dispatcher);
  }

  // Wait for all queues to finish
  for (auto& f : fences) f.wait();
}

// ============================================================================
// Advanced Render Graph Implementation
// ============================================================================

void AdvancedRenderGraph::executeAdvanced() {
  if (!isValid()) {
    throw std::runtime_error("Cannot execute invalid render graph");
  }

  AdvancedRenderGraphExecutor executor(*ctx);
  executor.executeMultiQueue(*this, schedule, timelineSemaphores);
}

AdvancedRenderGraph AdvancedRenderGraph::build(VulkanContext& ctx,
                                               std::function<void(RenderGraphBuilder&)> setup,
                                               bool enableAliasing,
                                               bool enableMultiQueue) {
  RenderGraphBuilder builder(ctx);
  if (setup) {
    setup(builder);
  }

  AdvancedRenderGraph graph;
  graph.ctx = &ctx;
  graph.resources = std::move(builder.resources);
  graph.passes = std::move(builder.passes);
  graph.importedBuffers = std::move(builder.importedBuffers);
  graph.importedImages = std::move(builder.importedImages);
  graph.importedVkBuffers = std::move(builder.importedVkBuffers);
  graph.importedVkImages = std::move(builder.importedVkImages);
  graph.importedVkImageViews = std::move(builder.importedVkImageViews);

  AdvancedRenderGraphCompiler compiler(ctx, enableAliasing, enableMultiQueue);
  auto result = compiler.compileAdvanced(graph.resources, graph.passes);

  if (!result.success) {
    throw std::runtime_error("Render graph compilation failed: " + result.error);
  }

  graph.compiledPasses = std::move(result.compiledPasses);
  graph.executionOrder = std::move(result.executionOrder);
  graph.aliasGroups = std::move(result.aliasGroups);
  graph.schedule = std::move(result.schedule);
  graph.timelineSemaphores = std::move(result.timelineSemaphores);

  for (auto& res : graph.resources) {
    if (res.type == transient_resource_type_e::buffer) {
      auto it = std::find_if(graph.importedBuffers.begin(), graph.importedBuffers.end(),
                             [&](const auto& p) { return p.first == res.id; });
      auto ext = graph.importedVkBuffers.find(res.id);
      if (it == graph.importedBuffers.end() && ext == graph.importedVkBuffers.end()) {
        graph.allocatedBuffers.emplace(res.id,
            Owner<Buffer>{ctx.createBuffer(res.bufferDesc.size, res.bufferDesc.usage,
                                           res.bufferDesc.memoryProperties)});
      }
    } else {
      auto it = std::find_if(graph.importedImages.begin(), graph.importedImages.end(),
                             [&](const auto& p) { return p.first == res.id; });
      auto ext = graph.importedVkImages.find(res.id);
      if (it == graph.importedImages.end() && ext == graph.importedVkImages.end()) {
        auto imageCI = vk::ImageCreateInfo{}
                           .setImageType(res.imageDesc.imageType)
                           .setFormat(res.imageDesc.format)
                           .setExtent(res.imageDesc.extent)
                           .setMipLevels(res.imageDesc.mipLevels)
                           .setArrayLayers(res.imageDesc.arrayLayers)
                           .setUsage(res.imageDesc.usage)
                           .setSamples(res.imageDesc.samples)
                           .setTiling(vk::ImageTiling::eOptimal)
                           .setSharingMode(vk::SharingMode::eExclusive);
        graph.allocatedImages.emplace(res.id,
            Owner<Image>{ctx.createImage(imageCI, res.imageDesc.memoryProperties, true)});
      }
    }
  }

  graph.compiled = true;
  return graph;
}

// ============================================================================
// Aliased Memory Pool Implementation
// ============================================================================

AliasedMemoryPool::AliasedMemoryPool(VulkanContext& ctx, vk::DeviceSize initialSize)
    : _ctx{ctx}, _capacity{initialSize}, _used{0} {
}

AliasedMemoryPool::~AliasedMemoryPool() {
  if (_memory != VK_NULL_HANDLE) {
    _ctx.device.freeMemory(_memory, nullptr, _ctx.dispatcher);
  }
}

AliasedMemoryPool::Allocation AliasedMemoryPool::allocate(ResourceId id, vk::DeviceSize size,
                                                          vk::DeviceSize alignment) {
  Allocation alloc;
  alloc.resourceId = id;
  alloc.size = size;

  vk::DeviceSize alignedOffset = (_used + alignment - 1) & ~(alignment - 1);
  alloc.offset = alignedOffset;

  if (alignedOffset + size > _capacity) {
    _capacity = std::max(_capacity * 2, alignedOffset + size);
  }

  _used = alignedOffset + size;
  _allocations.push_back(alloc);
  _resourceToAlloc[id] = _allocations.size() - 1;

  return alloc;
}

void AliasedMemoryPool::free(ResourceId id) {
  auto it = _resourceToAlloc.find(id);
  if (it != _resourceToAlloc.end()) {
    _allocations[it->second].resourceId = InvalidResourceId;
    _resourceToAlloc.erase(it);
  }
}

void AliasedMemoryPool::reset() {
  _used = 0;
  _allocations.clear();
  _resourceToAlloc.clear();
}

}  // namespace zs
