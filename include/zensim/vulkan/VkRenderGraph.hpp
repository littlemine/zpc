#pragma once
/**
 * @file VkRenderGraph.hpp
 * @brief Vulkan Render Graph — core types and interfaces.
 *
 * Provides a declarative, DAG-based render graph for Vulkan with:
 *  - Virtual resource handles (buffer / image)
 *  - Pass nodes with typed resource references
 *  - Automatic barrier injection (layout transitions, memory barriers)
 *  - Self-contained descriptor set management (per-pass declaration + auto-bind)
 *  - Compilation (topological sort, hazard analysis)
 *  - Single-queue execution with optional wait/signal semaphores
 *
 * Integration: compiled as part of zpccore when ZS_ENABLE_VULKAN=1.
 */

#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "zensim/ZpcImplPattern.hpp"
#include "zensim/vulkan/VkContext.hpp"
#include "zensim/vulkan/VkPipeline.hpp"
#include "zensim/vulkan/VkRenderPass.hpp"
#include "zensim/vulkan/VkTexture.hpp"
#include "zensim/vulkan/VkTransientResource.hpp"
#include "zensim/vulkan/VkUtils.hpp"

namespace zs {

// ============================================================================
// Forward Declarations
// ============================================================================

struct RenderGraph;
struct RenderPassNode;
struct RenderGraphBuilder;
struct RenderGraphCompiler;
struct RenderGraphExecutor;
struct PassPipelineCache;
struct RenderGraphCompileCache;

// ============================================================================
// Resource Access Patterns
// ============================================================================

enum class ResourceAccess : u8 {
  None = 0,
  Read = 1 << 0,
  Write = 1 << 1,
  ReadWrite = Read | Write
};

inline ResourceAccess operator|(ResourceAccess a, ResourceAccess b) {
  return static_cast<ResourceAccess>(static_cast<u8>(a) | static_cast<u8>(b));
}
inline ResourceAccess operator&(ResourceAccess a, ResourceAccess b) {
  return static_cast<ResourceAccess>(static_cast<u8>(a) & static_cast<u8>(b));
}
inline ResourceAccess& operator|=(ResourceAccess& a, ResourceAccess b) {
  a = a | b;
  return a;
}
inline ResourceAccess& operator&=(ResourceAccess& a, ResourceAccess b) {
  a = a & b;
  return a;
}
inline bool any(ResourceAccess a) {
  return static_cast<u8>(a) != 0;
}

enum class PassType : u8 {
  Compute,
  Graphics,
  Transfer,
  Present,
  AsyncCompute,
  Copy,
  Blit,
  Clear,
  RayTracing
};

enum class QueueType : u8 {
  Graphics = 0,
  Compute,
  Transfer,
  Count
};

// ============================================================================
// Descriptor Set Binding Declarations (per-pass)
// ============================================================================

/// @brief A single descriptor write: either an image or a buffer binding.
struct PassDescriptorWrite {
  u32 binding{0};              ///< Binding index within the set
  u32 arrayElement{0};         ///< Array element for array descriptors (bindless)
  vk::DescriptorType type{vk::DescriptorType::eCombinedImageSampler};

  /// Payload — exactly one should be filled:
  vk::DescriptorImageInfo imageInfo{};
  vk::DescriptorBufferInfo bufferInfo{};

  bool isImage() const {
    return type == vk::DescriptorType::eCombinedImageSampler
        || type == vk::DescriptorType::eSampledImage
        || type == vk::DescriptorType::eStorageImage
        || type == vk::DescriptorType::eInputAttachment
        || type == vk::DescriptorType::eSampler;
  }
};

/// @brief Describes one descriptor set for a pass: a layout + a list of writes.
struct PassDescriptorSetInfo {
  vk::DescriptorSetLayout layout{VK_NULL_HANDLE};  ///< Set layout to allocate from
  std::vector<PassDescriptorWrite> writes;           ///< Descriptor writes for this set
};

/// @brief Push-constant data attached to a pass.
struct PassPushConstantInfo {
  vk::ShaderStageFlags stages{};
  u32 offset{0};
  u32 size{0};
  std::vector<u8> data;  ///< Inline push-constant bytes
};

/// @brief Pipeline binding info for a render graph pass.
///
/// Groups the Vulkan pipeline handle, its layout, and bind point together
/// so that the executor can bind the pipeline and use the layout for
/// descriptor set binding and push-constant commands.
struct PassPipelineInfo {
  vk::Pipeline pipeline{VK_NULL_HANDLE};
  vk::PipelineLayout layout{VK_NULL_HANDLE};
  vk::PipelineBindPoint bindPoint{vk::PipelineBindPoint::eGraphics};

  bool isValid() const { return pipeline != VK_NULL_HANDLE; }
  bool hasLayout() const { return layout != VK_NULL_HANDLE; }
};

/// @brief Declarative pipeline description for a render graph pass.
///
/// When set on a RenderPassNode, the render graph creates the Vulkan pipeline
/// during build().  For graphics pipelines the shaderStages in the
/// GraphicsPipelineDesc must contain compiled SPIR-V (e.g. obtained from
/// ShaderManager::getStageDesc()).  Descriptor set layouts are automatically
/// derived from the pass's descriptor set declarations.
struct PassPipelineDesc {
  enum class Type : u8 { None, Graphics, Compute };
  Type type{Type::None};

