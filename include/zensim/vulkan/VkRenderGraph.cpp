#include "zensim/vulkan/VkRenderGraph.hpp"
#include "zensim/vulkan/VkShader.hpp"

#include <algorithm>
#include <queue>
#include <stack>
#include <stdexcept>

namespace zs {

// ============================================================================
// PassPipelineCache Implementation
// ============================================================================

void PassPipelineCache::markDirty(const std::string& tag) {
  auto it = _entries.find(tag);
  if (it != _entries.end()) it->second.dirty = true;
}

void PassPipelineCache::markAllDirty() {
  for (auto& [_, entry] : _entries) entry.dirty = true;
}

bool PassPipelineCache::needsRebuild(const std::string& tag) const {
  auto it = _entries.find(tag);
  return it == _entries.end() || it->second.dirty;
}

PassPipelineInfo PassPipelineCache::getOrBuild(
    const std::string& tag,
    const PassPipelineDesc& desc,
    vk::RenderPass renderPass,
    const std::vector<vk::DescriptorSetLayout>& setLayouts,
  std::vector<UniquePtr<Pipeline>>& retiredPipelines) {

  auto it = _entries.find(tag);
  if (it != _entries.end() && !it->second.dirty && it->second.pipeline) {
    // Cache hit — reuse existing pipeline
    auto& p = *it->second.pipeline;
    PassPipelineInfo info;
    info.pipeline = *p;
    info.layout = static_cast<vk::PipelineLayout>(p);
    info.bindPoint = desc.isGraphics() ? vk::PipelineBindPoint::eGraphics
                                       : vk::PipelineBindPoint::eCompute;
    return info;
  }

  // Move old pipeline to retired list (kept alive by graph until fence wait)
  if (it != _entries.end() && it->second.pipeline) {
    retiredPipelines.push_back(std::move(it->second.pipeline));
  }

  // Build new pipeline
  PassPipelineInfo info;

  if (desc.isGraphics()) {
    auto pipeline = _ctx.createGraphicsPipeline(
        desc.graphicsDesc, renderPass, setLayouts);
    info.pipeline = *pipeline;
    info.layout = static_cast<vk::PipelineLayout>(pipeline);
    info.bindPoint = vk::PipelineBindPoint::eGraphics;
    _entries[tag] = CachedEntry{
      zs::make_unique<Pipeline>(std::move(pipeline)), false};

  } else if (desc.isCompute()) {
    const auto& shaderDesc = desc.computeShader;
    auto shaderModule = _ctx.createShaderModule(
        shaderDesc.spirv.data(), shaderDesc.spirv.size(), shaderDesc.stage);

    VulkanContext::PipelineLayoutDesc layoutDesc;
    layoutDesc.pushConstantRanges = desc.computePushConstantRanges;
    auto pipelineLayout = _ctx.createPipelineLayout(layoutDesc, setLayouts);

    VulkanContext::ComputePipelineDesc cpDesc;
    cpDesc.shader = &shaderModule;
    cpDesc.pipelineLayout = pipelineLayout;
    auto pipeline = _ctx.createComputePipeline(cpDesc);

    info.pipeline = *pipeline;
    info.layout = static_cast<vk::PipelineLayout>(pipeline);
    info.bindPoint = vk::PipelineBindPoint::eCompute;
    _entries[tag] = CachedEntry{
      zs::make_unique<Pipeline>(std::move(pipeline)), false};
  }

  return info;
}

void PassPipelineCache::erase(const std::string& tag) {
  _entries.erase(tag);
}

void PassPipelineCache::clear() {
  _entries.clear();
}

// ============================================================================
// RenderGraphCompileCache Implementation
// ============================================================================

bool RenderGraphCompileCache::tryGet(u64 signature,
                                     CompilationResult& out) const {
  auto it = _cache.find(signature);
  if (it == _cache.end()) return false;
  out = it->second;
  return true;
}

void RenderGraphCompileCache::store(u64 signature,
                                    const CompilationResult& result) {
  _cache[signature] = result;
}

void RenderGraphCompileCache::erase(u64 signature) {
  _cache.erase(signature);
}

void RenderGraphCompileCache::clear() {
  _cache.clear();
}

// ============================================================================
// RenderGraphBuilder Implementation
// ============================================================================

ResourceId RenderGraphBuilder::createBuffer(std::string_view name,
                                            const TransientBufferDesc& desc) {
  ResourceId id = nextResourceId++;
  resources.push_back(VirtualResource::makeBuffer(id, name, desc));
  return id;
}

ResourceId RenderGraphBuilder::createImage(std::string_view name,
                                           const TransientImageDesc& desc) {
  ResourceId id = nextResourceId++;
  resources.push_back(VirtualResource::makeImage(id, name, desc));
  return id;
}

ResourceId RenderGraphBuilder::importBuffer(std::string_view name, Buffer* buffer) {
  ResourceId id = nextResourceId++;
  VirtualResource res{id, name, transient_resource_type_e::buffer};
  res.bufferDesc.size = buffer->getSize();
  res.bufferDesc.usage = vk::BufferUsageFlags{};
  resources.push_back(res);
  importedBuffers.push_back({id, buffer});
  return id;
}

ResourceId RenderGraphBuilder::importBuffer(std::string_view name, Buffer* buffer,
                                            const TransientBufferDesc& desc) {
  ResourceId id = nextResourceId++;
  VirtualResource res{id, name, transient_resource_type_e::buffer};
  res.bufferDesc = desc;
  resources.push_back(res);
  importedBuffers.push_back({id, buffer});
  return id;
}

ResourceId RenderGraphBuilder::importBuffer(std::string_view name, vk::Buffer buffer,
                                            const TransientBufferDesc& desc) {
  ResourceId id = nextResourceId++;
  VirtualResource res{id, name, transient_resource_type_e::buffer};
  res.bufferDesc = desc;
  resources.push_back(res);
  importedVkBuffers.emplace(id, buffer);
  return id;
}

ResourceId RenderGraphBuilder::importImage(std::string_view name, Image* image) {
  ResourceId id = nextResourceId++;
  VirtualResource res{id, name, transient_resource_type_e::image};
  res.imageDesc.extent = image->getExtent();
  res.imageDesc.format = vk::Format::eUndefined;
  res.imageDesc.usage = vk::ImageUsageFlags{};
  resources.push_back(res);
  importedImages.push_back({id, image});
  return id;
}

ResourceId RenderGraphBuilder::importImage(std::string_view name, Image* image,
                                           const TransientImageDesc& desc) {
  ResourceId id = nextResourceId++;
  VirtualResource res{id, name, transient_resource_type_e::image};
  res.imageDesc = desc;
  resources.push_back(res);
  importedImages.push_back({id, image});
  return id;
}

ResourceId RenderGraphBuilder::importImage(std::string_view name, vk::Image image,
                                           const TransientImageDesc& desc) {
  ResourceId id = nextResourceId++;
  VirtualResource res{id, name, transient_resource_type_e::image};
  res.imageDesc = desc;
  resources.push_back(res);
  importedVkImages.emplace(id, image);
  return id;
}

ResourceId RenderGraphBuilder::importImage(std::string_view name, vk::Image image,
                                           vk::ImageView view,
                                           const TransientImageDesc& desc) {
  ResourceId id = nextResourceId++;
  VirtualResource res{id, name, transient_resource_type_e::image};
  res.imageDesc = desc;
  resources.push_back(res);
  importedVkImages.emplace(id, image);
  importedVkImageViews.emplace(id, view);
  return id;
}

u32 RenderGraphBuilder::addPass(std::string_view name, PassType type, QueueType queue,
                                std::function<void(RenderPassNode&)> setup) {
  u32 id = nextPassId++;
  passes.emplace_back(id, name, type);
  passes.back().queueType = queue;
  if (setup) {
    setup(passes.back());
  }
  return id;
}

u32 RenderGraphBuilder::insertPassBefore(
    u32 targetPassId, std::string_view name, PassType type, QueueType queue,
    std::function<void(RenderPassNode&)> setup) {
  if (!hasPass(targetPassId)) {
    throw std::runtime_error("Target pass not found: " + std::to_string(targetPassId));
  }
  u32 id = addPass(name, type, queue, std::move(setup));
  addDependency(id, targetPassId);
  return id;
}

u32 RenderGraphBuilder::insertPassAfter(
    u32 targetPassId, std::string_view name, PassType type, QueueType queue,
    std::function<void(RenderPassNode&)> setup, bool rewireDependents) {
  if (!hasPass(targetPassId)) {
    throw std::runtime_error("Target pass not found: " + std::to_string(targetPassId));
  }

  u32 id = addPass(name, type, queue, std::move(setup));
  addDependency(targetPassId, id);

  if (rewireDependents) {
    for (auto& p : passes) {
      if (p.id == id || p.id == targetPassId) continue;
      bool rewired = false;
      for (auto& dep : p.dependencies) {
        if (dep == targetPassId) {
          dep = id;
          rewired = true;
        }
      }
      if (rewired) {
        p.dependencies.erase(std::unique(p.dependencies.begin(), p.dependencies.end()),
                             p.dependencies.end());
      }
    }
  }

  return id;
}

u32 RenderGraphBuilder::addPassPerJob(
    std::string_view baseName, PassType type, QueueType queue,
    const std::vector<RenderJob>& jobs,
    std::function<void(RenderPassNode&, const RenderJob&, u32 jobIndex)> setup) {
  u32 firstId = ~0u;
  for (u32 ji = 0; ji < static_cast<u32>(jobs.size()); ++ji) {
    const auto& job = jobs[ji];
    std::string passName = std::string(baseName) + "[" + job.name + "]";
    u32 id = addPass(passName, type, queue, nullptr);
    if (ji == 0) firstId = id;

    auto& pass = getPass(id);

    // Apply job's render target / viewport to the pass (only if job provides one;
    // otherwise the graph will auto-create from the pass's attachment declarations).
    if (job.renderPass != VK_NULL_HANDLE) {
      pass.useRenderPass(job.renderPass, job.framebuffer, job.renderArea, job.clearValues);
    } else if (job.renderArea.width > 0 && job.renderArea.height > 0) {
      pass.renderArea = job.renderArea;  // hint for auto-created render pass
    }
    if (job.hasCustomViewport) {
      pass.autoViewportScissor = false;
      pass.customViewport = job.viewport;
      pass.customScissor = job.scissor;
    } else {
      pass.autoViewportScissor = true;
    }

    if (setup) {
      setup(pass, job, ji);
    }
  }
  return firstId;
}

RenderPassNode& RenderGraphBuilder::getPass(u32 passId) {
  for (auto& p : passes) {
    if (p.id == passId) return p;
  }
  throw std::runtime_error("Pass not found: " + std::to_string(passId));
}

VirtualResource& RenderGraphBuilder::getResource(ResourceId resId) {
  for (auto& r : resources) {
    if (r.id == resId) return r;
  }
  throw std::runtime_error("Resource not found: " + std::to_string(resId));
}

void RenderGraphBuilder::addDependency(u32 fromPass, u32 toPass) {
  for (auto& p : passes) {
    if (p.id == toPass) {
      if (std::find(p.dependencies.begin(), p.dependencies.end(), fromPass)
          == p.dependencies.end()) {
        p.dependencies.push_back(fromPass);
      }
      return;
    }
  }
  throw std::runtime_error("Target pass not found: " + std::to_string(toPass));
}

void RenderGraphBuilder::removeDependency(u32 fromPass, u32 toPass) {
  for (auto& p : passes) {
    if (p.id == toPass) {
      auto it = std::remove(p.dependencies.begin(), p.dependencies.end(), fromPass);
      p.dependencies.erase(it, p.dependencies.end());
      return;
    }
  }
  throw std::runtime_error("Target pass not found: " + std::to_string(toPass));
}

void RenderGraphBuilder::clearDependencies(u32 passId) {
  for (auto& p : passes) {
    if (p.id == passId) {
      p.dependencies.clear();
      return;
    }
  }
  throw std::runtime_error("Pass not found: " + std::to_string(passId));
}

bool RenderGraphBuilder::hasPass(u32 passId) const {
  return std::any_of(passes.begin(), passes.end(),
                     [&](const RenderPassNode& p) { return p.id == passId; });
}

void RenderGraphBuilder::removePass(u32 passId, bool reconnectDependencies) {
  auto it = std::find_if(passes.begin(), passes.end(),
                         [&](const RenderPassNode& p) { return p.id == passId; });
  if (it == passes.end()) {
    throw std::runtime_error("Pass not found: " + std::to_string(passId));
  }

  const auto removedDeps = it->dependencies;

  for (auto& p : passes) {
    if (p.id == passId) continue;

    const bool dependedOnRemoved =
        std::find(p.dependencies.begin(), p.dependencies.end(), passId) != p.dependencies.end();
    if (!dependedOnRemoved) continue;

    p.dependencies.erase(std::remove(p.dependencies.begin(), p.dependencies.end(), passId),
                         p.dependencies.end());

    if (reconnectDependencies) {
      for (u32 dep : removedDeps) {
        if (dep == p.id) continue;
        if (std::find(p.dependencies.begin(), p.dependencies.end(), dep) == p.dependencies.end()) {
          p.dependencies.push_back(dep);
        }
      }
    }
  }

  passes.erase(it);
}

namespace {

inline u64 hashCombine(u64 h, u64 v) {
  return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2));
}

