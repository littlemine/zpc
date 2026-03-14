// Copyright (c) zs contributors. Licensed under the MIT License.
// GpuDeviceVk.hpp - Vulkan backend implementation of gpu::Device.
//
// This file provides VkDevice, the concrete Vulkan implementation of the
// abstract gpu::Device interface. It wraps an existing VulkanContext and
// translates gpu:: API calls into Vulkan commands via vulkan.hpp + VMA.
//
// Resource lifetime:
//   Resources are stored in slot-based pools indexed by Handle::id.
//   Each pool slot holds the Vulkan native handle(s) plus metadata.
//   Destruction releases the Vulkan object and returns the slot to a free list.
//
// Threading: same guarantees as gpu::Device - creation is internally
//   synchronized (via mutex), encoding is per-thread.

#pragma once

#include "GpuDevice.hpp"
#include "GpuVkMapping.hpp"

// Vulkan headers (backend-specific)
#include "zensim/vulkan/VkContext.hpp"

#include <mutex>
#include <optional>
#include <vector>
#include <cassert>
#include <cstring>

namespace zs::gpu {

  // =========================================================================
  // Slot-based resource pool
  // =========================================================================
  // Generic container that maps Handle::id (1-based) to native resource
  // records. Supports O(1) alloc/free via a free list.

  template <typename Record>
  class ResourcePool {
  public:
    /// Allocate a slot and move-construct the record into it.
    /// Returns a 1-based ID suitable for Handle::id.
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
      return idx + 1;  // 1-based
    }

    /// Release a slot by ID. Does NOT destroy the Vulkan object --
    /// the caller must do that before calling release().
    void release(uint64_t id) {
      if (id == 0) return;
      auto idx = id - 1;
      if (idx < slots_.size() && slots_[idx].has_value()) {
        slots_[idx].reset();
        freeList_.push_back(idx);
      }
    }

    /// Get a pointer to the record, or nullptr if invalid.
    Record* get(uint64_t id) {
      if (id == 0) return nullptr;
      auto idx = id - 1;
      if (idx >= slots_.size()) return nullptr;
      return slots_[idx].has_value() ? &*slots_[idx] : nullptr;
    }

    const Record* get(uint64_t id) const {
      if (id == 0) return nullptr;
      auto idx = id - 1;
      if (idx >= slots_.size()) return nullptr;
      return slots_[idx].has_value() ? &*slots_[idx] : nullptr;
    }

    size_t activeCount() const {
      return slots_.size() - freeList_.size();
    }