  /// Graphics pipeline state (used when type == Graphics).
  /// shaderStages must contain compiled SPIR-V.
  GraphicsPipelineDesc graphicsDesc;

  /// Compute shader stage desc (used when type == Compute).
  /// Must contain compiled SPIR-V.
  ShaderStageDesc computeShader;

  /// Push constant ranges for compute pipelines.
  /// (For graphics pipelines, use graphicsDesc.pushConstantRanges.)
  std::vector<vk::PushConstantRange> computePushConstantRanges;

  bool isValid() const { return type != Type::None; }
  bool isGraphics() const { return type == Type::Graphics; }
  bool isCompute() const { return type == Type::Compute; }
};

// ============================================================================
// Pipeline Cache (persists across frames, dirty-based rebuild)
// ============================================================================

/// @brief Caches Vulkan pipelines by string tag with a dirty-flag mechanism.
///
/// Pipelines are expensive to create.  `PassPipelineCache` stores built
/// pipelines across frames so that only pipelines whose description has
/// changed (marked dirty) are rebuilt.  When a pipeline is rebuilt, the
/// old one is moved to a *retired* list returned to the caller so its
/// lifetime can be tied to the in-flight `RenderGraph`.
///
/// Usage:
///   - Create one `PassPipelineCache` per `VulkanContext` (lives across frames).
///   - In per-pass setup, call `setGraphicsPipelineDesc(tag, desc)` or
///     `setComputePipelineDesc(tag, shader, pushConstants)`.
///   - Pass the cache to `RenderGraphBuilder::build(pipelineCache)`.
///   - When the user changes a display option, call
///     `cache.markDirty(tag)` to trigger a rebuild on the next build.
struct ZPC_CORE_API PassPipelineCache {
  explicit PassPipelineCache(VulkanContext& ctx) : _ctx{ctx} {}
  ~PassPipelineCache() = default;

  // Non-copyable, movable
  PassPipelineCache(const PassPipelineCache&) = delete;
  PassPipelineCache& operator=(const PassPipelineCache&) = delete;
  PassPipelineCache(PassPipelineCache&&) = default;
  PassPipelineCache& operator=(PassPipelineCache&&) = default;

  /// @brief Mark a pipeline for rebuild on the next build().
  void markDirty(const std::string& tag);

  /// @brief Mark all cached pipelines for rebuild.
  void markAllDirty();

  /// @brief Check whether a tag needs (re)building.
  bool needsRebuild(const std::string& tag) const;

  /// @brief Get an existing pipeline or build from the description.
  ///
  /// @param tag          Unique pipeline identifier.
  /// @param desc         The declarative pipeline description.
  /// @param renderPass   Compatible VkRenderPass (graphics only).
  /// @param setLayouts   Descriptor set layouts for pipeline layout creation.
  /// @param retiredPipelines  Receives the old pipeline when a dirty rebuild
  ///                          replaces it.  The caller (RenderGraph) keeps
  ///                          the old pipeline alive until the GPU fence is
  ///                          waited on.
  /// @return Pipeline binding info (pipeline + layout + bind point).
  PassPipelineInfo getOrBuild(
      const std::string& tag,
      const PassPipelineDesc& desc,
      vk::RenderPass renderPass,
      const std::vector<vk::DescriptorSetLayout>& setLayouts,
      std::vector<std::unique_ptr<Pipeline>>& retiredPipelines);

  /// @brief Remove a cached pipeline.  The caller must ensure the GPU is done.
  void erase(const std::string& tag);

  /// @brief Destroy all cached pipelines.  Call only when GPU is idle.
  void clear();

  /// @brief Number of cached entries.
  size_t size() const noexcept { return _entries.size(); }

private:
  VulkanContext& _ctx;

  struct CachedEntry {
    std::unique_ptr<Pipeline> pipeline;
    bool dirty{false};
  };
  std::map<std::string, CachedEntry> _entries;
};

// ============================================================================
// Resource Identifier (used everywhere below)
// ============================================================================

using ResourceId = u32;
static constexpr ResourceId InvalidResourceId = ~0u;

// ============================================================================
// Render Pass Attachment Declaration (per-pass)
// ============================================================================

/// @brief Describes one render pass attachment declared by a pass.
///
/// When a pass declares attachments, the render graph automatically creates
/// a compatible VkRenderPass and VkFramebuffer during build().
struct RenderPassAttachment {
  ResourceId resourceId{InvalidResourceId};     ///< Image resource used as attachment
  bool isDepthStencil{false};                    ///< Color vs depth/stencil attachment
  vk::AttachmentLoadOp loadOp{vk::AttachmentLoadOp::eClear};
  vk::AttachmentStoreOp storeOp{vk::AttachmentStoreOp::eStore};
  vk::ClearValue clearValue{};                   ///< Used when loadOp == eClear
  /// Final layout override. eUndefined = auto-deduce based on attachment type.
  /// Color default: eColorAttachmentOptimal  Depth default: eDepthStencilAttachmentOptimal
  vk::ImageLayout finalLayout{vk::ImageLayout::eUndefined};
};

// ============================================================================
// Render Job  (per-view / per-camera / per-viewport)
// ============================================================================

/// @brief Encapsulates the per-view state for one camera / viewport.
///
/// A single compiled render graph can be executed for multiple RenderJobs.
/// Each job supplies its own target framebuffer, render area, clear values,
/// and camera data (via push-constant or UBO helpers).
struct RenderJob {
  std::string name;