template <typename T>
inline u64 hashValue(T v) {
  return static_cast<u64>(v);
}

u64 computeGraphSignature(const std::vector<VirtualResource>& resources,
                         const std::vector<RenderPassNode>& passes) {
  u64 h = 1469598103934665603ull;
  h = hashCombine(h, hashValue(resources.size()));
  h = hashCombine(h, hashValue(passes.size()));

  for (const auto& res : resources) {
    h = hashCombine(h, hashValue(res.id));
    h = hashCombine(h, hashValue(static_cast<u8>(res.type)));
    if (res.type == transient_resource_type_e::buffer) {
      h = hashCombine(h, hashValue(res.bufferDesc.size));
      h = hashCombine(h, hashValue(static_cast<u32>(res.bufferDesc.usage)));
      h = hashCombine(h, hashValue(static_cast<u32>(res.bufferDesc.memoryProperties)));
    } else {
      h = hashCombine(h, hashValue(static_cast<u32>(res.imageDesc.format)));
      h = hashCombine(h, hashValue(static_cast<u32>(res.imageDesc.imageType)));
      h = hashCombine(h, hashValue(static_cast<u32>(res.imageDesc.samples)));
      h = hashCombine(h, hashValue(static_cast<u32>(res.imageDesc.usage)));
      h = hashCombine(h, hashValue(res.imageDesc.extent.width));
      h = hashCombine(h, hashValue(res.imageDesc.extent.height));
      h = hashCombine(h, hashValue(res.imageDesc.extent.depth));
      h = hashCombine(h, hashValue(res.imageDesc.mipLevels));
      h = hashCombine(h, hashValue(res.imageDesc.arrayLayers));
    }
  }

  for (const auto& pass : passes) {
    h = hashCombine(h, hashValue(pass.id));
    h = hashCombine(h, hashValue(static_cast<u8>(pass.type)));
    h = hashCombine(h, hashValue(static_cast<u8>(pass.queueType)));
    h = hashCombine(h, hashValue(pass.hasSideEffect ? 1u : 0u));

    h = hashCombine(h, hashValue(pass.dependencies.size()));
    for (u32 dep : pass.dependencies) h = hashCombine(h, hashValue(dep));

    auto hashRef = [&](const ResourceRef& ref) {
      h = hashCombine(h, hashValue(ref.resourceId));
      h = hashCombine(h, hashValue(static_cast<u8>(ref.access)));
      h = hashCombine(h, hashValue(static_cast<u64>(ref.accessInfo.stages)));
      h = hashCombine(h, hashValue(static_cast<u64>(ref.accessInfo.access)));
      h = hashCombine(h, hashValue(static_cast<u32>(ref.accessInfo.layout)));
    };

    h = hashCombine(h, hashValue(pass.reads.size()));
    for (const auto& r : pass.reads) hashRef(r);
    h = hashCombine(h, hashValue(pass.writes.size()));
    for (const auto& r : pass.writes) hashRef(r);
  }

  return h;
}

}  // namespace