  private:
    std::vector<std::optional<Record>> slots_;
    std::vector<uint64_t> freeList_;
  };

  // =========================================================================
  // Vulkan resource records (what each pool slot stores)
  // =========================================================================

  struct VkBufferRecord {
    vk::Buffer       buffer;
    VmaAllocation    allocation = nullptr;
    vk::DeviceSize   size      = 0;
    vk::BufferUsageFlags usageFlags;
    void*            mapped    = nullptr;
  };

  struct VkTextureRecord {
    vk::Image            image;
    VmaAllocation        allocation = nullptr;
    vk::Extent3D         extent{};
    vk::Format           format     = vk::Format::eUndefined;
    uint32_t             mipLevels  = 1;
    uint32_t             arrayLayers = 1;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
    vk::ImageUsageFlags  usageFlags;
    // Default view (created alongside texture for convenience)
    vk::ImageView        defaultView = VK_NULL_HANDLE;
  };

  struct VkTextureViewRecord {
    vk::ImageView    view;
    TextureHandle    ownerTexture;  // back-reference for cleanup
  };

  struct VkSamplerRecord {
    vk::Sampler sampler;
  };

  struct VkShaderModuleRecord {
    vk::ShaderModule         module;
    vk::ShaderStageFlagBits  stage;
    std::string              entryPoint = "main";
  };

  struct VkBindGroupLayoutRecord {
    vk::DescriptorSetLayout               layout;
    std::vector<BindGroupLayoutEntry>      entries;
    // Cache the Vulkan bindings for descriptor write generation
    std::vector<vk::DescriptorSetLayoutBinding> vkBindings;
  };

  struct VkBindGroupRecord {
    vk::DescriptorSet set;
    BindGroupLayoutHandle layoutHandle;  // back-reference
  };

  struct VkRenderPipelineRecord {
    vk::Pipeline       pipeline;
    vk::PipelineLayout pipelineLayout;
    vk::RenderPass     renderPass;  // internally cached compatible render pass
  };

  struct VkComputePipelineRecord {
    vk::Pipeline       pipeline;
    vk::PipelineLayout pipelineLayout;
  };

  struct VkCommandBufferRecord {
    vk::CommandBuffer cmd;
  };

  // =========================================================================
  // VkRenderPassEncoderImpl
  // =========================================================================
  class VkDevice;  // forward

  class VkRenderPassEncoderImpl : public RenderPassEncoder {
  public:
    VkRenderPassEncoderImpl(VkDevice& dev, vk::CommandBuffer cmd)
        : dev_(dev), cmd_(cmd) {}

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
    VkDevice&          dev_;
    vk::CommandBuffer  cmd_;
    vk::PipelineLayout currentLayout_ = VK_NULL_HANDLE;
  };

  // =========================================================================
  // VkComputePassEncoderImpl
  // =========================================================================
  class VkComputePassEncoderImpl : public ComputePassEncoder {
  public:
    VkComputePassEncoderImpl(VkDevice& dev, vk::CommandBuffer cmd)
        : dev_(dev), cmd_(cmd) {}

    void setPipeline(ComputePipelineHandle pipeline) override;
    void setBindGroup(uint32_t groupIndex, BindGroupHandle group) override;
    void setPushConstants(ShaderStage stages, uint32_t offset,
                          uint32_t size, const void* data) override;
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override;
    void dispatchIndirect(BufferHandle buffer, uint64_t offset) override;
    void end() override;

  private:
    VkDevice&          dev_;
    vk::CommandBuffer  cmd_;
    vk::PipelineLayout currentLayout_ = VK_NULL_HANDLE;
  };

  // =========================================================================
  // VkCommandEncoderImpl
  // =========================================================================
  class VkCommandEncoderImpl : public CommandEncoder {
  public:
    VkCommandEncoderImpl(VkDevice& dev, vk::CommandBuffer cmd)
        : dev_(dev), cmd_(cmd) {}

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
    VkDevice&          dev_;
    vk::CommandBuffer  cmd_;
    // Sub-encoders (owned, one at a time)
    std::unique_ptr<VkRenderPassEncoderImpl>  currentRenderPass_;
    std::unique_ptr<VkComputePassEncoderImpl> currentComputePass_;
  };

  // =========================================================================
  // VkDevice - concrete Vulkan implementation of gpu::Device
  // =========================================================================
  class VkDevice : public Device {
  public:
    /// Construct from an existing VulkanContext.
    /// The VulkanContext must outlive this VkDevice.
    explicit VkDevice(VulkanContext& ctx) : ctx_(ctx) {}
    ~VkDevice() override = default;

    // -- Accessors for internal use by encoders --
    VulkanContext& vkCtx() { return ctx_; }
    const VulkanContext& vkCtx() const { return ctx_; }

    // -- Info --
    std::string_view backendName() const override { return "Vulkan"; }
    std::string_view deviceName() const override {
      return ctx_.deviceProperties.properties.deviceName;
    }

    // =====================================================================
    // Resource creation
    // =====================================================================

    BufferHandle createBuffer(const BufferDesc& desc) override {
      // Map gpu::BufferUsage to Vulkan usage flags
      vk::BufferUsageFlags vkUsage = vk_map::toVk(desc.usage);

      // Determine memory properties from usage hints
      VmaAllocationCreateInfo allocCI{};
      if ((desc.usage & BufferUsage::MapRead) != BufferUsage::None
          || (desc.usage & BufferUsage::MapWrite) != BufferUsage::None) {
        allocCI.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocCI.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
      } else {
        allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
      }

      vk::BufferCreateInfo bufCI{};
      bufCI.setSize(desc.size)
           .setUsage(vkUsage)
           .setSharingMode(vk::SharingMode::eExclusive);

      VkBufferCreateInfo rawBufCI = static_cast<VkBufferCreateInfo>(bufCI);
      VkBuffer rawBuf;
      VmaAllocation alloc;
      VmaAllocationInfo allocInfo{};
      auto result = vmaCreateBuffer(ctx_.allocator(), &rawBufCI, &allocCI,
                                    &rawBuf, &alloc, &allocInfo);
      if (result != VK_SUCCESS) return {};

      VkBufferRecord rec{};
      rec.buffer     = vk::Buffer{rawBuf};
      rec.allocation = alloc;
      rec.size       = desc.size;
      rec.usageFlags = vkUsage;
      rec.mapped     = desc.mappedAtCreation ? allocInfo.pMappedData : nullptr;

      std::lock_guard lock(mutex_);
      return BufferHandle{buffers_.allocate(std::move(rec))};
    }

    TextureHandle createTexture(const TextureDesc& desc) override {
      vk::ImageType imageType;
      switch (desc.dimension) {
        case TextureDimension::e1D: imageType = vk::ImageType::e1D; break;
        case TextureDimension::e3D: imageType = vk::ImageType::e3D; break;
        default:                    imageType = vk::ImageType::e2D; break;
      }

      vk::Format vkFormat = vk_map::toVk(desc.format);
      vk::ImageUsageFlags vkUsage = vk_map::toVk(desc.usage);
      vk::SampleCountFlagBits vkSamples = vk_map::toVk(desc.samples);

      vk::ImageCreateInfo imgCI{};
      imgCI.setImageType(imageType)
           .setFormat(vkFormat)
           .setExtent(vk::Extent3D(desc.width, desc.height,
                       desc.dimension == TextureDimension::e3D ? desc.depthOrLayers : 1u))
           .setMipLevels(desc.mipLevels)
           .setArrayLayers(desc.dimension == TextureDimension::e3D ? 1u : desc.depthOrLayers)
           .setSamples(vkSamples)
           .setTiling(vk::ImageTiling::eOptimal)
           .setUsage(vkUsage)
           .setSharingMode(vk::SharingMode::eExclusive)
           .setInitialLayout(vk::ImageLayout::eUndefined);

      VmaAllocationCreateInfo allocCI{};
      allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;

      VkImageCreateInfo rawImgCI = static_cast<VkImageCreateInfo>(imgCI);
      VkImage rawImg;
      VmaAllocation alloc;
      auto result = vmaCreateImage(ctx_.allocator(), &rawImgCI, &allocCI,
                                   &rawImg, &alloc, nullptr);
      if (result != VK_SUCCESS) return {};

      // Create default image view
      vk::ImageViewType viewType;
      switch (desc.dimension) {
        case TextureDimension::e1D: viewType = vk::ImageViewType::e1D; break;
        case TextureDimension::e3D: viewType = vk::ImageViewType::e3D; break;
        default:
          viewType = (desc.depthOrLayers > 1)
                         ? vk::ImageViewType::e2DArray
                         : vk::ImageViewType::e2D;
          break;
      }

      vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
      if (formatHasDepth(desc.format))
        aspect = vk::ImageAspectFlagBits::eDepth;
      if (formatHasStencil(desc.format))
        aspect = aspect | vk::ImageAspectFlagBits::eStencil;

      vk::ImageViewCreateInfo viewCI{};
      viewCI.setImage(vk::Image{rawImg})
            .setViewType(viewType)
            .setFormat(vkFormat)
            .setSubresourceRange({aspect, 0, desc.mipLevels,
                                  0, desc.dimension == TextureDimension::e3D
                                       ? 1u : desc.depthOrLayers});

      auto defaultView = ctx_.device.createImageView(viewCI, nullptr, ctx_.dispatcher);

      VkTextureRecord rec{};
      rec.image       = vk::Image{rawImg};
      rec.allocation  = alloc;
      rec.extent      = vk::Extent3D(desc.width, desc.height,
                         desc.dimension == TextureDimension::e3D ? desc.depthOrLayers : 1u);
      rec.format      = vkFormat;
      rec.mipLevels   = desc.mipLevels;
      rec.arrayLayers = desc.dimension == TextureDimension::e3D ? 1u : desc.depthOrLayers;
      rec.samples     = vkSamples;
      rec.usageFlags  = vkUsage;
      rec.defaultView = defaultView;

      std::lock_guard lock(mutex_);
      return TextureHandle{textures_.allocate(std::move(rec))};
    }

    TextureViewHandle createTextureView(TextureHandle texture,
                                         const TextureViewDesc& desc) override {
      std::lock_guard lock(mutex_);
      auto* texRec = textures_.get(texture.id);
      if (!texRec) return {};

      vk::Format vkFormat = (desc.format == Format::Undefined)
                                ? texRec->format
                                : vk_map::toVk(desc.format);

      vk::ImageViewType viewType;
      switch (desc.dimension) {
        case TextureViewDimension::e1D:      viewType = vk::ImageViewType::e1D; break;
        case TextureViewDimension::e2DArray: viewType = vk::ImageViewType::e2DArray; break;
        case TextureViewDimension::eCube:    viewType = vk::ImageViewType::eCube; break;
        case TextureViewDimension::eCubeArray: viewType = vk::ImageViewType::eCubeArray; break;
        case TextureViewDimension::e3D:      viewType = vk::ImageViewType::e3D; break;
        default:                              viewType = vk::ImageViewType::e2D; break;
      }

      Format gpuFormat = (desc.format == Format::Undefined)
                             ? vk_map::fromVk(texRec->format) : desc.format;
      vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
      if (formatHasDepth(gpuFormat))
        aspect = vk::ImageAspectFlagBits::eDepth;
      if (formatHasStencil(gpuFormat))
        aspect = aspect | vk::ImageAspectFlagBits::eStencil;

      uint32_t mipCount = (desc.mipLevelCount == 0)
                              ? VK_REMAINING_MIP_LEVELS : desc.mipLevelCount;
      uint32_t layerCount = (desc.arrayLayerCount == 0)
                                ? VK_REMAINING_ARRAY_LAYERS : desc.arrayLayerCount;

      vk::ImageViewCreateInfo viewCI{};
      viewCI.setImage(texRec->image)
            .setViewType(viewType)
            .setFormat(vkFormat)
            .setSubresourceRange({aspect, desc.baseMipLevel, mipCount,
                                  desc.baseArrayLayer, layerCount});

      auto view = ctx_.device.createImageView(viewCI, nullptr, ctx_.dispatcher);

      VkTextureViewRecord rec{};
      rec.view         = view;
      rec.ownerTexture = texture;

      return TextureViewHandle{textureViews_.allocate(std::move(rec))};
    }

    SamplerHandle createSampler(const SamplerDesc& desc) override {
      vk::SamplerCreateInfo samplerCI{};
      samplerCI.setMagFilter(vk_map::toVk(desc.magFilter))
               .setMinFilter(vk_map::toVk(desc.minFilter))
               .setMipmapMode(vk_map::toVk(desc.mipmapFilter))
               .setAddressModeU(vk_map::toVk(desc.addressU))
               .setAddressModeV(vk_map::toVk(desc.addressV))
               .setAddressModeW(vk_map::toVk(desc.addressW))
               .setMinLod(desc.lodMinClamp)
               .setMaxLod(desc.lodMaxClamp)
               .setMaxAnisotropy(desc.maxAnisotropy)
               .setAnisotropyEnable(desc.maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE)
               .setCompareEnable(desc.compareEnable ? VK_TRUE : VK_FALSE)
               .setCompareOp(vk_map::toVk(desc.compare))
               .setBorderColor(vk_map::toVk(desc.borderColor));

      auto sampler = ctx_.device.createSampler(samplerCI, nullptr, ctx_.dispatcher);

      VkSamplerRecord rec{};
      rec.sampler = sampler;

      std::lock_guard lock(mutex_);
      return SamplerHandle{samplers_.allocate(std::move(rec))};
    }

    ShaderModuleHandle createShaderModule(const ShaderModuleDesc& desc) override {
      if (desc.spirv.empty()) {
        // TODO: compile from GLSL/HLSL/WGSL via shaderc/DXC
        return {};
      }

      vk::ShaderModuleCreateInfo moduleCI{};
      moduleCI.setCodeSize(desc.spirv.size() * sizeof(uint32_t))
              .setPCode(desc.spirv.data());

      auto mod = ctx_.device.createShaderModule(moduleCI, nullptr, ctx_.dispatcher);

      VkShaderModuleRecord rec{};
      rec.module     = mod;
      rec.stage      = static_cast<vk::ShaderStageFlagBits>(
                           static_cast<VkShaderStageFlags>(vk_map::toVk(desc.stage)));
      rec.entryPoint = desc.entryPoint;

      std::lock_guard lock(mutex_);
      return ShaderModuleHandle{shaderModules_.allocate(std::move(rec))};
    }

    // =====================================================================
    // Bind group layouts and bind groups
    // =====================================================================

    BindGroupLayoutHandle createBindGroupLayout(
        const BindGroupLayoutDesc& desc) override {
      std::vector<vk::DescriptorSetLayoutBinding> vkBindings;
      vkBindings.reserve(desc.entries.size());

      for (auto& entry : desc.entries) {
        vk::DescriptorType vkType;
        switch (entry.type) {
          case BindingType::UniformBuffer:
            vkType = entry.hasDynamicOffset
                         ? vk::DescriptorType::eUniformBufferDynamic
                         : vk::DescriptorType::eUniformBuffer;
            break;
          case BindingType::StorageBuffer:
            vkType = entry.hasDynamicOffset
                         ? vk::DescriptorType::eStorageBufferDynamic
                         : vk::DescriptorType::eStorageBuffer;
            break;
          case BindingType::StorageBufferReadOnly:
            vkType = vk::DescriptorType::eStorageBuffer;
            break;
          case BindingType::Sampler:
            vkType = vk::DescriptorType::eSampler;
            break;
          case BindingType::SampledTexture:
            vkType = vk::DescriptorType::eSampledImage;
            break;
          case BindingType::StorageTexture:
            vkType = vk::DescriptorType::eStorageImage;
            break;
          // CombinedImageSampler is Vulkan-specific; not in cross-API BindingType
          default:
            vkType = vk::DescriptorType::eUniformBuffer;
            break;
        }

        vk::DescriptorSetLayoutBinding binding{};
        binding.setBinding(entry.binding)
               .setDescriptorType(vkType)
               .setDescriptorCount(1)
               .setStageFlags(vk_map::toVk(entry.visibility));

        vkBindings.push_back(binding);
      }

      vk::DescriptorSetLayoutCreateInfo layoutCI{};
      layoutCI.setBindingCount(static_cast<uint32_t>(vkBindings.size()))
              .setPBindings(vkBindings.data());

      auto layout = ctx_.device.createDescriptorSetLayout(
          layoutCI, nullptr, ctx_.dispatcher);

      VkBindGroupLayoutRecord rec{};
      rec.layout     = layout;
      rec.entries    = desc.entries;
      rec.vkBindings = std::move(vkBindings);

      std::lock_guard lock(mutex_);
      return BindGroupLayoutHandle{bindGroupLayouts_.allocate(std::move(rec))};
    }

    BindGroupHandle createBindGroup(const BindGroupDesc& desc) override {
      std::lock_guard lock(mutex_);

      auto* layoutRec = bindGroupLayouts_.get(desc.layout.id);
      if (!layoutRec) return {};

      // Allocate descriptor set from the default pool
      vk::DescriptorSet set;
      set = ctx_.device.allocateDescriptorSets(
          vk::DescriptorSetAllocateInfo{}
              .setDescriptorPool(ctx_.descriptorPool())
              .setPSetLayouts(&layoutRec->layout)
              .setDescriptorSetCount(1),
          ctx_.dispatcher)[0];

      // Write descriptors
      std::vector<vk::WriteDescriptorSet> writes;
      std::vector<vk::DescriptorBufferInfo> bufInfos;
      std::vector<vk::DescriptorImageInfo> imgInfos;
      bufInfos.reserve(desc.buffers.size());
      imgInfos.reserve(desc.textures.size() + desc.samplers.size());

      for (auto& entry : desc.buffers) {
        auto* bufRec = buffers_.get(entry.buffer.id);
        if (!bufRec) continue;

        vk::DescriptorBufferInfo info{};
        info.setBuffer(bufRec->buffer)
            .setOffset(entry.offset)
            .setRange(entry.size == 0 ? bufRec->size - entry.offset : entry.size);
        bufInfos.push_back(info);

        // Find the corresponding layout entry to get the descriptor type
        vk::DescriptorType dtype = vk::DescriptorType::eUniformBuffer;
        for (auto& vkb : layoutRec->vkBindings) {
          if (vkb.binding == entry.binding) {
            dtype = vkb.descriptorType;
            break;
          }
        }

        vk::WriteDescriptorSet write{};
        write.setDstSet(set)
             .setDstBinding(entry.binding)
             .setDescriptorType(dtype)
             .setDescriptorCount(1)
             .setPBufferInfo(&bufInfos.back());
        writes.push_back(write);
      }

      for (auto& entry : desc.textures) {
        auto* viewRec = textureViews_.get(entry.textureView.id);
        if (!viewRec) continue;

        vk::DescriptorImageInfo info{};
        info.setImageView(viewRec->view)
            .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
        imgInfos.push_back(info);

        vk::DescriptorType dtype = vk::DescriptorType::eSampledImage;
        for (auto& vkb : layoutRec->vkBindings) {
          if (vkb.binding == entry.binding) {
            dtype = vkb.descriptorType;
            break;
          }
        }

        vk::WriteDescriptorSet write{};
        write.setDstSet(set)
             .setDstBinding(entry.binding)
             .setDescriptorType(dtype)
             .setDescriptorCount(1)
             .setPImageInfo(&imgInfos.back());
        writes.push_back(write);
      }

      for (auto& entry : desc.samplers) {
        auto* sampRec = samplers_.get(entry.sampler.id);
        if (!sampRec) continue;

        vk::DescriptorImageInfo info{};
        info.setSampler(sampRec->sampler);
        imgInfos.push_back(info);

        vk::WriteDescriptorSet write{};
        write.setDstSet(set)
             .setDstBinding(entry.binding)
             .setDescriptorType(vk::DescriptorType::eSampler)
             .setDescriptorCount(1)
             .setPImageInfo(&imgInfos.back());
        writes.push_back(write);
      }

      if (!writes.empty()) {
        ctx_.device.updateDescriptorSets(
            static_cast<uint32_t>(writes.size()), writes.data(),
            0, nullptr, ctx_.dispatcher);
      }

      VkBindGroupRecord rec{};
      rec.set          = set;
      rec.layoutHandle = desc.layout;

      return BindGroupHandle{bindGroups_.allocate(std::move(rec))};
    }

    // =====================================================================
    // Pipeline creation
    // =====================================================================

    RenderPipelineHandle createRenderPipeline(
        const RenderPipelineDesc& desc,
        std::span<const BindGroupLayoutHandle> bindGroupLayouts) override {
      // Build pipeline layout
      std::vector<vk::DescriptorSetLayout> vkSetLayouts;
      vkSetLayouts.reserve(bindGroupLayouts.size());
      for (auto h : bindGroupLayouts) {
        auto* rec = bindGroupLayouts_.get(h.id);
        if (rec) vkSetLayouts.push_back(rec->layout);
      }

      std::vector<vk::PushConstantRange> vkPushConstants;
      for (auto& pc : desc.pushConstants) {
        vkPushConstants.push_back(vk::PushConstantRange{
            vk_map::toVk(pc.stages), pc.offset, pc.size});
      }

      vk::PipelineLayoutCreateInfo layoutCI{};
      layoutCI.setSetLayoutCount(static_cast<uint32_t>(vkSetLayouts.size()))
              .setPSetLayouts(vkSetLayouts.data())
              .setPushConstantRangeCount(static_cast<uint32_t>(vkPushConstants.size()))
              .setPPushConstantRanges(vkPushConstants.data());

      auto pipelineLayout = ctx_.device.createPipelineLayout(
          layoutCI, nullptr, ctx_.dispatcher);

      // Create a compatible render pass from the format signature
      auto renderPass = createCompatibleRenderPass_(desc);

      // Build shader stages
      std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
      auto* vertMod = shaderModules_.get(0);  // placeholder
      auto* fragMod = shaderModules_.get(0);

      // For now, pipeline creation requires pre-created shader modules
      // stored elsewhere. This is a design point -- we need shader handles
      // in RenderPipelineDesc. For the initial implementation, this is
      // stubbed. Full implementation will follow.

      // TODO: Complete pipeline creation with shader modules
      // The full Vulkan pipeline creation involves:
      //   1. Shader stage setup from ShaderModuleHandle references
      //   2. Vertex input state from desc.vertexBuffers
      //   3. Input assembly from desc.topology
      //   4. Rasterization from desc.cullMode, frontFace, polygonMode
      //   5. Multisample from desc.sampleCount
      //   6. Color blend from desc.colorTargets
      //   7. Depth/stencil from desc.depthStencil
      //   8. Dynamic state (viewport, scissor)

      VkRenderPipelineRecord rec{};
      rec.pipeline       = VK_NULL_HANDLE;  // TODO
      rec.pipelineLayout = pipelineLayout;
      rec.renderPass     = renderPass;

      std::lock_guard lock(mutex_);
      return RenderPipelineHandle{renderPipelines_.allocate(std::move(rec))};
    }

    ComputePipelineHandle createComputePipeline(
        const ComputePipelineDesc& desc,
        std::span<const BindGroupLayoutHandle> bindGroupLayouts) override {
      // TODO: implement
      return {};
    }

    // =====================================================================
    // Command encoding
    // =====================================================================

    std::unique_ptr<CommandEncoder> createCommandEncoder(
        std::string_view label) override {
      // Allocate a command buffer from the graphics pool
      auto& env = ctx_.env();
      auto& pool = env.pools(vk_queue_e::graphics);
      auto cmd = pool.createCommandBuffer(
          vk::CommandBufferLevel::ePrimary, true,
          nullptr, vk_cmd_usage_e::single_use);

      return std::make_unique<VkCommandEncoderImpl>(*this, cmd);
    }

    // =====================================================================
    // Submission
    // =====================================================================

    void submit(CommandBufferHandle cmdBuf) override {
      // TODO: proper submission with fences/semaphores
      // For now, this is a simplified path
    }

    void submit(std::span<const CommandBufferHandle> cmdBufs) override {
      // TODO: batch submission
    }

    // =====================================================================
    // Buffer operations
    // =====================================================================

    void* mapBuffer(BufferHandle buffer, uint64_t offset, uint64_t size) override {
      std::lock_guard lock(mutex_);
      auto* rec = buffers_.get(buffer.id);
      if (!rec) return nullptr;

      if (rec->mapped) return static_cast<char*>(rec->mapped) + offset;

      void* data = nullptr;
      auto result = vmaMapMemory(ctx_.allocator(), rec->allocation, &data);
      if (result != VK_SUCCESS) return nullptr;
      rec->mapped = data;
      return static_cast<char*>(data) + offset;
    }

    void unmapBuffer(BufferHandle buffer) override {
      std::lock_guard lock(mutex_);
      auto* rec = buffers_.get(buffer.id);
      if (!rec || !rec->mapped) return;

      vmaUnmapMemory(ctx_.allocator(), rec->allocation);
      rec->mapped = nullptr;
    }

    void writeBuffer(BufferHandle buffer, uint64_t offset,
                     const void* data, uint64_t size) override {
      auto* mapped = mapBuffer(buffer, offset, 0);
      if (mapped) {
        std::memcpy(mapped, data, size);
        // Flush if needed (VMA handles coherent memory)
        std::lock_guard lock(mutex_);
        auto* rec = buffers_.get(buffer.id);
        if (rec) {
          vmaFlushAllocation(ctx_.allocator(), rec->allocation, offset, size);
        }
      }
      unmapBuffer(buffer);
    }

    // =====================================================================
    // Synchronization
    // =====================================================================

    void waitIdle() override {
      ctx_.device.waitIdle(ctx_.dispatcher);
    }

    // =====================================================================
    // Resource destruction
    // =====================================================================

    void destroyBuffer(BufferHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = buffers_.get(h.id);
      if (!rec) return;
      if (rec->mapped) {
        vmaUnmapMemory(ctx_.allocator(), rec->allocation);
      }
      ctx_.device.destroyBuffer(rec->buffer, nullptr, ctx_.dispatcher);
      vmaFreeMemory(ctx_.allocator(), rec->allocation);
      buffers_.release(h.id);
    }

    void destroyTexture(TextureHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = textures_.get(h.id);
      if (!rec) return;
      if (rec->defaultView) {
        ctx_.device.destroyImageView(rec->defaultView, nullptr, ctx_.dispatcher);
      }
      ctx_.device.destroyImage(rec->image, nullptr, ctx_.dispatcher);
      vmaFreeMemory(ctx_.allocator(), rec->allocation);
      textures_.release(h.id);
    }

    void destroyTextureView(TextureViewHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = textureViews_.get(h.id);
      if (!rec) return;
      ctx_.device.destroyImageView(rec->view, nullptr, ctx_.dispatcher);
      textureViews_.release(h.id);
    }

    void destroySampler(SamplerHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = samplers_.get(h.id);
      if (!rec) return;
      ctx_.device.destroySampler(rec->sampler, nullptr, ctx_.dispatcher);
      samplers_.release(h.id);
    }

    void destroyShaderModule(ShaderModuleHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = shaderModules_.get(h.id);
      if (!rec) return;
      ctx_.device.destroyShaderModule(rec->module, nullptr, ctx_.dispatcher);
      shaderModules_.release(h.id);
    }

    void destroyBindGroupLayout(BindGroupLayoutHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = bindGroupLayouts_.get(h.id);
      if (!rec) return;
      ctx_.device.destroyDescriptorSetLayout(rec->layout, nullptr, ctx_.dispatcher);
      bindGroupLayouts_.release(h.id);
    }

    void destroyBindGroup(BindGroupHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = bindGroups_.get(h.id);
      if (!rec) return;
      // Descriptor sets are freed when the pool is reset; individual free
      // requires VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT.
      // For now, just release the slot.
      bindGroups_.release(h.id);
    }

    void destroyRenderPipeline(RenderPipelineHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = renderPipelines_.get(h.id);
      if (!rec) return;
      if (rec->pipeline)
        ctx_.device.destroyPipeline(rec->pipeline, nullptr, ctx_.dispatcher);
      if (rec->pipelineLayout)
        ctx_.device.destroyPipelineLayout(rec->pipelineLayout, nullptr, ctx_.dispatcher);
      if (rec->renderPass)
        ctx_.device.destroyRenderPass(rec->renderPass, nullptr, ctx_.dispatcher);
      renderPipelines_.release(h.id);
    }

    void destroyComputePipeline(ComputePipelineHandle h) override {
      std::lock_guard lock(mutex_);
      auto* rec = computePipelines_.get(h.id);
      if (!rec) return;
      if (rec->pipeline)
        ctx_.device.destroyPipeline(rec->pipeline, nullptr, ctx_.dispatcher);
      if (rec->pipelineLayout)
        ctx_.device.destroyPipelineLayout(rec->pipelineLayout, nullptr, ctx_.dispatcher);
      computePipelines_.release(h.id);
    }

    // =====================================================================
    // Internal accessors (for encoder impls)
    // =====================================================================

    VkBufferRecord* getBuffer(BufferHandle h) { return buffers_.get(h.id); }
    VkTextureRecord* getTexture(TextureHandle h) { return textures_.get(h.id); }
    VkTextureViewRecord* getTextureView(TextureViewHandle h) { return textureViews_.get(h.id); }
    VkSamplerRecord* getSampler(SamplerHandle h) { return samplers_.get(h.id); }
    VkShaderModuleRecord* getShaderModule(ShaderModuleHandle h) { return shaderModules_.get(h.id); }
    VkBindGroupLayoutRecord* getBindGroupLayout(BindGroupLayoutHandle h) { return bindGroupLayouts_.get(h.id); }
    VkBindGroupRecord* getBindGroup(BindGroupHandle h) { return bindGroups_.get(h.id); }
    VkRenderPipelineRecord* getRenderPipeline(RenderPipelineHandle h) { return renderPipelines_.get(h.id); }
    VkComputePipelineRecord* getComputePipeline(ComputePipelineHandle h) { return computePipelines_.get(h.id); }

  private:
    // Create a minimal compatible VkRenderPass from format signature
    vk::RenderPass createCompatibleRenderPass_(const RenderPipelineDesc& desc) {
      std::vector<vk::AttachmentDescription> attachments;
      std::vector<vk::AttachmentReference> colorRefs;

      for (size_t i = 0; i < desc.colorTargets.size(); ++i) {
        vk::AttachmentDescription att{};
        att.setFormat(vk_map::toVk(desc.colorTargets[i].format))
           .setSamples(vk_map::toVk(desc.sampleCount))
           .setLoadOp(vk::AttachmentLoadOp::eDontCare)
           .setStoreOp(vk::AttachmentStoreOp::eStore)
           .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
           .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
           .setInitialLayout(vk::ImageLayout::eUndefined)
           .setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);

        colorRefs.push_back({static_cast<uint32_t>(attachments.size()),
                             vk::ImageLayout::eColorAttachmentOptimal});
        attachments.push_back(att);
      }

      vk::AttachmentReference depthRef{};
      bool hasDepth = desc.depthStencil.format != Format::Undefined;
      if (hasDepth) {
        vk::AttachmentDescription att{};
        att.setFormat(vk_map::toVk(desc.depthStencil.format))
           .setSamples(vk_map::toVk(desc.sampleCount))
           .setLoadOp(vk::AttachmentLoadOp::eDontCare)
           .setStoreOp(vk::AttachmentStoreOp::eStore)
           .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
           .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
           .setInitialLayout(vk::ImageLayout::eUndefined)
           .setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

        depthRef = {static_cast<uint32_t>(attachments.size()),
                    vk::ImageLayout::eDepthStencilAttachmentOptimal};
        attachments.push_back(att);
      }

      vk::SubpassDescription subpass{};
      subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
             .setColorAttachmentCount(static_cast<uint32_t>(colorRefs.size()))
             .setPColorAttachments(colorRefs.data());
      if (hasDepth)
        subpass.setPDepthStencilAttachment(&depthRef);

      vk::RenderPassCreateInfo rpCI{};
      rpCI.setAttachmentCount(static_cast<uint32_t>(attachments.size()))
          .setPAttachments(attachments.data())
          .setSubpassCount(1)
          .setPSubpasses(&subpass);

      return ctx_.device.createRenderPass(rpCI, nullptr, ctx_.dispatcher);
    }

    VulkanContext& ctx_;
    std::mutex     mutex_;

    // Resource pools
    ResourcePool<VkBufferRecord>           buffers_;
    ResourcePool<VkTextureRecord>          textures_;
    ResourcePool<VkTextureViewRecord>      textureViews_;
    ResourcePool<VkSamplerRecord>          samplers_;
    ResourcePool<VkShaderModuleRecord>     shaderModules_;
    ResourcePool<VkBindGroupLayoutRecord>  bindGroupLayouts_;
    ResourcePool<VkBindGroupRecord>        bindGroups_;
    ResourcePool<VkRenderPipelineRecord>   renderPipelines_;
    ResourcePool<VkComputePipelineRecord>  computePipelines_;
  };

  // =========================================================================
  // Encoder implementations (inline for header-only backend)
  // =========================================================================

  // -- VkRenderPassEncoderImpl --

  inline void VkRenderPassEncoderImpl::setPipeline(RenderPipelineHandle pipeline) {
    auto* rec = dev_.getRenderPipeline(pipeline);
    if (!rec || !rec->pipeline) return;
    cmd_.bindPipeline(vk::PipelineBindPoint::eGraphics, rec->pipeline,
                      dev_.vkCtx().dispatcher);
    currentLayout_ = rec->pipelineLayout;
  }

  inline void VkRenderPassEncoderImpl::setBindGroup(
      uint32_t groupIndex, BindGroupHandle group) {
    auto* rec = dev_.getBindGroup(group);
    if (!rec || !currentLayout_) return;
    cmd_.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                            currentLayout_, groupIndex,
                            1, &rec->set, 0, nullptr,
                            dev_.vkCtx().dispatcher);
  }

  inline void VkRenderPassEncoderImpl::setVertexBuffer(
      uint32_t slot, BufferHandle buffer, uint64_t offset, uint64_t /*size*/) {
    auto* rec = dev_.getBuffer(buffer);
    if (!rec) return;
    vk::DeviceSize off = offset;
    cmd_.bindVertexBuffers(slot, 1, &rec->buffer, &off,
                           dev_.vkCtx().dispatcher);
  }

  inline void VkRenderPassEncoderImpl::setIndexBuffer(
      BufferHandle buffer, IndexFormat format, uint64_t offset, uint64_t /*size*/) {
    auto* rec = dev_.getBuffer(buffer);
    if (!rec) return;
    cmd_.bindIndexBuffer(rec->buffer, offset, vk_map::toVk(format),
                         dev_.vkCtx().dispatcher);
  }

  inline void VkRenderPassEncoderImpl::setViewport(const Viewport& vp) {
    vk::Viewport vkVp{vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth};
    cmd_.setViewport(0, 1, &vkVp, dev_.vkCtx().dispatcher);
  }

  inline void VkRenderPassEncoderImpl::setScissor(const Scissor& sc) {
    vk::Rect2D rect{{sc.x, sc.y}, {sc.width, sc.height}};
    cmd_.setScissor(0, 1, &rect, dev_.vkCtx().dispatcher);
  }

  inline void VkRenderPassEncoderImpl::setPushConstants(
      ShaderStage stages, uint32_t offset, uint32_t size, const void* data) {
    if (!currentLayout_) return;
    cmd_.pushConstants(currentLayout_, vk_map::toVk(stages),
                       offset, size, data, dev_.vkCtx().dispatcher);
  }

  inline void VkRenderPassEncoderImpl::draw(
      uint32_t vertexCount, uint32_t instanceCount,
      uint32_t firstVertex, uint32_t firstInstance) {
    cmd_.draw(vertexCount, instanceCount, firstVertex, firstInstance,
              dev_.vkCtx().dispatcher);
  }

  inline void VkRenderPassEncoderImpl::drawIndexed(
      uint32_t indexCount, uint32_t instanceCount,
      uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    cmd_.drawIndexed(indexCount, instanceCount, firstIndex,
                     vertexOffset, firstInstance, dev_.vkCtx().dispatcher);
  }

  inline void VkRenderPassEncoderImpl::drawIndirect(
      BufferHandle indirectBuffer, uint64_t indirectOffset) {
    auto* rec = dev_.getBuffer(indirectBuffer);
    if (!rec) return;
    cmd_.drawIndirect(rec->buffer, indirectOffset, 1, 0,
                      dev_.vkCtx().dispatcher);
  }

  inline void VkRenderPassEncoderImpl::drawIndexedIndirect(
      BufferHandle indirectBuffer, uint64_t indirectOffset) {
    auto* rec = dev_.getBuffer(indirectBuffer);
    if (!rec) return;
    cmd_.drawIndexedIndirect(rec->buffer, indirectOffset, 1, 0,
                             dev_.vkCtx().dispatcher);
  }

  inline void VkRenderPassEncoderImpl::end() {
    cmd_.endRenderPass(dev_.vkCtx().dispatcher);
  }

  // -- VkComputePassEncoderImpl --

  inline void VkComputePassEncoderImpl::setPipeline(ComputePipelineHandle pipeline) {
    auto* rec = dev_.getComputePipeline(pipeline);
    if (!rec || !rec->pipeline) return;
    cmd_.bindPipeline(vk::PipelineBindPoint::eCompute, rec->pipeline,
                      dev_.vkCtx().dispatcher);
    currentLayout_ = rec->pipelineLayout;
  }

  inline void VkComputePassEncoderImpl::setBindGroup(
      uint32_t groupIndex, BindGroupHandle group) {
    auto* rec = dev_.getBindGroup(group);
    if (!rec || !currentLayout_) return;
    cmd_.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                            currentLayout_, groupIndex,
                            1, &rec->set, 0, nullptr,
                            dev_.vkCtx().dispatcher);
  }

  inline void VkComputePassEncoderImpl::setPushConstants(
      ShaderStage stages, uint32_t offset, uint32_t size, const void* data) {
    if (!currentLayout_) return;
    cmd_.pushConstants(currentLayout_, vk_map::toVk(stages),
                       offset, size, data, dev_.vkCtx().dispatcher);
  }

  inline void VkComputePassEncoderImpl::dispatch(
      uint32_t x, uint32_t y, uint32_t z) {
    cmd_.dispatch(x, y, z, dev_.vkCtx().dispatcher);
  }

  inline void VkComputePassEncoderImpl::dispatchIndirect(
      BufferHandle buffer, uint64_t offset) {
    auto* rec = dev_.getBuffer(buffer);
    if (!rec) return;
    cmd_.dispatchIndirect(rec->buffer, offset, dev_.vkCtx().dispatcher);
  }

  inline void VkComputePassEncoderImpl::end() {
    // Compute passes in Vulkan don't have explicit end -- no-op
  }

  // -- VkCommandEncoderImpl --

  inline RenderPassEncoder* VkCommandEncoderImpl::beginRenderPass(
      const RenderPassBeginDesc& desc) {
    // Build attachment info and create a compatible render pass
    // For dynamic rendering (VK 1.3), we could use vkCmdBeginRendering
    // instead. For now, use traditional render passes for broad compat.

    // Collect clear values
    std::vector<vk::ClearValue> clearValues;
    for (auto& att : desc.colorAttachments) {
      vk::ClearValue cv;
      cv.color = vk::ClearColorValue{std::array<float,4>{
          att.clearValue.r, att.clearValue.g,
          att.clearValue.b, att.clearValue.a}};
      clearValues.push_back(cv);
    }
    if (desc.hasDepthStencil) {
      vk::ClearValue cv;
      cv.depthStencil = vk::ClearDepthStencilValue{
          desc.depthStencilAttachment.depthClearValue,
          desc.depthStencilAttachment.stencilClearValue};
      clearValues.push_back(cv);
    }

    // Determine render area from first color attachment
    vk::Extent2D renderArea(1, 1);
    if (!desc.colorAttachments.empty()) {
      auto* viewRec = dev_.getTextureView(desc.colorAttachments[0].view);
      if (viewRec) {
        auto* texRec = dev_.getTexture(viewRec->ownerTexture);
        if (texRec) {
          renderArea = vk::Extent2D(texRec->extent.width, texRec->extent.height);
        }
      }
    }

    // TODO: Create/cache compatible render pass + framebuffer from attachments
    // This is a significant piece of work. For the initial implementation,
    // we note the design and leave the render pass begin as a stub that
    // will be filled in when the pipeline creation is also complete.

    currentRenderPass_ = std::make_unique<VkRenderPassEncoderImpl>(dev_, cmd_);
    return currentRenderPass_.get();
  }

  inline ComputePassEncoder* VkCommandEncoderImpl::beginComputePass() {
    currentComputePass_ = std::make_unique<VkComputePassEncoderImpl>(dev_, cmd_);
    return currentComputePass_.get();
  }

  inline void VkCommandEncoderImpl::copyBufferToBuffer(
      BufferHandle src, uint64_t srcOffset,
      BufferHandle dst, uint64_t dstOffset, uint64_t size) {
    auto* srcRec = dev_.getBuffer(src);
    auto* dstRec = dev_.getBuffer(dst);
    if (!srcRec || !dstRec) return;

    vk::BufferCopy region{srcOffset, dstOffset, size};
    cmd_.copyBuffer(srcRec->buffer, dstRec->buffer, 1, &region,
                    dev_.vkCtx().dispatcher);
  }

  inline void VkCommandEncoderImpl::copyBufferToTexture(
      BufferHandle src, uint64_t srcOffset,
      uint32_t bytesPerRow, uint32_t rowsPerImage,
      TextureHandle dst,
      uint32_t mipLevel, uint32_t arrayLayer,
      uint32_t width, uint32_t height, uint32_t depth) {
    auto* srcRec = dev_.getBuffer(src);
    auto* dstRec = dev_.getTexture(dst);
    if (!srcRec || !dstRec) return;

    vk::BufferImageCopy region{};
    region.setBufferOffset(srcOffset)
          .setBufferRowLength(bytesPerRow)  // TODO: convert to texels
          .setBufferImageHeight(rowsPerImage)
          .setImageSubresource({vk::ImageAspectFlagBits::eColor, mipLevel, arrayLayer, 1})
          .setImageOffset({0, 0, 0})
          .setImageExtent({width, height, depth});

    cmd_.copyBufferToImage(srcRec->buffer, dstRec->image,
                           vk::ImageLayout::eTransferDstOptimal,
                           1, &region, dev_.vkCtx().dispatcher);
  }

  inline void VkCommandEncoderImpl::copyTextureToBuffer(
      TextureHandle src, uint32_t mipLevel, uint32_t arrayLayer,
      uint32_t width, uint32_t height, uint32_t depth,
      BufferHandle dst, uint64_t dstOffset,
      uint32_t bytesPerRow, uint32_t rowsPerImage) {
    auto* srcRec = dev_.getTexture(src);
    auto* dstRec = dev_.getBuffer(dst);
    if (!srcRec || !dstRec) return;

    vk::BufferImageCopy region{};
    region.setBufferOffset(dstOffset)
          .setBufferRowLength(bytesPerRow)
          .setBufferImageHeight(rowsPerImage)
          .setImageSubresource({vk::ImageAspectFlagBits::eColor, mipLevel, arrayLayer, 1})
          .setImageOffset({0, 0, 0})
          .setImageExtent({width, height, depth});

    cmd_.copyImageToBuffer(srcRec->image,
                           vk::ImageLayout::eTransferSrcOptimal,
                           dstRec->buffer,
                           1, &region, dev_.vkCtx().dispatcher);
  }

  inline CommandBufferHandle VkCommandEncoderImpl::finish() {
    cmd_.end(dev_.vkCtx().dispatcher);
    // The command buffer handle is stored as the raw uint64_t cast
    // This is safe because VkCommandBuffer is a dispatchable handle (pointer)
    return CommandBufferHandle{reinterpret_cast<uint64_t>(
        static_cast<VkCommandBuffer>(cmd_))};
  }

}  // namespace zs::gpu
