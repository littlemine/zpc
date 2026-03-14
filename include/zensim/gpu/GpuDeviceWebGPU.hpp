// Copyright (c) zs contributors. Licensed under the MIT License.
// GpuDeviceWebGPU.hpp - WebGPU backend implementation of gpu::Device.
//
// Wraps the WebGPU C API (webgpu.h) via Dawn, wgpu-native, or Emscripten.
// The gpu:: abstraction was designed with WebGPU as the baseline, so this
// backend provides the most direct mapping of all backends.
//
// Key WebGPU-specific behaviors:
//   - No render pass objects: pipelines use format signatures directly
//   - No push constants: emulated via reserved uniform buffer
//   - Async buffer mapping: mapBuffer() uses wgpuBufferMapAsync + poll
//   - Immutable bind groups: matches our descriptor model exactly
//   - Separate textures/samplers: no combined image sampler
//
// Build: define ZS_GPU_WEBGPU_IMPL to enable actual WebGPU API calls.
//        Without it, the header compiles with placeholder types for
//        structure validation and IDE support.

#pragma once

#include "GpuDevice.hpp"
#include "GpuWebGPUMapping.hpp"

// WebGPU header - enable via build system:
// #include <webgpu/webgpu.h>        // C API (Emscripten, wgpu-native)
// #include <webgpu/webgpu_cpp.h>    // Dawn C++ API

#include <cassert>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zs::gpu {

  // =========================================================================
  // Placeholder WebGPU types (when webgpu.h is not included)
  // =========================================================================
#ifndef WEBGPU_H_
  using WGPUDevice             = void*;
  using WGPUQueue              = void*;
  using WGPUBuffer             = void*;
  using WGPUTexture            = void*;
  using WGPUTextureView        = void*;
  using WGPUSampler            = void*;
  using WGPUShaderModule       = void*;
  using WGPUBindGroupLayout    = void*;
  using WGPUBindGroup          = void*;
  using WGPURenderPipeline     = void*;
  using WGPUComputePipeline    = void*;
  using WGPUCommandEncoder     = void*;
  using WGPUCommandBuffer      = void*;
  using WGPURenderPassEncoder  = void*;
  using WGPUComputePassEncoder = void*;
  using WGPUPipelineLayout     = void*;
#endif

  // =========================================================================
  // Slot-based resource pool (shared with Vulkan backend)
  // =========================================================================
  // Note: ResourcePool<T> is defined in GpuDeviceVk.hpp.
  // For standalone WebGPU usage, include a local copy:
#ifndef ZS_GPU_RESOURCE_POOL_DEFINED_
#define ZS_GPU_RESOURCE_POOL_DEFINED_
  template <typename Record>
  class WGPUResourcePool {
  public:
    uint64_t allocate(Record&& rec) {
      uint64_t idx;
      if (!freeList_.empty()) {
        idx = freeList_.back();
        freeList_.pop_back();
        slots_[idx].emplace(std::move(rec));
      } else {
        idx = slots_.size();
        slots_.push_back(std::optional<Record>{std::move(rec)});
      }
      return idx + 1;
    }
    void release(uint64_t id) {
      if (id == 0) return;
      auto idx = id - 1;
      if (idx < slots_.size() && slots_[idx].has_value()) {
        slots_[idx].reset();
        freeList_.push_back(idx);
      }
    }
    Record* get(uint64_t id) {
      if (id == 0) return nullptr;
      auto idx = id - 1;
      return (idx < slots_.size() && slots_[idx].has_value()) ? &*slots_[idx] : nullptr;
    }
    const Record* get(uint64_t id) const {
      if (id == 0) return nullptr;
      auto idx = id - 1;
      return (idx < slots_.size() && slots_[idx].has_value()) ? &*slots_[idx] : nullptr;
    }
    size_t activeCount() const { return slots_.size() - freeList_.size(); }
  private:
    std::vector<std::optional<Record>> slots_;
    std::vector<uint64_t> freeList_;
  };