  // ── Target ─────────────────────────────────────────────────────
  vk::RenderPass renderPass{VK_NULL_HANDLE};
  vk::Framebuffer framebuffer{VK_NULL_HANDLE};
  vk::Extent2D renderArea{};
  std::vector<vk::ClearValue> clearValues;

  // ── Viewport (auto-derived from renderArea when hasCustomViewport == false) ─
  bool hasCustomViewport{false};
  vk::Viewport viewport{};
  vk::Rect2D scissor{};

  // ── Camera matrices (column-major, suitable for push-constant upload) ──
  float view[16]{};
  float projection[16]{};

  // ── Convenience factory ────────────────────────────────────────
  static RenderJob make(std::string_view name, vk::RenderPass rp, vk::Framebuffer fb,
                        vk::Extent2D area, std::vector<vk::ClearValue> clears = {}) {
    RenderJob j;
    j.name = name;
    j.renderPass = rp;
    j.framebuffer = fb;
    j.renderArea = area;
    j.clearValues = std::move(clears);
    return j;
  }
};

// ============================================================================
// Pipeline Stage and Access Tracking
// ============================================================================

struct ResourceAccessInfo {
  vk::PipelineStageFlags2 stages{};
  vk::AccessFlags2 access{};
  vk::ImageLayout layout{vk::ImageLayout::eUndefined};

  ResourceAccessInfo() = default;
  ResourceAccessInfo(vk::PipelineStageFlags2 s, vk::AccessFlags2 a,
                     vk::ImageLayout l = vk::ImageLayout::eUndefined)
      : stages{s}, access{a}, layout{l} {}

  bool operator==(const ResourceAccessInfo& o) const {
    return stages == o.stages && access == o.access && layout == o.layout;
  }
  bool operator!=(const ResourceAccessInfo& o) const { return !(*this == o); }
};

// ============================================================================
// Virtual Resource Handle
// ============================================================================

struct VirtualResource {
  ResourceId id{InvalidResourceId};
  std::string name;
  transient_resource_type_e type{transient_resource_type_e::buffer};

  union {
    TransientBufferDesc bufferDesc{};
    TransientImageDesc imageDesc;
  };

  ResourceAccessInfo lastAccess;
  ResourceAccessInfo firstAccess;

  VirtualResource() = default;
  VirtualResource(ResourceId id, std::string_view name, transient_resource_type_e type)
      : id{id}, name{name}, type{type} {}

  static VirtualResource makeBuffer(ResourceId id, std::string_view name,
                                    const TransientBufferDesc& desc) {
    VirtualResource r{id, name, transient_resource_type_e::buffer};
    r.bufferDesc = desc;
    return r;
  }

  static VirtualResource makeImage(ResourceId id, std::string_view name,
                                   const TransientImageDesc& desc) {
    VirtualResource r{id, name, transient_resource_type_e::image};
    r.imageDesc = desc;
    return r;
  }
};

// ============================================================================
// Resource Reference (used in passes)
// ============================================================================

struct ResourceRef {
  ResourceId resourceId{InvalidResourceId};
  ResourceAccess access{ResourceAccess::None};
  ResourceAccessInfo accessInfo;

  bool isValid() const { return resourceId != InvalidResourceId; }
  bool isRead() const { return (access & ResourceAccess::Read) != ResourceAccess::None; }
  bool isWrite() const { return (access & ResourceAccess::Write) != ResourceAccess::None; }
};

// ============================================================================
// Pass Node
// ============================================================================

using PassExecuteFunc = std::function<void(vk::CommandBuffer, RenderPassNode&)>;

struct RenderPassNode {
  u32 id{~0u};
  std::string name;
  PassType type{PassType::Compute};
  QueueType queueType{QueueType::Graphics};

  std::vector<ResourceRef> reads;
  std::vector<ResourceRef> writes;

  PassExecuteFunc executeFunc;

  std::vector<u32> dependencies;

  bool hasSideEffect{false};

  // ── Pipeline binding (optional — enables auto-bind) ────────────
  PassPipelineInfo pipelineInfo;

  // ── Pipeline description (declarative — graph auto-builds) ─────
  /// If set, the render graph creates and manages the Vulkan pipeline
  /// during build().  Overrides usePipeline() when both are set.
  PassPipelineDesc pipelineDesc;