RenderGraph RenderGraphBuilder::build(PassPipelineCache* pipelineCache,
                                      RenderGraphCompileCache* compileCache,
                                      bool forceRecompile) {
  RenderGraph graph;
  graph.ctx = &ctx;
  graph.resources = std::move(resources);
  graph.passes = std::move(passes);
  graph.importedBuffers = std::move(importedBuffers);
  graph.importedImages = std::move(importedImages);
  graph.importedVkBuffers = std::move(importedVkBuffers);
  graph.importedVkImages = std::move(importedVkImages);
  graph.importedVkImageViews = std::move(importedVkImageViews);

  RenderGraphCompiler compiler(ctx);
  compiler.cullUnusedPasses(graph.passes);

  RenderGraphCompiler::CompilationResult result;
  const u64 graphSignature = computeGraphSignature(graph.resources, graph.passes);
  const bool cacheHit =
      compileCache && !forceRecompile && compileCache->tryGet(graphSignature, result);
  if (!cacheHit) {
    result = compiler.compile(graph.resources, graph.passes);
    if (compileCache && result.success) {
      compileCache->store(graphSignature, result);
    }
  }

  if (!result.success) {
    throw std::runtime_error("Render graph compilation failed: " + result.error);
  }

  graph.compiledPasses = std::move(result.compiledPasses);
  graph.executionOrder = std::move(result.executionOrder);

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

  // ── Auto-create render passes & framebuffers for passes with attachment declarations ──
  for (auto& pass : graph.passes) {
    if (pass.attachments.empty() || pass.renderPass != VK_NULL_HANDLE) continue;

    // Build the VkRenderPass from attachment declarations
    auto rpBuilder = ctx.renderpass();
    std::vector<u32> colorRefs;
    int depthRef = -1;
    u32 attIdx = 0;

    for (const auto& att : pass.attachments) {
      const auto* res = graph.findResource(att.resourceId);
      if (!res) {
        throw std::runtime_error(
            "RenderGraph: attachment resource " + std::to_string(att.resourceId)
            + " not found in pass '" + pass.name + "'");
      }

      vk::Format format = res->imageDesc.format;
      vk::SampleCountFlagBits samples = res->imageDesc.samples;

      // Determine initial layout
      vk::ImageLayout initialLayout = vk::ImageLayout::eUndefined;
      if (att.loadOp == vk::AttachmentLoadOp::eLoad) {
        initialLayout = att.isDepthStencil
                            ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                            : vk::ImageLayout::eColorAttachmentOptimal;
      }

      // Determine final layout
      vk::ImageLayout finalLayout = att.finalLayout;
      if (finalLayout == vk::ImageLayout::eUndefined) {
        finalLayout = att.isDepthStencil
                          ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                          : vk::ImageLayout::eColorAttachmentOptimal;
      }

      AttachmentDesc desc{format, initialLayout, finalLayout};
      desc.category = att.isDepthStencil ? depth_stencil : color;
      desc.sampleBits = samples;
      desc.loadOp = att.loadOp;
      desc.storeOp = att.storeOp;
      rpBuilder.addAttachment(desc);

      if (att.isDepthStencil) {
        depthRef = static_cast<int>(attIdx);
      } else {
        colorRefs.push_back(attIdx);
      }
      ++attIdx;
    }

    rpBuilder.addSubpass(colorRefs, depthRef);
    graph.managedRenderPasses.push_back(rpBuilder.build());
    auto& rp = graph.managedRenderPasses.back();

    // Gather image views for framebuffer
    std::vector<vk::ImageView> views;
    vk::Extent2D fbExtent{};
    for (const auto& att : pass.attachments) {
      vk::ImageView view = graph.getImageView(att.resourceId);
      if (view == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "RenderGraph: no image view for resource " + std::to_string(att.resourceId)
            + " in pass '" + pass.name + "'");
      }
      views.push_back(view);

      // Use first attachment's extent as render area (if not already set by job)
      if (fbExtent.width == 0) {
        const auto* res = graph.findResource(att.resourceId);
        fbExtent.width = res->imageDesc.extent.width;
        fbExtent.height = res->imageDesc.extent.height;
      }
    }

    graph.managedFramebuffers.push_back(
        ctx.createFramebuffer(views, fbExtent, rp));

    // Fill in the pass fields
    pass.renderPass = rp;
    pass.framebuffer = graph.managedFramebuffers.back();
    if (pass.renderArea.width == 0 || pass.renderArea.height == 0) {
      pass.renderArea = fbExtent;
    }

    // Build clearValues from attachment declarations
    pass.clearValues.clear();
    for (const auto& att : pass.attachments) {
      pass.clearValues.push_back(att.clearValue);
    }
  }

  // ── Auto-create pipelines for passes with pipeline descriptions ──
  for (auto& pass : graph.passes) {
    if (!pass.pipelineDesc.isValid()) continue;
    if (pass.pipelineInfo.isValid()) continue;  // already set manually via usePipeline()

    // Gather descriptor set layouts from the pass's descriptor set declarations
    std::vector<vk::DescriptorSetLayout> setLayouts;
    for (const auto& ds : pass.descriptorSets) {
      setLayouts.push_back(ds.layout);
    }

    // ── Use pipeline cache when tag is set and cache is available ──
    if (pipelineCache && !pass.pipelineTag.empty()) {
      pass.pipelineInfo = pipelineCache->getOrBuild(
          pass.pipelineTag, pass.pipelineDesc,
          pass.renderPass, setLayouts,
          graph.managedPipelines);  // retired pipelines kept alive by graph
      continue;
    }

    // ── Uncached path: create pipeline per-frame ──
    if (pass.pipelineDesc.isGraphics()) {
      // Graphics pipeline: requires a render pass (auto-created or manual)
      if (pass.renderPass == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "RenderGraph: pass '" + pass.name
            + "' has a graphics pipeline desc but no render pass");
      }
      auto pipeline = ctx.createGraphicsPipeline(
          pass.pipelineDesc.graphicsDesc, pass.renderPass, setLayouts);
      pass.pipelineInfo.pipeline = *pipeline;
      pass.pipelineInfo.layout = static_cast<vk::PipelineLayout>(pipeline);
      pass.pipelineInfo.bindPoint = vk::PipelineBindPoint::eGraphics;
      graph.managedPipelines.push_back(
          zs::make_unique<Pipeline>(std::move(pipeline)));

    } else if (pass.pipelineDesc.isCompute()) {
      // Compute pipeline: create layout + shader module + pipeline
      const auto& shaderDesc = pass.pipelineDesc.computeShader;
      if (shaderDesc.spirv.empty()) {
        throw std::runtime_error(
            "RenderGraph: pass '" + pass.name
            + "' has a compute pipeline desc but shader SPIR-V is empty");
      }

      // Create temporary shader module from SPIR-V
      auto shaderModule = ctx.createShaderModule(
          shaderDesc.spirv.data(), shaderDesc.spirv.size(), shaderDesc.stage);

      // Create pipeline layout
      VulkanContext::PipelineLayoutDesc layoutDesc;
      layoutDesc.pushConstantRanges = pass.pipelineDesc.computePushConstantRanges;
      auto pipelineLayout = ctx.createPipelineLayout(layoutDesc, setLayouts);

      // Create compute pipeline (takes ownership of layout)
      VulkanContext::ComputePipelineDesc cpDesc;
      cpDesc.shader = &shaderModule;
      cpDesc.pipelineLayout = pipelineLayout;
      auto pipeline = ctx.createComputePipeline(cpDesc);
      // Temp shaderModule destroyed here (its VkShaderModule handle is baked
      // into the VkPipeline and no longer needed after creation).

      pass.pipelineInfo.pipeline = *pipeline;
      pass.pipelineInfo.layout = static_cast<vk::PipelineLayout>(pipeline);
      pass.pipelineInfo.bindPoint = vk::PipelineBindPoint::eCompute;
      graph.managedPipelines.push_back(
          zs::make_unique<Pipeline>(std::move(pipeline)));
    }
  }

  graph.compiled = true;
  return graph;
}