#endif

  // =========================================================================
  // WebGPU resource records
  // =========================================================================

  struct WGPUBufferRecord {
    WGPUBuffer  buffer  = nullptr;
    uint64_t    size    = 0;
    BufferUsage usage   = BufferUsage::None;
    void*       mapped  = nullptr;
  };

  struct WGPUTextureRecord {
    WGPUTexture     texture     = nullptr;
    WGPUTextureView defaultView = nullptr;
    uint32_t        width = 0, height = 0, depthOrLayers = 1;
    Format          format = Format::Undefined;
    uint32_t        mipLevels = 1;
    uint32_t        arrayLayers = 1;
    TextureUsage    usage = TextureUsage::None;
  };

  struct WGPUTextureViewRecord {
    WGPUTextureView view = nullptr;
    TextureHandle   ownerTexture;
  };

  struct WGPUSamplerRecord {
    WGPUSampler sampler = nullptr;
  };

  struct WGPUShaderModuleRecord {
    WGPUShaderModule module     = nullptr;
    std::string      entryPoint = "main";
  };

  struct WGPUBindGroupLayoutRecord {
    WGPUBindGroupLayout layout = nullptr;
    std::vector<BindGroupLayoutEntry> entries;
  };

  struct WGPUBindGroupRecord {
    WGPUBindGroup         group = nullptr;
    BindGroupLayoutHandle layoutHandle;
  };

  struct WGPURenderPipelineRecord {
    WGPURenderPipeline pipeline       = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
  };

  struct WGPUComputePipelineRecord {
    WGPUComputePipeline pipeline       = nullptr;
    WGPUPipelineLayout  pipelineLayout = nullptr;
  };

  struct WGPUCommandBufferRecord {
    WGPUCommandBuffer cmdBuf = nullptr;
  };

  // =========================================================================
  // Push constant emulation
  // =========================================================================
  // WebGPU has no push constants. We emulate them by reserving a uniform
  // buffer that is automatically bound at a well-known location (e.g.,
  // bind group 3, binding 0). The shader compiler must transform push_constant
  // references to uniform buffer reads.
  struct PushConstantEmulation {
    static constexpr uint32_t kBindGroup = 3;
    static constexpr uint32_t kBinding   = 0;
    static constexpr uint32_t kMaxSize   = 256;  // 256 bytes, matches Vulkan minimum

    BufferHandle    uniformBuffer{};
    BindGroupHandle bindGroup{};
    uint8_t         data[kMaxSize]{};
    bool            dirty = false;
  };

  // =========================================================================
  // Forward declarations
  // =========================================================================
  class WebGPUDevice;
  class WebGPURenderPassEncoderImpl;
  class WebGPUComputePassEncoderImpl;
  class WebGPUCommandEncoderImpl;

  // =========================================================================
  // WebGPURenderPassEncoderImpl
  // =========================================================================
  class WebGPURenderPassEncoderImpl : public RenderPassEncoder {
  public:
    WebGPURenderPassEncoderImpl(WebGPUDevice& dev, WGPURenderPassEncoder enc)
        : dev_(dev), enc_(enc) {}

    void setPipeline(RenderPipelineHandle pipeline) override;
    void setBindGroup(uint32_t groupIndex, BindGroupHandle group) override;
    void setVertexBuffer(uint32_t slot, BufferHandle buffer,
                         uint64_t offset, uint64_t size) override;
    void setIndexBuffer(BufferHandle buffer, IndexFormat format,
                        uint64_t offset, uint64_t size) override;
    void setViewport(const Viewport& vp) override;
    void setScissor(const Scissor& sc) override;
    void setPushConstants(ShaderStage stages, uint32_t offset,
                          uint32_t size, const void* data) override;
    void draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t firstVertex, uint32_t firstInstance) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                     uint32_t firstIndex, int32_t vertexOffset,
                     uint32_t firstInstance) override;
    void drawIndirect(BufferHandle indirectBuffer,
                      uint64_t indirectOffset) override;
    void drawIndexedIndirect(BufferHandle indirectBuffer,
                             uint64_t indirectOffset) override;
    void end() override;

  private:
    WebGPUDevice&          dev_;
    WGPURenderPassEncoder  enc_;
    WGPURenderPipeline     currentPipeline_ = nullptr;
    WGPUPipelineLayout     currentLayout_   = nullptr;
  };

  // =========================================================================
  // WebGPUComputePassEncoderImpl
  // =========================================================================
  class WebGPUComputePassEncoderImpl : public ComputePassEncoder {
  public:
    WebGPUComputePassEncoderImpl(WebGPUDevice& dev, WGPUComputePassEncoder enc)
        : dev_(dev), enc_(enc) {}

    void setPipeline(ComputePipelineHandle pipeline) override;
    void setBindGroup(uint32_t groupIndex, BindGroupHandle group) override;
    void setPushConstants(ShaderStage stages, uint32_t offset,
                          uint32_t size, const void* data) override;
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override;
    void dispatchIndirect(BufferHandle buffer, uint64_t offset) override;
    void end() override;

  private:
    WebGPUDevice&           dev_;
    WGPUComputePassEncoder  enc_;
  };

  // =========================================================================
  // WebGPUCommandEncoderImpl
  // =========================================================================
  class WebGPUCommandEncoderImpl : public CommandEncoder {
  public:
    WebGPUCommandEncoderImpl(WebGPUDevice& dev, WGPUCommandEncoder enc)
        : dev_(dev), enc_(enc) {}

    RenderPassEncoder* beginRenderPass(const RenderPassBeginDesc& desc) override;
    ComputePassEncoder* beginComputePass() override;

    void copyBufferToBuffer(BufferHandle src, uint64_t srcOffset,
                            BufferHandle dst, uint64_t dstOffset,
                            uint64_t size) override;
    void copyBufferToTexture(BufferHandle src, uint64_t srcOffset,
                             uint32_t bytesPerRow, uint32_t rowsPerImage,
                             TextureHandle dst,
                             uint32_t mipLevel, uint32_t arrayLayer,
                             uint32_t width, uint32_t height,
                             uint32_t depth) override;
    void copyTextureToBuffer(TextureHandle src,
                             uint32_t mipLevel, uint32_t arrayLayer,
                             uint32_t width, uint32_t height,
                             uint32_t depth,
                             BufferHandle dst, uint64_t dstOffset,
                             uint32_t bytesPerRow, uint32_t rowsPerImage) override;
    CommandBufferHandle finish() override;

  private:
    WebGPUDevice&       dev_;
    WGPUCommandEncoder  enc_;
    std::unique_ptr<WebGPURenderPassEncoderImpl>  currentRenderPass_;
    std::unique_ptr<WebGPUComputePassEncoderImpl> currentComputePass_;
  };

  // =========================================================================
  // WebGPUDevice
  // =========================================================================

  class WebGPUDevice : public Device {
  public:
    explicit WebGPUDevice(WGPUDevice device, WGPUQueue queue)
        : device_(device), queue_(queue) {}

    ~WebGPUDevice() override {
      // Release push constant emulation resources
      if (pushConstants_.uniformBuffer.id != 0)
        destroyBuffer(pushConstants_.uniformBuffer);
    }

    // -- Info --
    std::string_view backendName() const override { return "WebGPU"; }
    std::string_view deviceName() const override { return deviceName_; }

    // =====================================================================
    // Resource creation
    // =====================================================================

    BufferHandle createBuffer(const BufferDesc& desc) override {
#ifdef ZS_GPU_WEBGPU_IMPL
      WGPUBufferDescriptor wgpuDesc{};
      wgpuDesc.size             = desc.size;
      wgpuDesc.usage            = wgpu_map::toWGPU(desc.usage);
      wgpuDesc.mappedAtCreation = desc.mappedAtCreation;
      if (!desc.label.empty()) wgpuDesc.label = desc.label.c_str();

      WGPUBuffer buf = wgpuDeviceCreateBuffer(device_, &wgpuDesc);
      if (!buf) return {};

      WGPUBufferRecord rec{};
      rec.buffer = buf;
      rec.size   = desc.size;
      rec.usage  = desc.usage;
      if (desc.mappedAtCreation) {
        rec.mapped = wgpuBufferGetMappedRange(buf, 0, desc.size);
      }

      std::lock_guard lock(mutex_);
      return BufferHandle{buffers_.allocate(std::move(rec))};
#else
      WGPUBufferRecord rec{};
      rec.size  = desc.size;
      rec.usage = desc.usage;
      std::lock_guard lock(mutex_);
      return BufferHandle{buffers_.allocate(std::move(rec))};
#endif
    }

    TextureHandle createTexture(const TextureDesc& desc) override {
#ifdef ZS_GPU_WEBGPU_IMPL
      WGPUTextureDescriptor wgpuDesc{};
      switch (desc.dimension) {
        case TextureDimension::e1D: wgpuDesc.dimension = WGPUTextureDimension_1D; break;
        case TextureDimension::e3D: wgpuDesc.dimension = WGPUTextureDimension_3D; break;
        default:                    wgpuDesc.dimension = WGPUTextureDimension_2D; break;
      }
      wgpuDesc.size.width              = desc.width;
      wgpuDesc.size.height             = desc.height;
      wgpuDesc.size.depthOrArrayLayers = desc.depthOrLayers;
      wgpuDesc.mipLevelCount           = desc.mipLevels;
      wgpuDesc.sampleCount             = static_cast<uint32_t>(desc.samples);
      wgpuDesc.format    = static_cast<WGPUTextureFormat>(wgpu_map::toWGPU(desc.format));
      wgpuDesc.usage     = wgpu_map::toWGPU(desc.usage);
      if (!desc.label.empty()) wgpuDesc.label = desc.label.c_str();

      WGPUTexture tex = wgpuDeviceCreateTexture(device_, &wgpuDesc);
      if (!tex) return {};

      // Create default view
      WGPUTextureView defaultView = wgpuTextureCreateView(tex, nullptr);

      WGPUTextureRecord rec{};
      rec.texture       = tex;
      rec.defaultView   = defaultView;
      rec.width         = desc.width;
      rec.height        = desc.height;
      rec.depthOrLayers = desc.depthOrLayers;
      rec.format        = desc.format;
      rec.mipLevels     = desc.mipLevels;
      rec.usage         = desc.usage;

      std::lock_guard lock(mutex_);
      return TextureHandle{textures_.allocate(std::move(rec))};
#else
      WGPUTextureRecord rec{};
      rec.width = desc.width; rec.height = desc.height;
      rec.depthOrLayers = desc.depthOrLayers;
      rec.format = desc.format; rec.mipLevels = desc.mipLevels;
      rec.usage = desc.usage;
      std::lock_guard lock(mutex_);
      return TextureHandle{textures_.allocate(std::move(rec))};
#endif
    }

    TextureViewHandle createTextureView(
        TextureHandle texture, const TextureViewDesc& desc) override {
#ifdef ZS_GPU_WEBGPU_IMPL
      auto* texRec = textures_.get(texture.id);
      if (!texRec) return {};

      WGPUTextureViewDescriptor viewDesc{};
      // Map dimension
      switch (desc.dimension) {
        case TextureViewDimension::e2D:
          viewDesc.dimension = WGPUTextureViewDimension_2D; break;
        case TextureViewDimension::e2DArray:
          viewDesc.dimension = WGPUTextureViewDimension_2DArray; break;
        case TextureViewDimension::Cube:
          viewDesc.dimension = WGPUTextureViewDimension_Cube; break;
        case TextureViewDimension::CubeArray:
          viewDesc.dimension = WGPUTextureViewDimension_CubeArray; break;
        case TextureViewDimension::e3D:
          viewDesc.dimension = WGPUTextureViewDimension_3D; break;
        default:
          viewDesc.dimension = WGPUTextureViewDimension_2D; break;
      }
      viewDesc.format         = static_cast<WGPUTextureFormat>(
          desc.format != Format::Undefined ? wgpu_map::toWGPU(desc.format)
                                           : wgpu_map::toWGPU(texRec->format));
      viewDesc.baseMipLevel   = desc.baseMipLevel;
      viewDesc.mipLevelCount  = desc.mipLevelCount == 0 ? WGPU_MIP_LEVEL_COUNT_UNDEFINED
                                                         : desc.mipLevelCount;
      viewDesc.baseArrayLayer = desc.baseArrayLayer;
      viewDesc.arrayLayerCount= desc.arrayLayerCount == 0 ? WGPU_ARRAY_LAYER_COUNT_UNDEFINED
                                                          : desc.arrayLayerCount;
      // Aspect
      bool isDepth = (texRec->format == Format::Depth16Unorm ||
                      texRec->format == Format::Depth24Plus ||
                      texRec->format == Format::Depth32Float);
      viewDesc.aspect = isDepth ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_All;

      WGPUTextureView view = wgpuTextureCreateView(texRec->texture, &viewDesc);
      if (!view) return {};

      WGPUTextureViewRecord rec{};
      rec.view = view;
      rec.ownerTexture = texture;

      std::lock_guard lock(mutex_);
      return TextureViewHandle{textureViews_.allocate(std::move(rec))};
#else
      (void)texture; (void)desc;
      WGPUTextureViewRecord rec{};
      rec.ownerTexture = texture;
      std::lock_guard lock(mutex_);
      return TextureViewHandle{textureViews_.allocate(std::move(rec))};
#endif
    }

    SamplerHandle createSampler(const SamplerDesc& desc) override {
#ifdef ZS_GPU_WEBGPU_IMPL
      WGPUSamplerDescriptor wgpuDesc{};
      wgpuDesc.addressModeU  = static_cast<WGPUAddressMode>(wgpu_map::toWGPU(desc.addressModeU));
      wgpuDesc.addressModeV  = static_cast<WGPUAddressMode>(wgpu_map::toWGPU(desc.addressModeV));
      wgpuDesc.addressModeW  = static_cast<WGPUAddressMode>(wgpu_map::toWGPU(desc.addressModeW));
      wgpuDesc.magFilter     = static_cast<WGPUFilterMode>(wgpu_map::toWGPU(desc.magFilter));
      wgpuDesc.minFilter     = static_cast<WGPUFilterMode>(wgpu_map::toWGPU(desc.minFilter));
      wgpuDesc.mipmapFilter  = static_cast<WGPUMipmapFilterMode>(wgpu_map::toWGPU(desc.mipmapFilter));
      wgpuDesc.lodMinClamp   = desc.lodMinClamp;
      wgpuDesc.lodMaxClamp   = desc.lodMaxClamp;
      wgpuDesc.maxAnisotropy = desc.maxAnisotropy;
      if (desc.compare != CompareOp::Always) {
        wgpuDesc.compare = static_cast<WGPUCompareFunction>(wgpu_map::toWGPU(desc.compare));
      }

      WGPUSampler samp = wgpuDeviceCreateSampler(device_, &wgpuDesc);
      if (!samp) return {};

      WGPUSamplerRecord rec{};
      rec.sampler = samp;
      std::lock_guard lock(mutex_);
      return SamplerHandle{samplers_.allocate(std::move(rec))};
#else
      (void)desc;
      WGPUSamplerRecord rec{};
      std::lock_guard lock(mutex_);
      return SamplerHandle{samplers_.allocate(std::move(rec))};
#endif
    }

    ShaderModuleHandle createShaderModule(const ShaderModuleDesc& desc) override {
#ifdef ZS_GPU_WEBGPU_IMPL
      WGPUShaderModuleDescriptor moduleDesc{};

      // WebGPU prefers WGSL; SPIR-V available via Dawn extension
      WGPUShaderModuleWGSLDescriptor wgslDesc{};
      WGPUShaderModuleSPIRVDescriptor spirvDesc{};

      if (!desc.wgsl.empty()) {
        wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
        wgslDesc.code = desc.wgsl.c_str();
        moduleDesc.nextInChain = &wgslDesc.chain;
      } else if (!desc.spirv.empty()) {
        // SPIR-V path (Dawn extension, not standard WebGPU)
        spirvDesc.chain.sType = WGPUSType_ShaderModuleSPIRVDescriptor;
        spirvDesc.code     = desc.spirv.data();
        spirvDesc.codeSize = static_cast<uint32_t>(desc.spirv.size());
        moduleDesc.nextInChain = &spirvDesc.chain;
      }

      WGPUShaderModule mod = wgpuDeviceCreateShaderModule(device_, &moduleDesc);
      if (!mod) return {};

      WGPUShaderModuleRecord rec{};
      rec.module     = mod;
      rec.entryPoint = desc.entryPoint;
      std::lock_guard lock(mutex_);
      return ShaderModuleHandle{shaderModules_.allocate(std::move(rec))};
#else
      WGPUShaderModuleRecord rec{};
      rec.entryPoint = desc.entryPoint;
      std::lock_guard lock(mutex_);
      return ShaderModuleHandle{shaderModules_.allocate(std::move(rec))};
#endif
    }

    // =====================================================================
    // Bind groups (1:1 mapping to WebGPU)
    // =====================================================================

    BindGroupLayoutHandle createBindGroupLayout(
        const BindGroupLayoutDesc& desc) override {
#ifdef ZS_GPU_WEBGPU_IMPL
      std::vector<WGPUBindGroupLayoutEntry> entries;
      entries.reserve(desc.entries.size());

      for (auto& e : desc.entries) {
        WGPUBindGroupLayoutEntry wgpuEntry{};
        wgpuEntry.binding    = e.binding;
        wgpuEntry.visibility = wgpu_map::toWGPU(e.visibility);

        switch (e.type) {
          case BindingType::UniformBuffer:
            wgpuEntry.buffer.type           = WGPUBufferBindingType_Uniform;
            wgpuEntry.buffer.hasDynamicOffset = e.hasDynamicOffset;
            break;
          case BindingType::StorageBuffer:
            wgpuEntry.buffer.type           = WGPUBufferBindingType_Storage;
            wgpuEntry.buffer.hasDynamicOffset = e.hasDynamicOffset;
            break;
          case BindingType::StorageBufferReadOnly:
            wgpuEntry.buffer.type           = WGPUBufferBindingType_ReadOnlyStorage;
            break;
          case BindingType::SampledTexture:
            wgpuEntry.texture.sampleType    = WGPUTextureSampleType_Float;
            wgpuEntry.texture.viewDimension = WGPUTextureViewDimension_2D;
            break;
          case BindingType::StorageTexture:
            wgpuEntry.storageTexture.access        = WGPUStorageTextureAccess_WriteOnly;
            wgpuEntry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
            break;
          case BindingType::Sampler:
            wgpuEntry.sampler.type = WGPUSamplerBindingType_Filtering;
            break;
          default: break;
        }
        entries.push_back(wgpuEntry);
      }

      WGPUBindGroupLayoutDescriptor layoutDesc{};
      layoutDesc.entryCount = static_cast<uint32_t>(entries.size());
      layoutDesc.entries    = entries.data();

      WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device_, &layoutDesc);
      if (!layout) return {};

      WGPUBindGroupLayoutRecord rec{};
      rec.layout  = layout;
      rec.entries = desc.entries;
      std::lock_guard lock(mutex_);
      return BindGroupLayoutHandle{bindGroupLayouts_.allocate(std::move(rec))};
#else
      WGPUBindGroupLayoutRecord rec{};
      rec.entries = desc.entries;
      std::lock_guard lock(mutex_);
      return BindGroupLayoutHandle{bindGroupLayouts_.allocate(std::move(rec))};
#endif
    }

    BindGroupHandle createBindGroup(const BindGroupDesc& desc) override {
#ifdef ZS_GPU_WEBGPU_IMPL
      auto* layoutRec = bindGroupLayouts_.get(desc.layout.id);
      if (!layoutRec) return {};

      std::vector<WGPUBindGroupEntry> entries;
      for (auto& b : desc.buffers) {
        auto* bufRec = buffers_.get(b.buffer.id);
        if (!bufRec) continue;
        WGPUBindGroupEntry e{};
        e.binding = b.binding;
        e.buffer  = bufRec->buffer;
        e.offset  = b.offset;
        e.size    = b.size == 0 ? bufRec->size - b.offset : b.size;
        entries.push_back(e);
      }
      for (auto& t : desc.textures) {
        auto* viewRec = textureViews_.get(t.textureView.id);
        if (!viewRec) continue;
        WGPUBindGroupEntry e{};
        e.binding     = t.binding;
        e.textureView = viewRec->view;
        entries.push_back(e);
      }
      for (auto& s : desc.samplers) {
        auto* sampRec = samplers_.get(s.sampler.id);
        if (!sampRec) continue;
        WGPUBindGroupEntry e{};
        e.binding = s.binding;
        e.sampler = sampRec->sampler;
        entries.push_back(e);
      }

      WGPUBindGroupDescriptor groupDesc{};
      groupDesc.layout     = layoutRec->layout;
      groupDesc.entryCount = static_cast<uint32_t>(entries.size());
      groupDesc.entries    = entries.data();

      WGPUBindGroup group = wgpuDeviceCreateBindGroup(device_, &groupDesc);
      if (!group) return {};

      WGPUBindGroupRecord rec{};
      rec.group        = group;
      rec.layoutHandle = desc.layout;
      std::lock_guard lock(mutex_);
      return BindGroupHandle{bindGroups_.allocate(std::move(rec))};
#else
      WGPUBindGroupRecord rec{};
      rec.layoutHandle = desc.layout;
      std::lock_guard lock(mutex_);
      return BindGroupHandle{bindGroups_.allocate(std::move(rec))};
#endif
    }

    // =====================================================================
    // Pipelines
    // =====================================================================

    RenderPipelineHandle createRenderPipeline(
        const RenderPipelineDesc& desc,
        ShaderModuleHandle vertexShader,
        ShaderModuleHandle fragmentShader,
        std::span<const BindGroupLayoutHandle> bindGroupLayouts) override {
      // WebGPU render pipelines use format signatures directly (no VkRenderPass).
      // This is a 1:1 mapping from our RenderPipelineDesc.
#ifdef ZS_GPU_WEBGPU_IMPL
      auto* vsRec = shaderModules_.get(vertexShader.id);
      auto* fsRec = shaderModules_.get(fragmentShader.id);
      if (!vsRec || !fsRec) return {};

      // Build pipeline layout
      std::vector<WGPUBindGroupLayout> layouts;
      for (auto h : bindGroupLayouts) {
        auto* r = bindGroupLayouts_.get(h.id);
        if (r) layouts.push_back(r->layout);
      }
      WGPUPipelineLayoutDescriptor plDesc{};
      plDesc.bindGroupLayoutCount = static_cast<uint32_t>(layouts.size());
      plDesc.bindGroupLayouts     = layouts.data();
      WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device_, &plDesc);

      // Vertex state
      std::vector<WGPUVertexAttribute> allAttribs;
      std::vector<WGPUVertexBufferLayout> vertexBuffers;
      for (auto& vb : desc.vertexBuffers) {
        WGPUVertexBufferLayout wgpuVB{};
        wgpuVB.arrayStride = vb.stride;
        wgpuVB.stepMode    = (vb.stepMode == VertexStepMode::Instance)
                                 ? WGPUVertexStepMode_Instance
                                 : WGPUVertexStepMode_Vertex;
        size_t baseIdx = allAttribs.size();
        for (auto& a : vb.attributes) {
          WGPUVertexAttribute wa{};
          wa.format         = static_cast<WGPUVertexFormat>(wgpu_map::toWGPU(a.format));
          wa.offset         = a.offset;
          wa.shaderLocation = a.shaderLocation;
          allAttribs.push_back(wa);
        }
        wgpuVB.attributeCount = static_cast<uint32_t>(allAttribs.size() - baseIdx);
        wgpuVB.attributes     = allAttribs.data() + baseIdx;
        vertexBuffers.push_back(wgpuVB);
      }

      // Color targets
      std::vector<WGPUColorTargetState> colorTargets;
      std::vector<WGPUBlendState> blendStates;
      blendStates.resize(desc.colorTargets.size());
      for (size_t i = 0; i < desc.colorTargets.size(); ++i) {
        auto& ct = desc.colorTargets[i];
        WGPUColorTargetState wgpuCT{};
        wgpuCT.format    = static_cast<WGPUTextureFormat>(wgpu_map::toWGPU(ct.format));
        wgpuCT.writeMask = wgpu_map::toWGPU(ct.writeMask);
        if (ct.blendEnabled) {
          auto& bs       = blendStates[i];
          bs.color.srcFactor = static_cast<WGPUBlendFactor>(wgpu_map::toWGPU(ct.blend.color.srcFactor));
          bs.color.dstFactor = static_cast<WGPUBlendFactor>(wgpu_map::toWGPU(ct.blend.color.dstFactor));
          bs.color.operation = static_cast<WGPUBlendOperation>(wgpu_map::toWGPU(ct.blend.color.operation));
          bs.alpha.srcFactor = static_cast<WGPUBlendFactor>(wgpu_map::toWGPU(ct.blend.alpha.srcFactor));
          bs.alpha.dstFactor = static_cast<WGPUBlendFactor>(wgpu_map::toWGPU(ct.blend.alpha.dstFactor));
          bs.alpha.operation = static_cast<WGPUBlendOperation>(wgpu_map::toWGPU(ct.blend.alpha.operation));
          wgpuCT.blend = &bs;
        }
        colorTargets.push_back(wgpuCT);
      }

      // Depth stencil
      WGPUDepthStencilState depthStencil{};
      bool hasDepth = (desc.depthStencil.format != Format::Undefined);
      if (hasDepth) {
        depthStencil.format              = static_cast<WGPUTextureFormat>(wgpu_map::toWGPU(desc.depthStencil.format));
        depthStencil.depthWriteEnabled   = desc.depthStencil.depthWriteEnabled;
        depthStencil.depthCompare        = static_cast<WGPUCompareFunction>(wgpu_map::toWGPU(desc.depthStencil.depthCompare));
        depthStencil.stencilReadMask     = desc.depthStencil.stencilReadMask;
        depthStencil.stencilWriteMask    = desc.depthStencil.stencilWriteMask;
        // Front face stencil
        depthStencil.stencilFront.compare    = static_cast<WGPUCompareFunction>(wgpu_map::toWGPU(desc.depthStencil.stencilFront.compare));
        depthStencil.stencilFront.failOp     = static_cast<WGPUStencilOperation>(wgpu_map::toWGPU(desc.depthStencil.stencilFront.failOp));
        depthStencil.stencilFront.depthFailOp= static_cast<WGPUStencilOperation>(wgpu_map::toWGPU(desc.depthStencil.stencilFront.depthFailOp));
        depthStencil.stencilFront.passOp     = static_cast<WGPUStencilOperation>(wgpu_map::toWGPU(desc.depthStencil.stencilFront.passOp));
        // Back face stencil
        depthStencil.stencilBack.compare     = static_cast<WGPUCompareFunction>(wgpu_map::toWGPU(desc.depthStencil.stencilBack.compare));
        depthStencil.stencilBack.failOp      = static_cast<WGPUStencilOperation>(wgpu_map::toWGPU(desc.depthStencil.stencilBack.failOp));
        depthStencil.stencilBack.depthFailOp = static_cast<WGPUStencilOperation>(wgpu_map::toWGPU(desc.depthStencil.stencilBack.depthFailOp));
        depthStencil.stencilBack.passOp      = static_cast<WGPUStencilOperation>(wgpu_map::toWGPU(desc.depthStencil.stencilBack.passOp));
      }

      // Assemble pipeline descriptor
      WGPURenderPipelineDescriptor pipelineDesc{};
      pipelineDesc.layout = pipelineLayout;

      // Vertex stage
      pipelineDesc.vertex.module      = vsRec->module;
      pipelineDesc.vertex.entryPoint  = vsRec->entryPoint.c_str();
      pipelineDesc.vertex.bufferCount = static_cast<uint32_t>(vertexBuffers.size());
      pipelineDesc.vertex.buffers     = vertexBuffers.data();

      // Fragment stage
      WGPUFragmentState fragmentState{};
      fragmentState.module      = fsRec->module;
      fragmentState.entryPoint  = fsRec->entryPoint.c_str();
      fragmentState.targetCount = static_cast<uint32_t>(colorTargets.size());
      fragmentState.targets     = colorTargets.data();
      pipelineDesc.fragment     = &fragmentState;

      // Primitive state
      pipelineDesc.primitive.topology  = static_cast<WGPUPrimitiveTopology>(wgpu_map::toWGPU(desc.topology));
      pipelineDesc.primitive.frontFace = static_cast<WGPUFrontFace>(wgpu_map::toWGPU(desc.frontFace));
      pipelineDesc.primitive.cullMode  = static_cast<WGPUCullMode>(wgpu_map::toWGPU(desc.cullMode));
      if (desc.topology == Topology::TriangleStrip || desc.topology == Topology::LineStrip) {
        pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Uint32;
      }

      // Depth stencil
      if (hasDepth) pipelineDesc.depthStencil = &depthStencil;

      // Multisample
      pipelineDesc.multisample.count = static_cast<uint32_t>(desc.sampleCount);
      pipelineDesc.multisample.mask  = 0xFFFFFFFF;

      WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
      if (!pipeline) {
        wgpuPipelineLayoutRelease(pipelineLayout);
        return {};
      }

      WGPURenderPipelineRecord rec{};
      rec.pipeline       = pipeline;
      rec.pipelineLayout = pipelineLayout;
      std::lock_guard lock(mutex_);
      return RenderPipelineHandle{renderPipelines_.allocate(std::move(rec))};