  /// Optional tag for PassPipelineCache lookup / dirty tracking.
  /// When non-empty and a PassPipelineCache is provided to build(),
  /// the cache is consulted instead of creating a new pipeline every frame.
  std::string pipelineTag;

  // ── Render pass / framebuffer for graphics passes ──────────────
  vk::RenderPass renderPass{VK_NULL_HANDLE};
  vk::Framebuffer framebuffer{VK_NULL_HANDLE};
  vk::Extent2D renderArea{};
  std::vector<vk::ClearValue> clearValues;

  // ── Render pass attachment declarations (auto-create render pass) ──
  /// If non-empty, the render graph creates a VkRenderPass and VkFramebuffer
  /// from these declarations during build(). Overrides useRenderPass().
  std::vector<RenderPassAttachment> attachments;

  // ── Viewport / scissor ─────────────────────────────────────────
  bool autoViewportScissor{false};  ///< Automatically set viewport/scissor from renderArea
  vk::Viewport customViewport{};   ///< If width > 0, overrides autoViewportScissor
  vk::Rect2D customScissor{};

  // ── Descriptor set declarations ────────────────────────────────
  std::vector<PassDescriptorSetInfo> descriptorSets;  ///< One per set number (0, 1, …)

  // ── Push constants ─────────────────────────────────────────────
  std::vector<PassPushConstantInfo> pushConstants;

  RenderPassNode() = default;
  RenderPassNode(u32 id, std::string_view name, PassType type)
      : id{id}, name{name}, type{type} {}

  // ── Resource access declarations ───────────────────────────────

  void read(ResourceId res, const ResourceAccessInfo& info) {
    ResourceRef ref;
    ref.resourceId = res;
    ref.access = ResourceAccess::Read;
    ref.accessInfo = info;
    reads.push_back(ref);
  }

  void write(ResourceId res, const ResourceAccessInfo& info) {
    ResourceRef ref;
    ref.resourceId = res;
    ref.access = ResourceAccess::Write;
    ref.accessInfo = info;
    writes.push_back(ref);
  }

  void readWrite(ResourceId res, const ResourceAccessInfo& readInfo,
                 const ResourceAccessInfo& writeInfo) {
    read(res, readInfo);
    write(res, writeInfo);
  }

  // ── Pipeline binding helpers ───────────────────────────────────

  /// @brief Bind a graphics pipeline for this pass (enables auto-bind of descriptors).
  void usePipeline(vk::Pipeline p, vk::PipelineLayout layout,
                   vk::PipelineBindPoint bp = vk::PipelineBindPoint::eGraphics) {
    pipelineInfo = {p, layout, bp};
  }

  /// @brief Convenience: bind from a zs::Pipeline (implicit conversion).
  template <typename PipelineT>
  void usePipeline(const PipelineT& p,
                   vk::PipelineBindPoint bp = vk::PipelineBindPoint::eGraphics) {
    pipelineInfo = {static_cast<vk::Pipeline>(*p),
                    static_cast<vk::PipelineLayout>(p), bp};
  }

  // ── Declarative pipeline helpers (graph auto-builds) ───────────

  /// @brief Set a graphics pipeline description (uncached, rebuilt every frame).
  void setGraphicsPipelineDesc(GraphicsPipelineDesc desc) {
    pipelineDesc.type = PassPipelineDesc::Type::Graphics;
    pipelineDesc.graphicsDesc = std::move(desc);
    pipelineTag.clear();
  }

  /// @brief Set a graphics pipeline description with a cache tag.
  /// The pipeline is built once and reused across frames.
  /// Call `PassPipelineCache::markDirty(tag)` to force a rebuild.
  void setGraphicsPipelineDesc(std::string tag, GraphicsPipelineDesc desc) {
    pipelineDesc.type = PassPipelineDesc::Type::Graphics;
    pipelineDesc.graphicsDesc = std::move(desc);
    pipelineTag = std::move(tag);
  }

  /// @brief Set a compute pipeline description (uncached, rebuilt every frame).
  void setComputePipelineDesc(const ShaderStageDesc& shader,
                              std::vector<vk::PushConstantRange> pushConstantRanges = {}) {
    pipelineDesc.type = PassPipelineDesc::Type::Compute;
    pipelineDesc.computeShader = shader;
    pipelineDesc.computePushConstantRanges = std::move(pushConstantRanges);
    pipelineTag.clear();
  }

  /// @brief Set a compute pipeline description with a cache tag.
  void setComputePipelineDesc(std::string tag,
                              const ShaderStageDesc& shader,
                              std::vector<vk::PushConstantRange> pushConstantRanges = {}) {
    pipelineDesc.type = PassPipelineDesc::Type::Compute;
    pipelineDesc.computeShader = shader;
    pipelineDesc.computePushConstantRanges = std::move(pushConstantRanges);
    pipelineTag = std::move(tag);
  }

  // ── Render pass / framebuffer helpers ──────────────────────────

