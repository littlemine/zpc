// Copyright (c) zs contributors. Licensed under the MIT License.
// GpuDevice.hpp - Abstract GPU device interface.
//
// This is the central factory / context for all GPU operations. Each backend
// (Vulkan, WebGPU, DX12, Metal, OpenGL) provides a concrete implementation.
//
// Object lifetime model:
//   - The Device owns the underlying API context (instance, device, queues).
//   - Created objects (Buffer, Texture, Pipeline, etc.) are returned as
//     opaque handles. Destruction happens via explicit destroy*() calls or
//     RAII wrappers (backend-specific).
//   - Bind groups are immutable and pooled; the Device manages allocation.
//
// Threading model:
//   - Resource creation methods are thread-safe (internally synchronized).
//   - Command encoding is NOT thread-safe — each thread gets its own encoder.
//   - Submission is serialized per queue.
//
// This header defines the interface only. No implementation, no backend
// headers. Backend headers (e.g. GpuDeviceVk.hpp) include this and provide
// the concrete class.

#pragma once

#include "GpuDescriptors.hpp"
#include "GpuTypes.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace zs::gpu {

  // =========================================================================
  // Opaque handle types
  // =========================================================================
  // Each backend defines what these actually contain. At the interface level
  // they are type-safe wrappers around a uint64_t (pointer or index).
  //
  // We use a tagged-handle pattern: each handle type is distinct at compile
  // time, preventing accidental misuse (e.g. passing a BufferHandle where a
  // TextureHandle is expected).

  template <typename Tag>
  struct Handle {
    uint64_t id = 0;
    constexpr explicit operator bool() const { return id != 0; }
    constexpr bool operator==(Handle other) const { return id == other.id; }
    constexpr bool operator!=(Handle other) const { return id != other.id; }
  };

  struct BufferTag {};
  struct TextureTag {};
  struct TextureViewTag {};
  struct SamplerTag {};
  struct ShaderModuleTag {};
  struct BindGroupLayoutTag {};
  struct BindGroupTag {};
  struct RenderPipelineTag {};
  struct ComputePipelineTag {};
  struct CommandBufferTag {};

  using BufferHandle          = Handle<BufferTag>;
  using TextureHandle         = Handle<TextureTag>;
  using TextureViewHandle     = Handle<TextureViewTag>;
  using SamplerHandle         = Handle<SamplerTag>;
  using ShaderModuleHandle    = Handle<ShaderModuleTag>;
  using BindGroupLayoutHandle = Handle<BindGroupLayoutTag>;
  using BindGroupHandle       = Handle<BindGroupTag>;
  using RenderPipelineHandle  = Handle<RenderPipelineTag>;
  using ComputePipelineHandle = Handle<ComputePipelineTag>;
  using CommandBufferHandle   = Handle<CommandBufferTag>;

  // =========================================================================
  // Bind group entries (for creating bind groups with concrete resources)
  // =========================================================================
  struct BindGroupBufferEntry {
    uint32_t     binding = 0;
    BufferHandle buffer;
    uint64_t     offset  = 0;
    uint64_t     size    = 0;  // 0 = whole remaining buffer
  };

  struct BindGroupTextureEntry {
    uint32_t          binding = 0;
    TextureViewHandle textureView;
  };

  struct BindGroupSamplerEntry {
    uint32_t      binding = 0;
    SamplerHandle sampler;
  };

  struct BindGroupDesc {
    BindGroupLayoutHandle layout;
    std::vector<BindGroupBufferEntry>  buffers;
    std::vector<BindGroupTextureEntry> textures;
    std::vector<BindGroupSamplerEntry> samplers;
    std::string label;
  };

  // =========================================================================
  // Pipeline layout desc (ordered list of bind group layouts + push constants)
  // =========================================================================
  struct PipelineLayoutDesc {
    std::vector<BindGroupLayoutHandle> bindGroupLayouts;  // index = set number
    std::vector<PushConstantRange>     pushConstants;
    std::string label;
  };

  // =========================================================================
  // Render pass with concrete attachments (for command encoding)
  // =========================================================================
  struct RenderPassColorAttachment {
    TextureViewHandle view;
    TextureViewHandle resolveTarget;  // null handle = no resolve
    LoadOp     loadOp   = LoadOp::Clear;
    StoreOp    storeOp  = StoreOp::Store;
    ClearValue clearValue;
  };

  struct RenderPassDepthStencilAttachment {
    TextureViewHandle view;
    LoadOp     depthLoadOp     = LoadOp::Clear;
    StoreOp    depthStoreOp    = StoreOp::Store;
    float      depthClearValue = 1.0f;
    bool       depthReadOnly   = false;
    LoadOp     stencilLoadOp   = LoadOp::DontCare;
    StoreOp    stencilStoreOp  = StoreOp::DontCare;
    uint32_t   stencilClearValue = 0;
    bool       stencilReadOnly   = true;
  };

  struct RenderPassBeginDesc {
    std::vector<RenderPassColorAttachment> colorAttachments;
    RenderPassDepthStencilAttachment       depthStencilAttachment;
    bool                                   hasDepthStencil = false;
    std::string label;
  };

  // =========================================================================
  // Viewport / scissor (command encoding)
  // =========================================================================
  struct Viewport {
    float x = 0, y = 0;
    float width = 0, height = 0;
    float minDepth = 0.0f, maxDepth = 1.0f;
  };

  struct Scissor {
    int32_t  x = 0, y = 0;
    uint32_t width = 0, height = 0;
  };

  // =========================================================================
  // Command encoder (abstract interface for recording GPU commands)
  // =========================================================================
  // The encoder is the cross-API equivalent of:
  //   Vulkan  - VkCommandBuffer
  //   DX12    - ID3D12GraphicsCommandList
  //   Metal   - MTLRenderCommandEncoder
  //   WebGPU  - GPUCommandEncoder + GPURenderPassEncoder
  //   OpenGL  - (immediate mode state changes)
  //
  // Usage pattern:
  //   auto enc = device.createCommandEncoder();
  //   enc->beginRenderPass(desc);
  //   enc->setPipeline(pipeline);
  //   enc->setBindGroup(0, bindGroup);
  //   enc->draw(vertexCount, instanceCount, firstVertex, firstInstance);
  //   enc->endRenderPass();
  //   auto cmdBuf = enc->finish();
  //   device.submit(cmdBuf);

  class RenderPassEncoder {
  public:
    virtual ~RenderPassEncoder() = default;

    virtual void setPipeline(RenderPipelineHandle pipeline) = 0;

    virtual void setBindGroup(uint32_t groupIndex, BindGroupHandle group) = 0;

    virtual void setVertexBuffer(uint32_t slot, BufferHandle buffer,
                                 uint64_t offset = 0, uint64_t size = 0) = 0;

    virtual void setIndexBuffer(BufferHandle buffer, IndexFormat format,
                                uint64_t offset = 0, uint64_t size = 0) = 0;

    virtual void setViewport(const Viewport& vp) = 0;
    virtual void setScissor(const Scissor& sc) = 0;

    virtual void setPushConstants(ShaderStage stages, uint32_t offset,
                                  uint32_t size, const void* data) = 0;

    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                      uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;

    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                             uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                             uint32_t firstInstance = 0) = 0;

    virtual void drawIndirect(BufferHandle indirectBuffer,
                              uint64_t indirectOffset) = 0;

    virtual void drawIndexedIndirect(BufferHandle indirectBuffer,
                                     uint64_t indirectOffset) = 0;

    virtual void end() = 0;
  };

  class ComputePassEncoder {
  public:
    virtual ~ComputePassEncoder() = default;

    virtual void setPipeline(ComputePipelineHandle pipeline) = 0;
    virtual void setBindGroup(uint32_t groupIndex, BindGroupHandle group) = 0;
    virtual void setPushConstants(ShaderStage stages, uint32_t offset,
                                  uint32_t size, const void* data) = 0;
    virtual void dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1) = 0;
    virtual void dispatchIndirect(BufferHandle buffer, uint64_t offset) = 0;
    virtual void end() = 0;
  };

  class CommandEncoder {
  public:
    virtual ~CommandEncoder() = default;

    virtual RenderPassEncoder* beginRenderPass(const RenderPassBeginDesc& desc) = 0;
    virtual ComputePassEncoder* beginComputePass() = 0;

    // Copy commands (outside render/compute passes)
    virtual void copyBufferToBuffer(BufferHandle src, uint64_t srcOffset,
                                    BufferHandle dst, uint64_t dstOffset,
                                    uint64_t size) = 0;

    virtual void copyBufferToTexture(BufferHandle src, uint64_t srcOffset,
                                     uint32_t bytesPerRow, uint32_t rowsPerImage,
                                     TextureHandle dst,
                                     uint32_t mipLevel, uint32_t arrayLayer,
                                     uint32_t width, uint32_t height,
                                     uint32_t depth = 1) = 0;

    virtual void copyTextureToBuffer(TextureHandle src,
                                     uint32_t mipLevel, uint32_t arrayLayer,
                                     uint32_t width, uint32_t height,
                                     uint32_t depth,
                                     BufferHandle dst, uint64_t dstOffset,
                                     uint32_t bytesPerRow, uint32_t rowsPerImage) = 0;

    /// Finish recording and produce a command buffer for submission.
    virtual CommandBufferHandle finish() = 0;
  };

  // =========================================================================
  // Device (abstract GPU device interface)
  // =========================================================================
  class Device {
  public:
    virtual ~Device() = default;

    // -- Info --
    virtual std::string_view backendName() const = 0;   // "Vulkan", "WebGPU", etc.
    virtual std::string_view deviceName() const = 0;     // GPU name

    // -- Resource creation --
    virtual BufferHandle       createBuffer(const BufferDesc& desc) = 0;
    virtual TextureHandle      createTexture(const TextureDesc& desc) = 0;
    virtual TextureViewHandle  createTextureView(TextureHandle texture,
                                                  const TextureViewDesc& desc = {}) = 0;
    virtual SamplerHandle      createSampler(const SamplerDesc& desc = {}) = 0;
    virtual ShaderModuleHandle createShaderModule(const ShaderModuleDesc& desc) = 0;

    // -- Bind group layouts + bind groups --
    virtual BindGroupLayoutHandle createBindGroupLayout(
        const BindGroupLayoutDesc& desc) = 0;
    virtual BindGroupHandle createBindGroup(const BindGroupDesc& desc) = 0;

    // -- Pipeline creation --
    // The pipeline layout is built from an ordered list of bind group layouts
    // plus push constant ranges, embedded in the pipeline desc.
    virtual RenderPipelineHandle createRenderPipeline(
        const RenderPipelineDesc& desc,
        std::span<const BindGroupLayoutHandle> bindGroupLayouts) = 0;

    virtual ComputePipelineHandle createComputePipeline(
        const ComputePipelineDesc& desc,
        std::span<const BindGroupLayoutHandle> bindGroupLayouts) = 0;

    // -- Command encoding --
    virtual std::unique_ptr<CommandEncoder> createCommandEncoder(
        std::string_view label = {}) = 0;

    // -- Submission --
    virtual void submit(CommandBufferHandle cmdBuf) = 0;
    virtual void submit(std::span<const CommandBufferHandle> cmdBufs) = 0;

    // -- Buffer operations --
    virtual void* mapBuffer(BufferHandle buffer, uint64_t offset = 0,
                            uint64_t size = 0) = 0;
    virtual void  unmapBuffer(BufferHandle buffer) = 0;
    virtual void  writeBuffer(BufferHandle buffer, uint64_t offset,
                              const void* data, uint64_t size) = 0;

    // -- Synchronization --
    virtual void waitIdle() = 0;

    // -- Resource destruction --
    virtual void destroyBuffer(BufferHandle h) = 0;
    virtual void destroyTexture(TextureHandle h) = 0;
    virtual void destroyTextureView(TextureViewHandle h) = 0;
    virtual void destroySampler(SamplerHandle h) = 0;
    virtual void destroyShaderModule(ShaderModuleHandle h) = 0;
    virtual void destroyBindGroupLayout(BindGroupLayoutHandle h) = 0;
    virtual void destroyBindGroup(BindGroupHandle h) = 0;
    virtual void destroyRenderPipeline(RenderPipelineHandle h) = 0;
    virtual void destroyComputePipeline(ComputePipelineHandle h) = 0;

    // -- Convenience --
    /// Create a 2D texture with common defaults.
    TextureHandle createTexture2D(uint32_t width, uint32_t height,
                                  Format format = Format::RGBA8Unorm,
                                  TextureUsage usage = TextureUsage::Sampled,
                                  uint32_t mipLevels = 1,
                                  SampleCount samples = SampleCount::x1) {
      return createTexture({TextureDimension::e2D, format,
                            width, height, 1, mipLevels, samples, usage});
    }

    /// Create a uniform/storage buffer with CPU write access.
    BufferHandle createMappableBuffer(uint64_t size,
                                      BufferUsage usage = BufferUsage::Uniform) {
      return createBuffer({size, usage | BufferUsage::CopyDst | BufferUsage::MapWrite});
    }
  };

}  // namespace zs::gpu