// ============================================================================
// RenderGraphCompiler Implementation
// ============================================================================

std::vector<u32> RenderGraphCompiler::topologicalSort(const std::vector<RenderPassNode>& passes) {
  std::unordered_map<u32, u32> inDegree;
  std::unordered_map<u32, std::vector<u32>> adjList;
  std::unordered_map<u32, u32> passIdToIndex;

  for (u32 i = 0; i < passes.size(); ++i) {
    passIdToIndex[passes[i].id] = i;
    inDegree[passes[i].id] = 0;
  }

  for (const auto& pass : passes) {
    for (u32 dep : pass.dependencies) {
      adjList[dep].push_back(pass.id);
      inDegree[pass.id]++;
    }
  }

  std::queue<u32> queue;
  for (const auto& [id, degree] : inDegree) {
    if (degree == 0) {
      queue.push(id);
    }
  }

  std::vector<u32> result;
  while (!queue.empty()) {
    u32 id = queue.front();
    queue.pop();
    result.push_back(passIdToIndex[id]);

    for (u32 neighbor : adjList[id]) {
      if (--inDegree[neighbor] == 0) {
        queue.push(neighbor);
      }
    }
  }

  if (result.size() != passes.size()) {
    return {};
  }

  return result;
}

void RenderGraphCompiler::computeBarriers(const std::vector<VirtualResource>& resources,
                                          const std::vector<RenderPassNode>& passes,
                                          const std::vector<u32>& executionOrder,
                                          std::vector<CompiledPass>& outCompiled) {
  std::unordered_map<ResourceId, ResourceAccessInfo> lastAccess;

  outCompiled.resize(passes.size());

  for (u32 orderIdx : executionOrder) {
    const auto& pass = passes[orderIdx];
    auto& compiled = outCompiled[orderIdx];
    compiled.passId = pass.id;

    for (const auto& ref : pass.reads) {
      BarrierInfo barrier;
      barrier.resourceId = ref.resourceId;
      barrier.dstAccess = ref.accessInfo;

      auto it = lastAccess.find(ref.resourceId);
      if (it != lastAccess.end()) {
        barrier.srcAccess = it->second;
      } else {
        for (const auto& res : resources) {
          if (res.id == ref.resourceId) {
            barrier.srcAccess = res.firstAccess;
            break;
          }
        }
      }

      if (barrier.srcAccess.stages && barrier.dstAccess.stages) {
        compiled.preBarriers.push_back(barrier);
      }

      lastAccess[ref.resourceId] = ref.accessInfo;
    }

    for (const auto& ref : pass.writes) {
      BarrierInfo barrier;
      barrier.resourceId = ref.resourceId;
      barrier.dstAccess = ref.accessInfo;

      auto it = lastAccess.find(ref.resourceId);
      if (it != lastAccess.end()) {
        barrier.srcAccess = it->second;
      } else {
        for (const auto& res : resources) {
          if (res.id == ref.resourceId) {
            barrier.srcAccess = res.firstAccess;
            break;
          }
        }
      }

      if (barrier.srcAccess.stages && barrier.dstAccess.stages) {
        compiled.preBarriers.push_back(barrier);
      }

      lastAccess[ref.resourceId] = ref.accessInfo;
    }

    compiled.needsRenderPass = (pass.type == PassType::Graphics);
  }
}