  /// @brief Set the VkRenderPass, framebuffer, render area, and clear values.
  /// This is the manual (legacy) path. Prefer addColorAttachment()/addDepthAttachment()
  /// for graph-managed render passes.
  void useRenderPass(vk::RenderPass rp, vk::Framebuffer fb, vk::Extent2D area,
                     std::vector<vk::ClearValue> clears = {}) {
    renderPass = rp;
    framebuffer = fb;
    renderArea = area;
    clearValues = std::move(clears);
  }

  // ── Render pass attachment declaration helpers ─────────────────

  /// @brief Declare a color attachment for this pass. The render graph will
  /// auto-create a VkRenderPass and VkFramebuffer during build().
  /// @param res        Resource id of the image used as attachment
  /// @param loadOp     Load operation (eClear, eLoad, eDontCare)
  /// @param clear      Clear value (used when loadOp == eClear)
  /// @param finalLayout  Final layout override (eUndefined = auto: eColorAttachmentOptimal)
  void addColorAttachment(ResourceId res,
                          vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear,
                          vk::ClearValue clear = {},
                          vk::ImageLayout finalLayout = vk::ImageLayout::eUndefined) {
    RenderPassAttachment att;
    att.resourceId = res;
    att.isDepthStencil = false;
    att.loadOp = loadOp;
    att.clearValue = clear;
    att.finalLayout = finalLayout;
    attachments.push_back(att);
  }

  /// @brief Declare a depth/stencil attachment for this pass.
  /// @param res        Resource id of the depth image
  /// @param loadOp     Load operation (eClear, eLoad, eDontCare)
  /// @param clear      Clear value (used when loadOp == eClear)
  /// @param finalLayout  Final layout override (eUndefined = auto: eDepthStencilAttachmentOptimal)
  void addDepthAttachment(ResourceId res,
                          vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear,
                          vk::ClearValue clear = {},
                          vk::ImageLayout finalLayout = vk::ImageLayout::eUndefined) {
    RenderPassAttachment att;
    att.resourceId = res;
    att.isDepthStencil = true;
    att.loadOp = loadOp;
    att.clearValue = clear;
    att.finalLayout = finalLayout;
    attachments.push_back(att);
  }

  // ── Descriptor binding helpers (image) ─────────────────────────

  /// @brief Bind a combined image sampler from a VkTexture.
  void bindImage(u32 setIndex, u32 binding, const VkTexture& texture,
                 u32 arrayElement = 0,
                 vk::DescriptorType type = vk::DescriptorType::eCombinedImageSampler) {
    ensureSetCount(setIndex + 1);
    PassDescriptorWrite w;
    w.binding = binding;
    w.arrayElement = arrayElement;
    w.type = type;
    w.imageInfo = vk::DescriptorImageInfo{
        texture.sampler,
        static_cast<vk::ImageView>(texture.image.get()),
        texture.imageLayout};
    descriptorSets[setIndex].writes.push_back(w);
  }

  /// @brief Bind a combined image sampler from raw descriptor info.
  void bindImage(u32 setIndex, u32 binding, const vk::DescriptorImageInfo& info,
                 u32 arrayElement = 0,
                 vk::DescriptorType type = vk::DescriptorType::eCombinedImageSampler) {
    ensureSetCount(setIndex + 1);
    PassDescriptorWrite w;
    w.binding = binding;
    w.arrayElement = arrayElement;
    w.type = type;
    w.imageInfo = info;
    descriptorSets[setIndex].writes.push_back(w);
  }

  // ── Descriptor binding helpers (buffer) ────────────────────────

  /// @brief Bind a buffer descriptor.
  void bindBuffer(u32 setIndex, u32 binding, const vk::DescriptorBufferInfo& info,
                  vk::DescriptorType type = vk::DescriptorType::eUniformBuffer) {
    ensureSetCount(setIndex + 1);
    PassDescriptorWrite w;
    w.binding = binding;
    w.type = type;
    w.bufferInfo = info;
    descriptorSets[setIndex].writes.push_back(w);
  }

  // ── Descriptor set layout ─────────────────────────────────────

  /// @brief Set the descriptor set layout for a given set index.
  void setDescriptorSetLayout(u32 setIndex, vk::DescriptorSetLayout layout) {
    ensureSetCount(setIndex + 1);
    descriptorSets[setIndex].layout = layout;
  }

  // ── Push constant helpers ──────────────────────────────────────

  /// @brief Declare a push constant range and copy data inline.
  template <typename T>
  void setPushConstant(vk::ShaderStageFlags stages, u32 offset, const T& value) {
    PassPushConstantInfo pc;
    pc.stages = stages;
    pc.offset = offset;
    pc.size = sizeof(T);
    pc.data.resize(sizeof(T));
    std::memcpy(pc.data.data(), &value, sizeof(T));
    pushConstants.push_back(std::move(pc));
  }

private:
  void ensureSetCount(u32 count) {
    if (descriptorSets.size() < count) {
      descriptorSets.resize(count);
    }
  }
};

// ============================================================================
// Barrier Info (computed during compilation)
// ============================================================================

struct BarrierInfo {
  ResourceId resourceId{InvalidResourceId};
  ResourceAccessInfo srcAccess;
  ResourceAccessInfo dstAccess;
  vk::DependencyFlags flags{};

