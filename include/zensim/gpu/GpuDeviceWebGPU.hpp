// Copyright (c) zs contributors. Licensed under the MIT License.
// GpuDeviceWebGPU.hpp - WebGPU backend skeleton for gpu::Device.
//
// This file provides WebGPUDevice, the WebGPU implementation of the abstract
// gpu::Device interface. It wraps the WebGPU C API (wgpu.h / webgpu.h),
// which is available via:
//   - Emscripten's built-in WebGPU support (browser)
//   - Dawn (Google's WebGPU implementation for native)
//   - wgpu-native (Rust-based WebGPU implementation for native)
//
// The gpu:: abstraction was designed with WebGPU semantics as the baseline,
// so this backend should be the most 1:1 mapping of all backends.
//
// Status: SKELETON - method signatures and structure only. Implementation
// will follow once a WebGPU provider (Dawn or wgpu-native) is integrated
// into the zpc build system.
//
// Build prerequisites:
//   - Emscripten: #include <webgpu/webgpu.h> (provided by emsdk)
//   - Dawn:       #include <webgpu/webgpu_cpp.h> (C++ wrappers)
//   - wgpu-native: #include <webgpu/webgpu.h>

#pragma once

#include "GpuDevice.hpp"

// WebGPU header - uncomment the appropriate one for your provider:
// #include <webgpu/webgpu.h>        // C API (Emscripten, wgpu-native)
// #include <webgpu/webgpu_cpp.h>    // Dawn C++ API

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace zs::gpu {

  // =========================================================================
  // Forward declarations for WebGPU C API types
  // These will be replaced with actual WebGPU types when the header is
  // available. For now, we use opaque placeholder types so the header
  // can be parsed and the structure validated.
  // =========================================================================
#ifndef WEBGPU_H_
  // Placeholder types when WebGPU header is not available
  using WGPUDevice        = void*;
  using WGPUQueue         = void*;
  using WGPUBuffer        = void*;
  using WGPUTexture       = void*;
  using WGPUTextureView   = void*;
  using WGPUSampler       = void*;
  using WGPUShaderModule  = void*;
  using WGPUBindGroupLayout = void*;
  using WGPUBindGroup     = void*;
  using WGPURenderPipeline  = void*;
  using WGPUComputePipeline = void*;
  using WGPUCommandEncoder  = void*;
  using WGPUCommandBuffer   = void*;
  using WGPURenderPassEncoder  = void*;
  using WGPUComputePassEncoder = void*;
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
    WGPUTexture     texture = nullptr;
    WGPUTextureView defaultView = nullptr;
    uint32_t        width = 0, height = 0, depthOrLayers = 0;
    Format          format = Format::Undefined;
    uint32_t        mipLevels = 1;
  };

  struct WGPUTextureViewRecord {
    WGPUTextureView view = nullptr;
    TextureHandle   ownerTexture;
  };

  struct WGPUSamplerRecord {
    WGPUSampler sampler = nullptr;
  };

  struct WGPUShaderModuleRecord {
    WGPUShaderModule module = nullptr;
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
    WGPURenderPipeline pipeline = nullptr;
  };

  struct WGPUComputePipelineRecord {
    WGPUComputePipeline pipeline = nullptr;
  };

  // =========================================================================
  // WebGPURenderPassEncoderImpl
  // =========================================================================
  class WebGPUDevice;  // forward

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
  // WebGPUDevice - WebGPU implementation of gpu::Device
  // =========================================================================
  // This is a skeleton. Method bodies will be filled in once a WebGPU
  // provider is integrated. The structure mirrors WebGPU's native API
  // almost 1:1, making this the simplest backend to implement.

  class WebGPUDevice : public Device {
  public:
    /// Construct from a WGPUDevice handle.
    /// The caller is responsible for creating the device (via adapter request).
    explicit WebGPUDevice(WGPUDevice device, WGPUQueue queue)
        : device_(device), queue_(queue) {}

    ~WebGPUDevice() override = default;

    // -- Info --
    std::string_view backendName() const override { return "WebGPU"; }
    std::string_view deviceName() const override {
      return deviceName_;  // set during initialization
    }

    // -- Resource creation --
    // Each maps nearly 1:1 to wgpuDevice* functions.
    BufferHandle createBuffer(const BufferDesc& desc) override {
      // wgpuDeviceCreateBuffer(device_, &bufferDesc)
      (void)desc;
      return {};  // TODO
    }

    TextureHandle createTexture(const TextureDesc& desc) override {
      // wgpuDeviceCreateTexture(device_, &textureDesc)
      (void)desc;
      return {};  // TODO
    }

    TextureViewHandle createTextureView(TextureHandle texture,
                                         const TextureViewDesc& desc) override {
      // wgpuTextureCreateView(texture, &viewDesc)
      (void)texture; (void)desc;
      return {};  // TODO
    }

    SamplerHandle createSampler(const SamplerDesc& desc) override {
      // wgpuDeviceCreateSampler(device_, &samplerDesc)
      (void)desc;
      return {};  // TODO
    }

    ShaderModuleHandle createShaderModule(const ShaderModuleDesc& desc) override {
      // For WebGPU: use WGSL source (desc.wgsl)
      // For Dawn on desktop: can also use SPIR-V (desc.spirv)
      // wgpuDeviceCreateShaderModule(device_, &moduleDesc)
      (void)desc;
      return {};  // TODO
    }

    // -- Bind groups --
    BindGroupLayoutHandle createBindGroupLayout(
        const BindGroupLayoutDesc& desc) override {
      // wgpuDeviceCreateBindGroupLayout(device_, &layoutDesc)
      // WebGPU bind group layouts map 1:1 to our BindGroupLayoutDesc
      (void)desc;
      return {};  // TODO
    }

    BindGroupHandle createBindGroup(const BindGroupDesc& desc) override {
      // wgpuDeviceCreateBindGroup(device_, &groupDesc)
      // WebGPU bind groups are immutable, matching our design
      (void)desc;
      return {};  // TODO
    }

    // -- Pipelines --
    RenderPipelineHandle createRenderPipeline(
        const RenderPipelineDesc& desc,
        ShaderModuleHandle vertexShader,
        ShaderModuleHandle fragmentShader,
        std::span<const BindGroupLayoutHandle> bindGroupLayouts) override {
      // wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc)
      // WebGPU pipeline creation uses format signatures (no render pass),
      // matching our desc exactly.
      (void)desc; (void)vertexShader; (void)fragmentShader;
      (void)bindGroupLayouts;
      return {};  // TODO
    }

    ComputePipelineHandle createComputePipeline(
        const ComputePipelineDesc& desc,
        ShaderModuleHandle computeShader,
        std::span<const BindGroupLayoutHandle> bindGroupLayouts) override {
      // wgpuDeviceCreateComputePipeline(device_, &pipelineDesc)
      (void)desc; (void)computeShader; (void)bindGroupLayouts;
      return {};  // TODO
    }

    // -- Command encoding --
    std::unique_ptr<CommandEncoder> createCommandEncoder(
        std::string_view label) override {
      // wgpuDeviceCreateCommandEncoder(device_, &encoderDesc)
      (void)label;
      return nullptr;  // TODO
    }

    // -- Submission --
    void submit(CommandBufferHandle cmdBuf) override {
      // wgpuQueueSubmit(queue_, 1, &cmdBuf)
      (void)cmdBuf;
    }

    void submit(std::span<const CommandBufferHandle> cmdBufs) override {
      // wgpuQueueSubmit(queue_, count, cmdBufs)
      (void)cmdBufs;
    }

    // -- Buffer operations --
    void* mapBuffer(BufferHandle buffer, uint64_t offset, uint64_t size) override {
      // WebGPU buffer mapping is async: wgpuBufferMapAsync
      // For synchronous semantics, we need to spin-wait or use
      // wgpuDevicePoll. This is a key difference from Vulkan.
      (void)buffer; (void)offset; (void)size;
      return nullptr;  // TODO
    }

    void unmapBuffer(BufferHandle buffer) override {
      // wgpuBufferUnmap(buffer)
      (void)buffer;
    }

    void writeBuffer(BufferHandle buffer, uint64_t offset,
                     const void* data, uint64_t size) override {
      // wgpuQueueWriteBuffer(queue_, buffer, offset, data, size)
      // This is the preferred way in WebGPU (avoids map/unmap)
      (void)buffer; (void)offset; (void)data; (void)size;
    }

    // -- Synchronization --
    void waitIdle() override {
      // WebGPU doesn't have a direct waitIdle.
      // Use wgpuDevicePoll(device_, true) for Dawn
      // or requestAnimationFrame fence for browser.
    }

    // -- Resource destruction --
    // WebGPU uses reference counting; these call wgpu*Release
    void destroyBuffer(BufferHandle h) override { (void)h; }
    void destroyTexture(TextureHandle h) override { (void)h; }
    void destroyTextureView(TextureViewHandle h) override { (void)h; }
    void destroySampler(SamplerHandle h) override { (void)h; }
    void destroyShaderModule(ShaderModuleHandle h) override { (void)h; }
    void destroyBindGroupLayout(BindGroupLayoutHandle h) override { (void)h; }
    void destroyBindGroup(BindGroupHandle h) override { (void)h; }
    void destroyRenderPipeline(RenderPipelineHandle h) override { (void)h; }
    void destroyComputePipeline(ComputePipelineHandle h) override { (void)h; }

    // -- Internal accessors --
    WGPUDevice  wgpuDevice() const { return device_; }
    WGPUQueue   wgpuQueue()  const { return queue_; }

  private:
    WGPUDevice  device_ = nullptr;
    WGPUQueue   queue_  = nullptr;
    std::string deviceName_ = "WebGPU Device";
  };

  // =========================================================================
  // Encoder implementations (stubs)
  // =========================================================================
  // These will be filled in once a WebGPU provider is integrated.
  // Each method maps nearly 1:1 to the corresponding wgpu* function.

  // -- WebGPURenderPassEncoderImpl --
  inline void WebGPURenderPassEncoderImpl::setPipeline(RenderPipelineHandle) {}
  inline void WebGPURenderPassEncoderImpl::setBindGroup(uint32_t, BindGroupHandle) {}
  inline void WebGPURenderPassEncoderImpl::setVertexBuffer(uint32_t, BufferHandle, uint64_t, uint64_t) {}
  inline void WebGPURenderPassEncoderImpl::setIndexBuffer(BufferHandle, IndexFormat, uint64_t, uint64_t) {}
  inline void WebGPURenderPassEncoderImpl::setViewport(const Viewport&) {}
  inline void WebGPURenderPassEncoderImpl::setScissor(const Scissor&) {}
  inline void WebGPURenderPassEncoderImpl::setPushConstants(ShaderStage, uint32_t, uint32_t, const void*) {
    // WebGPU does not natively support push constants.
    // Emulated via a reserved uniform buffer (bind group 0, binding N).
  }
  inline void WebGPURenderPassEncoderImpl::draw(uint32_t, uint32_t, uint32_t, uint32_t) {}
  inline void WebGPURenderPassEncoderImpl::drawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) {}
  inline void WebGPURenderPassEncoderImpl::drawIndirect(BufferHandle, uint64_t) {}
  inline void WebGPURenderPassEncoderImpl::drawIndexedIndirect(BufferHandle, uint64_t) {}
  inline void WebGPURenderPassEncoderImpl::end() {
    // wgpuRenderPassEncoderEnd(enc_)
  }

  // -- WebGPUComputePassEncoderImpl --
  inline void WebGPUComputePassEncoderImpl::setPipeline(ComputePipelineHandle) {}
  inline void WebGPUComputePassEncoderImpl::setBindGroup(uint32_t, BindGroupHandle) {}
  inline void WebGPUComputePassEncoderImpl::setPushConstants(ShaderStage, uint32_t, uint32_t, const void*) {}
  inline void WebGPUComputePassEncoderImpl::dispatch(uint32_t, uint32_t, uint32_t) {}
  inline void WebGPUComputePassEncoderImpl::dispatchIndirect(BufferHandle, uint64_t) {}
  inline void WebGPUComputePassEncoderImpl::end() {
    // wgpuComputePassEncoderEnd(enc_)
  }

  // -- WebGPUCommandEncoderImpl --
  inline RenderPassEncoder* WebGPUCommandEncoderImpl::beginRenderPass(
      const RenderPassBeginDesc&) {
    // wgpuCommandEncoderBeginRenderPass maps directly to our RenderPassBeginDesc
    return nullptr;  // TODO
  }
  inline ComputePassEncoder* WebGPUCommandEncoderImpl::beginComputePass() {
    return nullptr;  // TODO
  }
  inline void WebGPUCommandEncoderImpl::copyBufferToBuffer(
      BufferHandle, uint64_t, BufferHandle, uint64_t, uint64_t) {}
  inline void WebGPUCommandEncoderImpl::copyBufferToTexture(
      BufferHandle, uint64_t, uint32_t, uint32_t, TextureHandle,
      uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
  inline void WebGPUCommandEncoderImpl::copyTextureToBuffer(
      TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
      BufferHandle, uint64_t, uint32_t, uint32_t) {}
  inline CommandBufferHandle WebGPUCommandEncoderImpl::finish() {
    // wgpuCommandEncoderFinish(enc_, &desc)
    return {};  // TODO
  }

}  // namespace zs::gpu