void RenderGraphCompiler::cullUnusedPasses(std::vector<RenderPassNode>& passes) {
  std::unordered_set<ResourceId> writtenResources;
  std::unordered_set<ResourceId> readResources;

  for (const auto& pass : passes) {
    for (const auto& ref : pass.writes) {
      writtenResources.insert(ref.resourceId);
    }
    for (const auto& ref : pass.reads) {
      readResources.insert(ref.resourceId);
    }
  }

  std::vector<ResourceId> outputResources;
  for (ResourceId res : writtenResources) {
    if (readResources.find(res) == readResources.end()) {
      outputResources.push_back(res);
    }
  }

  std::unordered_set<u32> neededPasses;
  std::queue<ResourceId> workQueue;

  for (ResourceId res : outputResources) {
    workQueue.push(res);
  }

  while (!workQueue.empty()) {
    ResourceId res = workQueue.front();
    workQueue.pop();

    for (const auto& pass : passes) {
      if (neededPasses.count(pass.id)) continue;

      for (const auto& ref : pass.writes) {
        if (ref.resourceId == res) {
          neededPasses.insert(pass.id);
          for (const auto& r : pass.reads) {
            workQueue.push(r.resourceId);
          }
          break;
        }
      }
    }
  }

  for (auto& pass : passes) {
    if (pass.hasSideEffect) {
      neededPasses.insert(pass.id);
    }
  }

  // Remove passes that are not needed
  passes.erase(std::remove_if(passes.begin(), passes.end(),
      [&](const RenderPassNode& p) { return neededPasses.count(p.id) == 0; }),
      passes.end());
}