#else
      (void)desc; (void)vertexShader; (void)fragmentShader; (void)bindGroupLayouts;
      WGPURenderPipelineRecord rec{};
      std::lock_guard lock(mutex_);
      return RenderPipelineHandle{renderPipelines_.allocate(std::move(rec))};
#endif
    }

    ComputePipelineHandle createComputePipeline(
        const ComputePipelineDesc& desc,
        ShaderModuleHandle computeShader,
        std::span<const BindGroupLayoutHandle> bindGroupLayouts) override {
#ifdef ZS_GPU_WEBGPU_IMPL
      auto* csRec = shaderModules_.get(computeShader.id);
      if (!csRec) return {};

      std::vector<WGPUBindGroupLayout> layouts;
      for (auto h : bindGroupLayouts) {
        auto* r = bindGroupLayouts_.get(h.id);
        if (r) layouts.push_back(r->layout);
      }
      WGPUPipelineLayoutDescriptor plDesc{};
      plDesc.bindGroupLayoutCount = static_cast<uint32_t>(layouts.size());
      plDesc.bindGroupLayouts     = layouts.data();
      WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device_, &plDesc);

      WGPUComputePipelineDescriptor cpDesc{};
      cpDesc.layout                 = pipelineLayout;
      cpDesc.compute.module         = csRec->module;
      cpDesc.compute.entryPoint     = csRec->entryPoint.c_str();

      WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device_, &cpDesc);
      if (!pipeline) {
        wgpuPipelineLayoutRelease(pipelineLayout);
        return {};
      }

      WGPUComputePipelineRecord rec{};
      rec.pipeline       = pipeline;
      rec.pipelineLayout = pipelineLayout;
      std::lock_guard lock(mutex_);
      return ComputePipelineHandle{computePipelines_.allocate(std::move(rec))};