  bool isValid() const { return resourceId != InvalidResourceId; }
};

// ============================================================================
// Compiled Pass (ready for execution)
// ============================================================================

struct CompiledPass {
  u32 passId{~0u};
  std::vector<BarrierInfo> preBarriers;
  std::vector<BarrierInfo> postBarriers;
  vk::PipelineStageFlags2 waitStages{};
  vk::PipelineStageFlags2 signalStages{};

  bool needsRenderPass{false};
  vk::RenderPass renderPass{VK_NULL_HANDLE};
  vk::Framebuffer framebuffer{VK_NULL_HANDLE};
  vk::Extent2D renderArea{};
};

// ============================================================================
// Render Graph Builder
// ============================================================================

struct ZPC_CORE_API RenderGraphBuilder {
  RenderGraphBuilder(VulkanContext& ctx) : ctx{ctx} {}

  ResourceId createBuffer(std::string_view name, const TransientBufferDesc& desc);
  ResourceId createImage(std::string_view name, const TransientImageDesc& desc);

  ResourceId importBuffer(std::string_view name, Buffer* buffer);
  ResourceId importBuffer(std::string_view name, Buffer* buffer, const TransientBufferDesc& desc);
  ResourceId importBuffer(std::string_view name, vk::Buffer buffer,
                          const TransientBufferDesc& desc);
  ResourceId importImage(std::string_view name, Image* image);
  ResourceId importImage(std::string_view name, Image* image, const TransientImageDesc& desc);
  ResourceId importImage(std::string_view name, vk::Image image, const TransientImageDesc& desc);
  ResourceId importImage(std::string_view name, vk::Image image, vk::ImageView view,
                         const TransientImageDesc& desc);

  u32 addPass(std::string_view name, PassType type, QueueType queue,
              std::function<void(RenderPassNode&)> setup);

  /// @brief Insert a pass immediately before target pass.
  /// Adds dependency: inserted -> target.
  u32 insertPassBefore(u32 targetPassId, std::string_view name, PassType type, QueueType queue,
                       std::function<void(RenderPassNode&)> setup);

  /// @brief Insert a pass immediately after target pass.
  /// Adds dependency: target -> inserted. Optionally rewires existing dependents.
  u32 insertPassAfter(u32 targetPassId, std::string_view name, PassType type, QueueType queue,
                      std::function<void(RenderPassNode&)> setup,
                      bool rewireDependents = true);

  /// @brief Add a pass that is instantiated once per RenderJob.
  ///
  /// For each job the callback receives the pass node and the job, so the
  /// user can set up per-view descriptors, push constants, etc.
  /// Returns the id of the *first* generated pass.
  u32 addPassPerJob(std::string_view baseName, PassType type, QueueType queue,
                    const std::vector<RenderJob>& jobs,
                    std::function<void(RenderPassNode&, const RenderJob&, u32 jobIndex)> setup);

  RenderPassNode& getPass(u32 passId);
  VirtualResource& getResource(ResourceId resId);

  void addDependency(u32 fromPass, u32 toPass);
  void removeDependency(u32 fromPass, u32 toPass);
  void clearDependencies(u32 passId);
  bool hasPass(u32 passId) const;

  /// @brief Remove a pass. If reconnectDependencies is true, each dependent pass
  /// receives removed pass's dependencies to preserve DAG reachability.
  void removePass(u32 passId, bool reconnectDependencies = true);

  /// @brief Compile the render graph and allocate resources.
  /// @param pipelineCache  Optional pipeline cache.  When provided, passes
  ///        with a `pipelineTag` consult the cache instead of creating a new
  ///        pipeline every frame.  Pipelines that are replaced due to dirty
  ///        flags are moved to `managedPipelines` in the returned graph so
  ///        their GPU lifetime is tied to the frame fence.
  RenderGraph build(PassPipelineCache* pipelineCache = nullptr,
                    RenderGraphCompileCache* compileCache = nullptr,
                    bool forceRecompile = false);

private:
  friend struct AdvancedRenderGraph;

  VulkanContext& ctx;

  std::vector<VirtualResource> resources;
  std::vector<RenderPassNode> passes;
  std::vector<std::pair<ResourceId, Buffer*>> importedBuffers;
  std::vector<std::pair<ResourceId, Image*>> importedImages;
  std::unordered_map<ResourceId, vk::Buffer> importedVkBuffers;
  std::unordered_map<ResourceId, vk::Image> importedVkImages;
  std::unordered_map<ResourceId, vk::ImageView> importedVkImageViews;

  ResourceId nextResourceId{0};
  u32 nextPassId{0};
};

// ============================================================================
// Render Graph Compiler
// ============================================================================

struct ZPC_CORE_API RenderGraphCompiler {
  explicit RenderGraphCompiler(VulkanContext& ctx) : ctx{ctx} {}

  struct CompilationResult {
    std::vector<CompiledPass> compiledPasses;
    std::vector<u32> executionOrder;
    std::vector<ResourceAccessInfo> finalResourceStates;
    bool success{false};
    std::string error;
  };