RenderGraphCompiler::CompilationResult
RenderGraphCompiler::compile(const std::vector<VirtualResource>& resources,
                             const std::vector<RenderPassNode>& passes) {
  CompilationResult result;

  if (passes.empty()) {
    result.success = true;
    return result;
  }

  result.executionOrder = topologicalSort(passes);
  if (result.executionOrder.empty()) {
    result.success = false;
    result.error = "Cyclic dependency detected in render graph";
    return result;
  }

  computeBarriers(resources, passes, result.executionOrder, result.compiledPasses);

  result.success = true;
  return result;
}

// ============================================================================
// RenderGraphExecutor Implementation
// ============================================================================

void RenderGraphExecutor::injectBarriers(vk::CommandBuffer cmd,
                                         const std::vector<BarrierInfo>& barriers,
                                         const std::vector<VirtualResource>& resources,
                                         RenderGraph& graph) {
  if (barriers.empty()) return;

  std::vector<vk::MemoryBarrier2> memoryBarriers;
  std::vector<vk::BufferMemoryBarrier2> bufferBarriers;
  std::vector<vk::ImageMemoryBarrier2> imageBarriers;

  for (const auto& barrier : barriers) {
    if (!barrier.isValid()) continue;

    const auto* resPtr = [&]() -> const VirtualResource* {
      for (const auto& r : resources) {
        if (r.id == barrier.resourceId) return &r;
      }
      return nullptr;
    }();
    if (!resPtr) continue;
    const auto& res = *resPtr;

    if (res.type == transient_resource_type_e::buffer) {
      Buffer* buffer = graph.getBuffer(barrier.resourceId);
      vk::Buffer bufferHandle = VK_NULL_HANDLE;
      if (buffer) {
        bufferHandle = static_cast<vk::Buffer>(*buffer);
      } else {
        bufferHandle = graph.getExternalBuffer(barrier.resourceId);
      }
      if (bufferHandle == VK_NULL_HANDLE) continue;
      bufferBarriers.push_back(vk::BufferMemoryBarrier2{}
                                   .setSrcStageMask(barrier.srcAccess.stages)
                                   .setSrcAccessMask(barrier.srcAccess.access)
                                   .setDstStageMask(barrier.dstAccess.stages)
                                   .setDstAccessMask(barrier.dstAccess.access)
                                   .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                   .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                   .setBuffer(bufferHandle)
                                   .setOffset(0)
                                   .setSize(VK_WHOLE_SIZE));
    } else {
      Image* image = graph.getImage(barrier.resourceId);
      vk::Image imageHandle = VK_NULL_HANDLE;
      if (image) {
        imageHandle = static_cast<vk::Image>(*image);
      } else {
        imageHandle = graph.getExternalImage(barrier.resourceId);
      }
      if (imageHandle == VK_NULL_HANDLE) continue;
      auto aspect = res.imageDesc.format == vk::Format::eUndefined
                        ? vk::ImageAspectFlagBits::eColor
                        : deduce_image_format_aspect_flag(res.imageDesc.format);
      imageBarriers.push_back(vk::ImageMemoryBarrier2{}
                                  .setSrcStageMask(barrier.srcAccess.stages)
                                  .setSrcAccessMask(barrier.srcAccess.access)
                                  .setDstStageMask(barrier.dstAccess.stages)
                                  .setDstAccessMask(barrier.dstAccess.access)
                                  .setOldLayout(barrier.srcAccess.layout)
                                  .setNewLayout(barrier.dstAccess.layout)
                                  .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                  .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
                                  .setImage(imageHandle)
                                  .setSubresourceRange(vk::ImageSubresourceRange{
                                      aspect, 0, VK_REMAINING_MIP_LEVELS, 0,
                                      VK_REMAINING_ARRAY_LAYERS}));
    }
  }

  if (memoryBarriers.empty() && bufferBarriers.empty() && imageBarriers.empty()) return;

  vk::DependencyInfo depInfo{};
  depInfo.setMemoryBarrierCount(static_cast<u32>(memoryBarriers.size()))
      .setPMemoryBarriers(memoryBarriers.data())
      .setBufferMemoryBarrierCount(static_cast<u32>(bufferBarriers.size()))
      .setPBufferMemoryBarriers(bufferBarriers.data())
      .setImageMemoryBarrierCount(static_cast<u32>(imageBarriers.size()))
      .setPImageMemoryBarriers(imageBarriers.data());

  cmd.pipelineBarrier2(depInfo, graph.ctx->dispatcher);
}