#else
      (void)desc; (void)computeShader; (void)bindGroupLayouts;
      WGPUComputePipelineRecord rec{};
      std::lock_guard lock(mutex_);
      return ComputePipelineHandle{computePipelines_.allocate(std::move(rec))};
#endif
    }

    // =====================================================================
    // Command encoding
    // =====================================================================

    std::unique_ptr<CommandEncoder> createCommandEncoder(
        std::string_view label) override {
#ifdef ZS_GPU_WEBGPU_IMPL
      WGPUCommandEncoderDescriptor encDesc{};
      std::string labelStr(label);
      if (!labelStr.empty()) encDesc.label = labelStr.c_str();
      WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device_, &encDesc);
      if (!enc) return nullptr;
      return std::make_unique<WebGPUCommandEncoderImpl>(*this, enc);
#else
      (void)label;
      return std::make_unique<WebGPUCommandEncoderImpl>(*this, nullptr);
#endif
    }

    // =====================================================================
    // Submission
    // =====================================================================

    void submit(CommandBufferHandle cmdBuf) override {
#ifdef ZS_GPU_WEBGPU_IMPL
      auto* rec = commandBuffers_.get(cmdBuf.id);
      if (!rec || !rec->cmdBuf) return;
      wgpuQueueSubmit(queue_, 1, &rec->cmdBuf);
      commandBuffers_.release(cmdBuf.id);
#else
      (void)cmdBuf;
#endif
    }

    void submit(std::span<const CommandBufferHandle> cmdBufs) override {
#ifdef ZS_GPU_WEBGPU_IMPL
      std::vector<WGPUCommandBuffer> bufs;
      for (auto h : cmdBufs) {
        auto* rec = commandBuffers_.get(h.id);
        if (rec && rec->cmdBuf) bufs.push_back(rec->cmdBuf);
      }
      if (!bufs.empty()) {
        wgpuQueueSubmit(queue_, static_cast<uint32_t>(bufs.size()), bufs.data());
      }
      for (auto h : cmdBufs) commandBuffers_.release(h.id);
#else
      (void)cmdBufs;
#endif
    }

    // =====================================================================
    // Buffer operations
    // =====================================================================

    void* mapBuffer(BufferHandle buffer, uint64_t offset, uint64_t size) override {
      std::lock_guard lock(mutex_);
      auto* rec = buffers_.get(buffer.id);
      if (!rec) return nullptr;
      if (rec->mapped) return static_cast<char*>(rec->mapped) + offset;

#ifdef ZS_GPU_WEBGPU_IMPL
      // WebGPU async mapping with synchronous wait via device poll
      bool done = false;
      auto mapSize = (size == 0) ? rec->size - offset : size;
      wgpuBufferMapAsync(rec->buffer, WGPUMapMode_Read | WGPUMapMode_Write,
                         offset, mapSize,
                         [](WGPUBufferMapAsyncStatus status, void* userdata) {
                           *(bool*)userdata = (status == WGPUBufferMapAsyncStatus_Success);
                         }, &done);
      // Poll until mapping completes
      while (!done) {
        wgpuDevicePoll(device_, false, nullptr);
      }
      rec->mapped = const_cast<void*>(wgpuBufferGetConstMappedRange(rec->buffer, offset, mapSize));
      return rec->mapped;
#else
      return nullptr;
#endif
    }

    void unmapBuffer(BufferHandle buffer) override {
      std::lock_guard lock(mutex_);
      auto* rec = buffers_.get(buffer.id);
      if (!rec || !rec->mapped) return;
#ifdef ZS_GPU_WEBGPU_IMPL
      wgpuBufferUnmap(rec->buffer);
#endif
      rec->mapped = nullptr;
    }

    void writeBuffer(BufferHandle buffer, uint64_t offset,
                     const void* data, uint64_t size) override {
#ifdef ZS_GPU_WEBGPU_IMPL
      // wgpuQueueWriteBuffer is the preferred path in WebGPU
      auto* rec = buffers_.get(buffer.id);
      if (!rec) return;
      wgpuQueueWriteBuffer(queue_, rec->buffer, offset, data, size);
#else
      (void)buffer; (void)offset; (void)data; (void)size;
#endif
    }

    // =====================================================================
    // Synchronization
    // =====================================================================

    void waitIdle() override {
#ifdef ZS_GPU_WEBGPU_IMPL
      // Dawn: wgpuDevicePoll(device_, true, nullptr) blocks until idle
      // wgpu-native: similar
      // Emscripten: not possible to block
      wgpuDevicePoll(device_, true, nullptr);
#endif
    }

    // =====================================================================
    // Destruction (WebGPU uses release/drop pattern)
    // =====================================================================

    void destroyBuffer(BufferHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = buffers_.get(h.id);
      if (!rec) return;
#ifdef ZS_GPU_WEBGPU_IMPL
      wgpuBufferDestroy(rec->buffer);
      wgpuBufferRelease(rec->buffer);
#endif
      buffers_.release(h.id);
    }

    void destroyTexture(TextureHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = textures_.get(h.id);
      if (!rec) return;
#ifdef ZS_GPU_WEBGPU_IMPL
      if (rec->defaultView) wgpuTextureViewRelease(rec->defaultView);
      wgpuTextureDestroy(rec->texture);
      wgpuTextureRelease(rec->texture);
#endif
      textures_.release(h.id);
    }

    void destroyTextureView(TextureViewHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = textureViews_.get(h.id);
      if (!rec) return;
#ifdef ZS_GPU_WEBGPU_IMPL
      wgpuTextureViewRelease(rec->view);
#endif
      textureViews_.release(h.id);
    }

    void destroySampler(SamplerHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = samplers_.get(h.id);
      if (!rec) return;
#ifdef ZS_GPU_WEBGPU_IMPL
      wgpuSamplerRelease(rec->sampler);
#endif
      samplers_.release(h.id);
    }

    void destroyShaderModule(ShaderModuleHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = shaderModules_.get(h.id);
      if (!rec) return;
#ifdef ZS_GPU_WEBGPU_IMPL
      wgpuShaderModuleRelease(rec->module);
#endif
      shaderModules_.release(h.id);
    }

    void destroyBindGroupLayout(BindGroupLayoutHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = bindGroupLayouts_.get(h.id);
      if (!rec) return;
#ifdef ZS_GPU_WEBGPU_IMPL
      wgpuBindGroupLayoutRelease(rec->layout);
#endif
      bindGroupLayouts_.release(h.id);
    }

    void destroyBindGroup(BindGroupHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = bindGroups_.get(h.id);
      if (!rec) return;
#ifdef ZS_GPU_WEBGPU_IMPL
      wgpuBindGroupRelease(rec->group);
#endif
      bindGroups_.release(h.id);
    }

    void destroyRenderPipeline(RenderPipelineHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = renderPipelines_.get(h.id);
      if (!rec) return;
#ifdef ZS_GPU_WEBGPU_IMPL
      if (rec->pipeline) wgpuRenderPipelineRelease(rec->pipeline);
      if (rec->pipelineLayout) wgpuPipelineLayoutRelease(rec->pipelineLayout);
#endif
      renderPipelines_.release(h.id);
    }

    void destroyComputePipeline(ComputePipelineHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = computePipelines_.get(h.id);
      if (!rec) return;
#ifdef ZS_GPU_WEBGPU_IMPL
      if (rec->pipeline) wgpuComputePipelineRelease(rec->pipeline);
      if (rec->pipelineLayout) wgpuPipelineLayoutRelease(rec->pipelineLayout);
#endif
      computePipelines_.release(h.id);
    }

    // =====================================================================
    // Internal accessors
    // =====================================================================
    WGPUDevice wgpuDevice() const { return device_; }
    WGPUQueue  wgpuQueue()  const { return queue_; }

    WGPUBufferRecord*           getBuffer(BufferHandle h)           { return buffers_.get(h.id); }
    WGPUTextureRecord*          getTexture(TextureHandle h)         { return textures_.get(h.id); }
    WGPUTextureViewRecord*      getTextureView(TextureViewHandle h) { return textureViews_.get(h.id); }
    WGPUSamplerRecord*          getSampler(SamplerHandle h)         { return samplers_.get(h.id); }
    WGPURenderPipelineRecord*   getRenderPipeline(RenderPipelineHandle h) { return renderPipelines_.get(h.id); }
    WGPUComputePipelineRecord*  getComputePipeline(ComputePipelineHandle h) { return computePipelines_.get(h.id); }
    WGPUBindGroupRecord*        getBindGroup(BindGroupHandle h)     { return bindGroups_.get(h.id); }
    WGPUBindGroupLayoutRecord*  getBindGroupLayout(BindGroupLayoutHandle h) { return bindGroupLayouts_.get(h.id); }

  private:
    WGPUDevice  device_ = nullptr;
    WGPUQueue   queue_  = nullptr;
    std::string deviceName_ = "WebGPU Device";
    std::mutex  mutex_;

    PushConstantEmulation pushConstants_;

    // Resource pools
    WGPUResourcePool<WGPUBufferRecord>            buffers_;
    WGPUResourcePool<WGPUTextureRecord>           textures_;
    WGPUResourcePool<WGPUTextureViewRecord>       textureViews_;
    WGPUResourcePool<WGPUSamplerRecord>           samplers_;
    WGPUResourcePool<WGPUShaderModuleRecord>      shaderModules_;
    WGPUResourcePool<WGPUBindGroupLayoutRecord>   bindGroupLayouts_;
    WGPUResourcePool<WGPUBindGroupRecord>         bindGroups_;
    WGPUResourcePool<WGPURenderPipelineRecord>    renderPipelines_;
    WGPUResourcePool<WGPUComputePipelineRecord>   computePipelines_;
    WGPUResourcePool<WGPUCommandBufferRecord>     commandBuffers_;
  };

  // =========================================================================
  // Encoder implementations
  // =========================================================================

  // -- WebGPURenderPassEncoderImpl --
  inline void WebGPURenderPassEncoderImpl::setPipeline(RenderPipelineHandle pipeline) {
#ifdef ZS_GPU_WEBGPU_IMPL
    auto* rec = dev_.getRenderPipeline(pipeline);
    if (!rec || !rec->pipeline) return;
    wgpuRenderPassEncoderSetPipeline(enc_, rec->pipeline);
    currentPipeline_ = rec->pipeline;
    currentLayout_   = rec->pipelineLayout;
#else
    (void)pipeline;
#endif
  }

  inline void WebGPURenderPassEncoderImpl::setBindGroup(
      uint32_t groupIndex, BindGroupHandle group) {
#ifdef ZS_GPU_WEBGPU_IMPL
    auto* rec = dev_.getBindGroup(group);
    if (!rec) return;
    wgpuRenderPassEncoderSetBindGroup(enc_, groupIndex, rec->group, 0, nullptr);
#else
    (void)groupIndex; (void)group;
#endif
  }

  inline void WebGPURenderPassEncoderImpl::setVertexBuffer(
      uint32_t slot, BufferHandle buffer, uint64_t offset, uint64_t size) {
#ifdef ZS_GPU_WEBGPU_IMPL
    auto* rec = dev_.getBuffer(buffer);
    if (!rec) return;
    wgpuRenderPassEncoderSetVertexBuffer(enc_, slot, rec->buffer, offset,
        size == 0 ? rec->size - offset : size);
#else
    (void)slot; (void)buffer; (void)offset; (void)size;
#endif
  }

  inline void WebGPURenderPassEncoderImpl::setIndexBuffer(
      BufferHandle buffer, IndexFormat format, uint64_t offset, uint64_t size) {
#ifdef ZS_GPU_WEBGPU_IMPL
    auto* rec = dev_.getBuffer(buffer);
    if (!rec) return;
    wgpuRenderPassEncoderSetIndexBuffer(enc_, rec->buffer,
        static_cast<WGPUIndexFormat>(wgpu_map::toWGPU(format)),
        offset, size == 0 ? rec->size - offset : size);
#else
    (void)buffer; (void)format; (void)offset; (void)size;
#endif
  }

  inline void WebGPURenderPassEncoderImpl::setViewport(const Viewport& vp) {
#ifdef ZS_GPU_WEBGPU_IMPL
    wgpuRenderPassEncoderSetViewport(enc_, vp.x, vp.y, vp.width, vp.height,
                                     vp.minDepth, vp.maxDepth);
#else
    (void)vp;
#endif
  }

  inline void WebGPURenderPassEncoderImpl::setScissor(const Scissor& sc) {
#ifdef ZS_GPU_WEBGPU_IMPL
    wgpuRenderPassEncoderSetScissorRect(enc_, sc.x, sc.y, sc.width, sc.height);
#else
    (void)sc;
#endif
  }

  inline void WebGPURenderPassEncoderImpl::setPushConstants(
      ShaderStage /*stages*/, uint32_t offset, uint32_t size, const void* data) {
    // WebGPU has no push constants. Emulate via uniform buffer write + bind.
#ifdef ZS_GPU_WEBGPU_IMPL
    // Write to the emulation buffer via queue write
    auto& pc = dev_.pushConstants_;
    std::memcpy(pc.data + offset, data, size);
    pc.dirty = true;
    // Actual buffer write + bind deferred to draw time
#else
    (void)offset; (void)size; (void)data;
#endif
  }

  inline void WebGPURenderPassEncoderImpl::draw(
      uint32_t vertexCount, uint32_t instanceCount,
      uint32_t firstVertex, uint32_t firstInstance) {
#ifdef ZS_GPU_WEBGPU_IMPL
    wgpuRenderPassEncoderDraw(enc_, vertexCount, instanceCount,
                              firstVertex, firstInstance);
#else
    (void)vertexCount; (void)instanceCount; (void)firstVertex; (void)firstInstance;
#endif
  }

  inline void WebGPURenderPassEncoderImpl::drawIndexed(
      uint32_t indexCount, uint32_t instanceCount,
      uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
#ifdef ZS_GPU_WEBGPU_IMPL
    wgpuRenderPassEncoderDrawIndexed(enc_, indexCount, instanceCount,
                                     firstIndex, vertexOffset, firstInstance);
#else
    (void)indexCount; (void)instanceCount; (void)firstIndex;
    (void)vertexOffset; (void)firstInstance;
#endif
  }

  inline void WebGPURenderPassEncoderImpl::drawIndirect(
      BufferHandle indirectBuffer, uint64_t indirectOffset) {
#ifdef ZS_GPU_WEBGPU_IMPL
    auto* rec = dev_.getBuffer(indirectBuffer);
    if (!rec) return;
    wgpuRenderPassEncoderDrawIndirect(enc_, rec->buffer, indirectOffset);
#else
    (void)indirectBuffer; (void)indirectOffset;
#endif
  }

  inline void WebGPURenderPassEncoderImpl::drawIndexedIndirect(
      BufferHandle indirectBuffer, uint64_t indirectOffset) {
#ifdef ZS_GPU_WEBGPU_IMPL
    auto* rec = dev_.getBuffer(indirectBuffer);
    if (!rec) return;
    wgpuRenderPassEncoderDrawIndexedIndirect(enc_, rec->buffer, indirectOffset);
#else
    (void)indirectBuffer; (void)indirectOffset;
#endif
  }

  inline void WebGPURenderPassEncoderImpl::end() {
#ifdef ZS_GPU_WEBGPU_IMPL
    wgpuRenderPassEncoderEnd(enc_);
    wgpuRenderPassEncoderRelease(enc_);
#endif
  }

  // -- WebGPUComputePassEncoderImpl --
  inline void WebGPUComputePassEncoderImpl::setPipeline(ComputePipelineHandle pipeline) {
#ifdef ZS_GPU_WEBGPU_IMPL
    auto* rec = dev_.getComputePipeline(pipeline);
    if (!rec || !rec->pipeline) return;
    wgpuComputePassEncoderSetPipeline(enc_, rec->pipeline);
#else
    (void)pipeline;
#endif
  }

  inline void WebGPUComputePassEncoderImpl::setBindGroup(
      uint32_t groupIndex, BindGroupHandle group) {
#ifdef ZS_GPU_WEBGPU_IMPL
    auto* rec = dev_.getBindGroup(group);
    if (!rec) return;
    wgpuComputePassEncoderSetBindGroup(enc_, groupIndex, rec->group, 0, nullptr);
#else
    (void)groupIndex; (void)group;
#endif
  }

  inline void WebGPUComputePassEncoderImpl::setPushConstants(
      ShaderStage, uint32_t offset, uint32_t size, const void* data) {
#ifdef ZS_GPU_WEBGPU_IMPL
    auto& pc = dev_.pushConstants_;
    std::memcpy(pc.data + offset, data, size);
    pc.dirty = true;
#else
    (void)offset; (void)size; (void)data;
#endif
  }

  inline void WebGPUComputePassEncoderImpl::dispatch(uint32_t x, uint32_t y, uint32_t z) {
#ifdef ZS_GPU_WEBGPU_IMPL
    wgpuComputePassEncoderDispatchWorkgroups(enc_, x, y, z);
#else
    (void)x; (void)y; (void)z;
#endif
  }

  inline void WebGPUComputePassEncoderImpl::dispatchIndirect(
      BufferHandle buffer, uint64_t offset) {
#ifdef ZS_GPU_WEBGPU_IMPL
    auto* rec = dev_.getBuffer(buffer);
    if (!rec) return;
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(enc_, rec->buffer, offset);
#else
    (void)buffer; (void)offset;
#endif
  }

  inline void WebGPUComputePassEncoderImpl::end() {
#ifdef ZS_GPU_WEBGPU_IMPL
    wgpuComputePassEncoderEnd(enc_);
    wgpuComputePassEncoderRelease(enc_);
#endif
  }

  // -- WebGPUCommandEncoderImpl --
  inline RenderPassEncoder* WebGPUCommandEncoderImpl::beginRenderPass(
      const RenderPassBeginDesc& desc) {
#ifdef ZS_GPU_WEBGPU_IMPL
    // Build color attachments
    std::vector<WGPURenderPassColorAttachment> colorAtts;
    for (auto& ca : desc.colorAttachments) {
      WGPURenderPassColorAttachment att{};
      auto* viewRec = dev_.getTextureView(ca.view);
      if (!viewRec) continue;
      att.view       = viewRec->view;
      att.loadOp     = static_cast<WGPULoadOp>(wgpu_map::toWGPU(ca.loadOp));
      att.storeOp    = static_cast<WGPUStoreOp>(wgpu_map::toWGPU(ca.storeOp));
      att.clearValue = {ca.clearColor[0], ca.clearColor[1],
                        ca.clearColor[2], ca.clearColor[3]};
      colorAtts.push_back(att);
    }

    WGPURenderPassDescriptor rpDesc{};
    rpDesc.colorAttachmentCount = static_cast<uint32_t>(colorAtts.size());
    rpDesc.colorAttachments     = colorAtts.data();

    // Depth attachment
    WGPURenderPassDepthStencilAttachment depthAtt{};
    if (desc.hasDepthStencil) {
      auto* dvRec = dev_.getTextureView(desc.depthStencilAttachment.view);
      if (dvRec) {
        depthAtt.view              = dvRec->view;
        depthAtt.depthLoadOp       = static_cast<WGPULoadOp>(
            wgpu_map::toWGPU(desc.depthStencilAttachment.depthLoadOp));
        depthAtt.depthStoreOp      = static_cast<WGPUStoreOp>(
            wgpu_map::toWGPU(desc.depthStencilAttachment.depthStoreOp));
        depthAtt.depthClearValue   = desc.depthStencilAttachment.clearDepth;
        depthAtt.stencilLoadOp     = static_cast<WGPULoadOp>(
            wgpu_map::toWGPU(desc.depthStencilAttachment.stencilLoadOp));
        depthAtt.stencilStoreOp    = static_cast<WGPUStoreOp>(
            wgpu_map::toWGPU(desc.depthStencilAttachment.stencilStoreOp));
        depthAtt.stencilClearValue = desc.depthStencilAttachment.clearStencil;
        rpDesc.depthStencilAttachment = &depthAtt;
      }
    }

    WGPURenderPassEncoder rpe = wgpuCommandEncoderBeginRenderPass(enc_, &rpDesc);
    currentRenderPass_ = std::make_unique<WebGPURenderPassEncoderImpl>(dev_, rpe);
    return currentRenderPass_.get();
#else
    (void)desc;
    currentRenderPass_ = std::make_unique<WebGPURenderPassEncoderImpl>(dev_, nullptr);
    return currentRenderPass_.get();
#endif
  }

  inline ComputePassEncoder* WebGPUCommandEncoderImpl::beginComputePass() {
#ifdef ZS_GPU_WEBGPU_IMPL
    WGPUComputePassDescriptor cpDesc{};
    WGPUComputePassEncoder cpe = wgpuCommandEncoderBeginComputePass(enc_, &cpDesc);
    currentComputePass_ = std::make_unique<WebGPUComputePassEncoderImpl>(dev_, cpe);
    return currentComputePass_.get();
#else
    currentComputePass_ = std::make_unique<WebGPUComputePassEncoderImpl>(dev_, nullptr);
    return currentComputePass_.get();
#endif
  }

  inline void WebGPUCommandEncoderImpl::copyBufferToBuffer(
      BufferHandle src, uint64_t srcOffset,
      BufferHandle dst, uint64_t dstOffset, uint64_t size) {
#ifdef ZS_GPU_WEBGPU_IMPL
    auto* srcRec = dev_.getBuffer(src);
    auto* dstRec = dev_.getBuffer(dst);
    if (!srcRec || !dstRec) return;
    wgpuCommandEncoderCopyBufferToBuffer(enc_,
        srcRec->buffer, srcOffset, dstRec->buffer, dstOffset, size);
#else
    (void)src; (void)srcOffset; (void)dst; (void)dstOffset; (void)size;
#endif
  }

  inline void WebGPUCommandEncoderImpl::copyBufferToTexture(
      BufferHandle src, uint64_t srcOffset,
      uint32_t bytesPerRow, uint32_t rowsPerImage,
      TextureHandle dst, uint32_t mipLevel, uint32_t arrayLayer,
      uint32_t width, uint32_t height, uint32_t depth) {
#ifdef ZS_GPU_WEBGPU_IMPL
    auto* bufRec = dev_.getBuffer(src);
    auto* texRec = dev_.getTexture(dst);
    if (!bufRec || !texRec) return;

    WGPUImageCopyBuffer srcCopy{};
    srcCopy.buffer             = bufRec->buffer;
    srcCopy.layout.offset      = srcOffset;
    srcCopy.layout.bytesPerRow = bytesPerRow;
    srcCopy.layout.rowsPerImage= rowsPerImage;

    WGPUImageCopyTexture dstCopy{};
    dstCopy.texture  = texRec->texture;
    dstCopy.mipLevel = mipLevel;
    dstCopy.origin   = {0, 0, arrayLayer};

    WGPUExtent3D copySize = {width, height, depth};
    wgpuCommandEncoderCopyBufferToTexture(enc_, &srcCopy, &dstCopy, &copySize);
#else
    (void)src; (void)srcOffset; (void)bytesPerRow; (void)rowsPerImage;
    (void)dst; (void)mipLevel; (void)arrayLayer;
    (void)width; (void)height; (void)depth;
#endif
  }

  inline void WebGPUCommandEncoderImpl::copyTextureToBuffer(
      TextureHandle src, uint32_t mipLevel, uint32_t arrayLayer,
      uint32_t width, uint32_t height, uint32_t depth,
      BufferHandle dst, uint64_t dstOffset,
      uint32_t bytesPerRow, uint32_t rowsPerImage) {
#ifdef ZS_GPU_WEBGPU_IMPL
    auto* texRec = dev_.getTexture(src);
    auto* bufRec = dev_.getBuffer(dst);
    if (!texRec || !bufRec) return;

    WGPUImageCopyTexture srcCopy{};
    srcCopy.texture  = texRec->texture;
    srcCopy.mipLevel = mipLevel;
    srcCopy.origin   = {0, 0, arrayLayer};

    WGPUImageCopyBuffer dstCopy{};
    dstCopy.buffer             = bufRec->buffer;
    dstCopy.layout.offset      = dstOffset;
    dstCopy.layout.bytesPerRow = bytesPerRow;
    dstCopy.layout.rowsPerImage= rowsPerImage;

    WGPUExtent3D copySize = {width, height, depth};
    wgpuCommandEncoderCopyTextureToBuffer(enc_, &srcCopy, &dstCopy, &copySize);
#else
    (void)src; (void)mipLevel; (void)arrayLayer;
    (void)width; (void)height; (void)depth;
    (void)dst; (void)dstOffset; (void)bytesPerRow; (void)rowsPerImage;
#endif
  }

  inline CommandBufferHandle WebGPUCommandEncoderImpl::finish() {
#ifdef ZS_GPU_WEBGPU_IMPL
    WGPUCommandBufferDescriptor finishDesc{};
    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(enc_, &finishDesc);
    wgpuCommandEncoderRelease(enc_);
    enc_ = nullptr;

    WGPUCommandBufferRecord rec{};
    rec.cmdBuf = cmdBuf;
    // Store in device's command buffer pool
    // (requires friend access or public method)
    return {};  // TODO: allocate in device pool
#else
    return {};
#endif
  }

}  // namespace zs::gpu