  CompilationResult compile(const std::vector<VirtualResource>& resources,
                            const std::vector<RenderPassNode>& passes);

  friend struct RenderGraphBuilder;

protected:
  VulkanContext& ctx;

private:
  std::vector<u32> topologicalSort(const std::vector<RenderPassNode>& passes);
  void computeBarriers(const std::vector<VirtualResource>& resources,
                       const std::vector<RenderPassNode>& passes,
                       const std::vector<u32>& executionOrder,
                       std::vector<CompiledPass>& outCompiled);
  void cullUnusedPasses(std::vector<RenderPassNode>& passes);
};

// ============================================================================
// Render Graph Compilation Cache
// ============================================================================

struct ZPC_CORE_API RenderGraphCompileCache {
  using CompilationResult = RenderGraphCompiler::CompilationResult;

  bool tryGet(u64 signature, CompilationResult& out) const;
  void store(u64 signature, const CompilationResult& result);
  void erase(u64 signature);
  void clear();
  size_t size() const noexcept { return _cache.size(); }

private:
  std::unordered_map<u64, CompilationResult> _cache;
};

// ============================================================================
// Render Graph Executor
// ============================================================================

struct ZPC_CORE_API RenderGraphExecutor {
  explicit RenderGraphExecutor(VulkanContext& ctx) : ctx{ctx} {}

  void execute(const RenderGraph& graph, vk::Queue queue, vk::Fence fence = VK_NULL_HANDLE);
  void executeWithSync(const RenderGraph& graph, vk::Queue queue,
                       const std::vector<vk::Semaphore>& waitSemaphores,
                       const std::vector<vk::PipelineStageFlags>& waitStages,
                       const std::vector<vk::Semaphore>& signalSemaphores,
                       vk::Fence fence = VK_NULL_HANDLE);

protected:
  VulkanContext& ctx;
  /// Per-execution descriptor pool (set by execute/executeWithSync from graph.externalDescriptorPool)
  vk::DescriptorPool executionPool{VK_NULL_HANDLE};

  void recordPass(vk::CommandBuffer cmd, const RenderPassNode& pass,
                  const CompiledPass& compiled);
  void injectBarriers(vk::CommandBuffer cmd, const std::vector<BarrierInfo>& barriers,
                      const std::vector<VirtualResource>& resources, RenderGraph& graph);
};

// ============================================================================
// Render Graph (Main Interface)
// ============================================================================

struct ZPC_CORE_API RenderGraph {
  RenderGraph() = default;
  RenderGraph(VulkanContext& ctx) : ctx{&ctx} {}

  // Non-copyable (contains RAII vectors of RenderPass / Framebuffer)
  RenderGraph(const RenderGraph&) = delete;
  RenderGraph& operator=(const RenderGraph&) = delete;

  // Move-only
  RenderGraph(RenderGraph&&) noexcept = default;
  RenderGraph& operator=(RenderGraph&&) noexcept = default;

  bool isValid() const { return ctx != nullptr && compiled; }

  void execute(vk::Queue queue, vk::Fence fence = VK_NULL_HANDLE);
  void executeWithSync(vk::Queue queue,
                       const std::vector<vk::Semaphore>& waitSemaphores,
                       const std::vector<vk::PipelineStageFlags>& waitStages,
                       const std::vector<vk::Semaphore>& signalSemaphores,
                       vk::Fence fence = VK_NULL_HANDLE);

  VulkanContext* ctx{nullptr};

  /// @brief Optional external descriptor pool for per-frame set allocation.
  /// If set (non-null), the executor allocates descriptor sets from this
  /// pool instead of the global ctx pool. The caller is responsible for
  /// lifetime management (e.g., per-frame rotation and reset after fence wait).
  vk::DescriptorPool externalDescriptorPool{VK_NULL_HANDLE};

  std::vector<VirtualResource> resources;
  std::vector<RenderPassNode> passes;
  std::vector<CompiledPass> compiledPasses;
  std::vector<u32> executionOrder;

  std::unordered_map<ResourceId, Owner<Buffer>> allocatedBuffers;
  std::unordered_map<ResourceId, Owner<Image>> allocatedImages;
  std::vector<std::pair<ResourceId, Buffer*>> importedBuffers;
  std::vector<std::pair<ResourceId, Image*>> importedImages;
  std::unordered_map<ResourceId, vk::Buffer> importedVkBuffers;
  std::unordered_map<ResourceId, vk::Image> importedVkImages;
  std::unordered_map<ResourceId, vk::ImageView> importedVkImageViews;

  /// Auto-created render pass and framebuffer resources (RAII, destroyed with graph).
  /// When using the ring-buffer pattern (one RenderGraph per buffered frame),
  /// these are safely destroyed after the corresponding fence has been waited on.
  std::vector<RenderPass> managedRenderPasses;
  std::vector<Framebuffer> managedFramebuffers;

  /// Auto-created pipelines (RAII, destroyed with graph).
  std::vector<std::unique_ptr<Pipeline>> managedPipelines;

  bool compiled{false};