void RenderGraphExecutor::recordPass(vk::CommandBuffer cmd, const RenderPassNode& pass,
                                     const CompiledPass& compiled) {
  // 1. Bind pipeline (if declared)
  if (pass.pipelineInfo.isValid()) {
    cmd.bindPipeline(pass.pipelineInfo.bindPoint, pass.pipelineInfo.pipeline, ctx.dispatcher);
  }

  // 2. Begin render pass (if declared on the pass node)
  bool beganRenderPass = false;
  if (pass.renderPass != VK_NULL_HANDLE && pass.framebuffer != VK_NULL_HANDLE) {
    vk::RenderPassBeginInfo rpBegin{};
    rpBegin.setRenderPass(pass.renderPass)
        .setFramebuffer(pass.framebuffer)
        .setRenderArea(vk::Rect2D{{0, 0}, pass.renderArea})
        .setClearValueCount(static_cast<u32>(pass.clearValues.size()))
        .setPClearValues(pass.clearValues.data());
    cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline, ctx.dispatcher);
    beganRenderPass = true;
  }

  // 3. Viewport / scissor (custom takes priority over auto)
  if (pass.customViewport.width > 0 && pass.customViewport.height > 0) {
    cmd.setViewport(0, {pass.customViewport}, ctx.dispatcher);
    cmd.setScissor(0, {pass.customScissor}, ctx.dispatcher);
  } else if (pass.autoViewportScissor && pass.renderArea.width > 0 && pass.renderArea.height > 0) {
    vk::Viewport vp{0.f, 0.f,
                     static_cast<float>(pass.renderArea.width),
                     static_cast<float>(pass.renderArea.height),
                     0.f, 1.f};
    vk::Rect2D sc{{0, 0}, pass.renderArea};
    cmd.setViewport(0, {vp}, ctx.dispatcher);
    cmd.setScissor(0, {sc}, ctx.dispatcher);
  }

  // 4. Allocate, write, and bind descriptor sets
  for (u32 setIdx = 0; setIdx < static_cast<u32>(pass.descriptorSets.size()); ++setIdx) {
    const auto& setInfo = pass.descriptorSets[setIdx];
    if (setInfo.layout == VK_NULL_HANDLE || setInfo.writes.empty()) continue;

    vk::DescriptorSet ds;
    if (executionPool != VK_NULL_HANDLE) {
      ds = ctx.device.allocateDescriptorSets(
          vk::DescriptorSetAllocateInfo{}
              .setDescriptorPool(executionPool)
              .setPSetLayouts(&setInfo.layout)
              .setDescriptorSetCount(1),
          ctx.dispatcher)[0];
    } else {
      ctx.acquireSet(setInfo.layout, ds);
    }

    for (const auto& w : setInfo.writes) {
      if (w.isImage()) {
        ctx.writeDescriptorSet(w.imageInfo, ds, w.type, w.binding, w.arrayElement);
      } else {
        ctx.writeDescriptorSet(w.bufferInfo, ds, w.type, w.binding, w.arrayElement);
      }
    }

    if (pass.pipelineInfo.hasLayout()) {
      cmd.bindDescriptorSets(pass.pipelineInfo.bindPoint, pass.pipelineInfo.layout, setIdx,
                             {ds}, {}, ctx.dispatcher);
    }
  }

  // 5. Push constants
  for (const auto& pc : pass.pushConstants) {
    if (pass.pipelineInfo.hasLayout() && pc.size > 0) {
      cmd.pushConstants(pass.pipelineInfo.layout, pc.stages, pc.offset, pc.size,
                        pc.data.data(), ctx.dispatcher);
    }
  }

  // 6. User-defined recording (draw calls, dispatches, etc.)
  if (pass.executeFunc) {
    pass.executeFunc(cmd, const_cast<RenderPassNode&>(pass));
  }

  // 7. End render pass (if we began one)
  if (beganRenderPass) {
    cmd.endRenderPass(ctx.dispatcher);
  }
}

void RenderGraphExecutor::execute(const RenderGraph& graph, vk::Queue queue, vk::Fence fence) {
  if (!graph.isValid()) {
    throw std::runtime_error("Cannot execute invalid render graph");
  }

  executionPool = graph.externalDescriptorPool;

  auto& env = graph.ctx->env();
  auto& pool = env.pools(vk_queue_e::graphics);
  vk::CommandBuffer cmd = pool.createCommandBuffer(vk::CommandBufferLevel::ePrimary, true,
                                                   nullptr, vk_cmd_usage_e::single_use);

  for (u32 orderIdx : graph.executionOrder) {
    const auto& pass = graph.passes[orderIdx];
    const auto& compiled = graph.compiledPasses[orderIdx];

    injectBarriers(cmd, compiled.preBarriers, graph.resources, const_cast<RenderGraph&>(graph));
    recordPass(cmd, pass, compiled);
    injectBarriers(cmd, compiled.postBarriers, graph.resources, const_cast<RenderGraph&>(graph));
  }

  cmd.end(graph.ctx->dispatcher);

  vk::SubmitInfo submitInfo{};
  submitInfo.setCommandBufferCount(1).setPCommandBuffers(&cmd);

  if (auto res = queue.submit(1, &submitInfo, fence, graph.ctx->dispatcher);
      res != vk::Result::eSuccess) {
    throw std::runtime_error("Failed to submit render graph command buffer");
  }
  // NOTE: command buffer is NOT freed here — it may still be pending on GPU.
  // The singleUsePool will reclaim it on pool reset / destroy.
}