  Buffer* getBuffer(ResourceId id);
  Image* getImage(ResourceId id);
  vk::Buffer getExternalBuffer(ResourceId id) const;
  vk::Image getExternalImage(ResourceId id) const;

  /// @brief Get the image view for a render-graph resource.
  /// Resolves imported Image*, imported vk::Image+view, and allocated images.
  vk::ImageView getImageView(ResourceId id) const;

  /// @brief Find a virtual resource by id.
  const VirtualResource* findResource(ResourceId id) const;

private:
  friend struct RenderGraphBuilder;
  friend struct RenderGraphCompiler;
  friend struct RenderGraphExecutor;
};

// ============================================================================
// Access Info Helpers
// ============================================================================

namespace access {

inline ResourceAccessInfo computeRead() {
  return {vk::PipelineStageFlagBits2::eComputeShader,
          vk::AccessFlagBits2::eShaderRead,
          vk::ImageLayout::eGeneral};
}

inline ResourceAccessInfo computeWrite() {
  return {vk::PipelineStageFlagBits2::eComputeShader,
          vk::AccessFlagBits2::eShaderWrite,
          vk::ImageLayout::eGeneral};
}

inline ResourceAccessInfo computeReadWrite() {
  return {vk::PipelineStageFlagBits2::eComputeShader,
          vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
          vk::ImageLayout::eGeneral};
}

inline ResourceAccessInfo vertexBufferRead() {
  return {vk::PipelineStageFlagBits2::eVertexInput,
          vk::AccessFlagBits2::eVertexAttributeRead,
          vk::ImageLayout::eUndefined};
}

inline ResourceAccessInfo indexBufferRead() {
  return {vk::PipelineStageFlagBits2::eVertexInput,
          vk::AccessFlagBits2::eIndexRead,
          vk::ImageLayout::eUndefined};
}

inline ResourceAccessInfo indirectBufferRead() {
  return {vk::PipelineStageFlagBits2::eDrawIndirect,
          vk::AccessFlagBits2::eIndirectCommandRead,
          vk::ImageLayout::eUndefined};
}

inline ResourceAccessInfo uniformBufferRead() {
  return {vk::PipelineStageFlagBits2::eAllGraphics,
          vk::AccessFlagBits2::eUniformRead,
          vk::ImageLayout::eUndefined};
}

inline ResourceAccessInfo colorAttachmentWrite(vk::AttachmentLoadOp loadOp
                                               = vk::AttachmentLoadOp::eClear) {
  return {vk::PipelineStageFlagBits2::eColorAttachmentOutput,
          vk::AccessFlagBits2::eColorAttachmentWrite
              | (loadOp == vk::AttachmentLoadOp::eLoad
                     ? vk::AccessFlagBits2::eColorAttachmentRead
                     : vk::AccessFlags2{}),
          vk::ImageLayout::eColorAttachmentOptimal};
}

inline ResourceAccessInfo depthAttachmentWrite(vk::AttachmentLoadOp loadOp
                                               = vk::AttachmentLoadOp::eClear) {
  return {vk::PipelineStageFlagBits2::eEarlyFragmentTests
              | vk::PipelineStageFlagBits2::eLateFragmentTests,
          vk::AccessFlagBits2::eDepthStencilAttachmentWrite
              | (loadOp == vk::AttachmentLoadOp::eLoad
                     ? vk::AccessFlagBits2::eDepthStencilAttachmentRead
                     : vk::AccessFlags2{}),
          vk::ImageLayout::eDepthStencilAttachmentOptimal};
}

inline ResourceAccessInfo sampleImage(vk::ImageLayout layout
                                      = vk::ImageLayout::eShaderReadOnlyOptimal,
                                      vk::PipelineStageFlags2 stage
                                      = vk::PipelineStageFlagBits2::eFragmentShader) {
  return {stage,
          vk::AccessFlagBits2::eShaderSampledRead,
          layout};
}

inline ResourceAccessInfo storageImage(vk::ImageLayout layout = vk::ImageLayout::eGeneral,
                                       vk::PipelineStageFlags2 stage
                                       = vk::PipelineStageFlagBits2::eComputeShader) {
  return {stage,
          vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderStorageRead,
          layout};
}

inline ResourceAccessInfo transferSrc() {
  return {vk::PipelineStageFlagBits2::eTransfer,
          vk::AccessFlagBits2::eTransferRead,
          vk::ImageLayout::eTransferSrcOptimal};
}

inline ResourceAccessInfo transferDst() {
  return {vk::PipelineStageFlagBits2::eTransfer,
          vk::AccessFlagBits2::eTransferWrite,
          vk::ImageLayout::eTransferDstOptimal};
}

inline ResourceAccessInfo present() {
  return {vk::PipelineStageFlagBits2::eBottomOfPipe,
          vk::AccessFlags2{},
          vk::ImageLayout::ePresentSrcKHR};
}

inline ResourceAccessInfo undefined() {
  return {vk::PipelineStageFlags2{},
          vk::AccessFlags2{},
          vk::ImageLayout::eUndefined};
}

}  // namespace access

}  // namespace zs