void RenderGraphExecutor::executeWithSync(const RenderGraph& graph, vk::Queue queue,
                                          const std::vector<vk::Semaphore>& waitSemaphores,
                                          const std::vector<vk::PipelineStageFlags>& waitStages,
                                          const std::vector<vk::Semaphore>& signalSemaphores,
                                          vk::Fence fence) {
  if (!graph.isValid()) {
    throw std::runtime_error("Cannot execute invalid render graph");
  }

  executionPool = graph.externalDescriptorPool;

  auto& env = graph.ctx->env();
  auto& pool = env.pools(vk_queue_e::graphics);
  vk::CommandBuffer cmd = pool.createCommandBuffer(vk::CommandBufferLevel::ePrimary, true,
                                                   nullptr, vk_cmd_usage_e::single_use);

  for (u32 orderIdx : graph.executionOrder) {
    const auto& pass = graph.passes[orderIdx];
    const auto& compiled = graph.compiledPasses[orderIdx];

    injectBarriers(cmd, compiled.preBarriers, graph.resources, const_cast<RenderGraph&>(graph));
    recordPass(cmd, pass, compiled);
    injectBarriers(cmd, compiled.postBarriers, graph.resources, const_cast<RenderGraph&>(graph));
  }

  cmd.end(graph.ctx->dispatcher);

  vk::SubmitInfo submitInfo{};
  submitInfo.setCommandBufferCount(1).setPCommandBuffers(&cmd);
  if (!waitSemaphores.empty()) {
    submitInfo.setWaitSemaphoreCount(static_cast<u32>(waitSemaphores.size()))
        .setPWaitSemaphores(waitSemaphores.data())
        .setPWaitDstStageMask(waitStages.data());
  }
  if (!signalSemaphores.empty()) {
    submitInfo.setSignalSemaphoreCount(static_cast<u32>(signalSemaphores.size()))
        .setPSignalSemaphores(signalSemaphores.data());
  }

  if (auto res = queue.submit(1, &submitInfo, fence, graph.ctx->dispatcher);
      res != vk::Result::eSuccess) {
    throw std::runtime_error("Failed to submit render graph command buffer");
  }
  // NOTE: command buffer is NOT freed here — it may still be pending on GPU.
  // The singleUsePool will reclaim it on pool reset / destroy.
}

// ============================================================================
// RenderGraph Implementation
// ============================================================================

void RenderGraph::execute(vk::Queue queue, vk::Fence fence) {
  RenderGraphExecutor executor(*ctx);
  executor.execute(*this, queue, fence);
}

void RenderGraph::executeWithSync(vk::Queue queue,
                                  const std::vector<vk::Semaphore>& waitSemaphores,
                                  const std::vector<vk::PipelineStageFlags>& waitStages,
                                  const std::vector<vk::Semaphore>& signalSemaphores,
                                  vk::Fence fence) {
  RenderGraphExecutor executor(*ctx);
  executor.executeWithSync(*this, queue, waitSemaphores, waitStages, signalSemaphores, fence);
}

Buffer* RenderGraph::getBuffer(ResourceId id) {
  for (const auto& [rid, buf] : importedBuffers) {
    if (rid == id) return buf;
  }
  auto it = allocatedBuffers.find(id);
  if (it != allocatedBuffers.end()) return &it->second.get();
  return nullptr;
}

Image* RenderGraph::getImage(ResourceId id) {
  for (const auto& [rid, img] : importedImages) {
    if (rid == id) return img;
  }
  auto ext = importedVkImages.find(id);
  if (ext != importedVkImages.end()) return nullptr;
  auto it = allocatedImages.find(id);
  if (it != allocatedImages.end()) return &it->second.get();
  return nullptr;
}

vk::Buffer RenderGraph::getExternalBuffer(ResourceId id) const {
  auto it = importedVkBuffers.find(id);
  if (it != importedVkBuffers.end()) return it->second;
  return VK_NULL_HANDLE;
}

vk::Image RenderGraph::getExternalImage(ResourceId id) const {
  auto it = importedVkImages.find(id);
  if (it != importedVkImages.end()) return it->second;
  return VK_NULL_HANDLE;
}

vk::ImageView RenderGraph::getImageView(ResourceId id) const {
  // 1. Imported Image* (RAII Image has operator vk::ImageView via pview)
  for (const auto& [rid, img] : importedImages) {
    if (rid == id) return static_cast<vk::ImageView>(*img);
  }
  // 2. Imported vk::Image + explicit view
  auto vit = importedVkImageViews.find(id);
  if (vit != importedVkImageViews.end()) return vit->second;
  // 3. Allocated (transient) images
  auto ait = allocatedImages.find(id);
  if (ait != allocatedImages.end()) return static_cast<vk::ImageView>(ait->second.get());
  return VK_NULL_HANDLE;
}

const VirtualResource* RenderGraph::findResource(ResourceId id) const {
  for (const auto& r : resources) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

}  // namespace zs
