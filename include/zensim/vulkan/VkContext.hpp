#pragma once
#include <memory>
#include <string>
#include <vector>
//
#include "vulkan/vulkan.hpp"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "vma/vk_mem_alloc.h"
//
#include "zensim/types/SourceLocation.hpp"
#include "zensim/vulkan/VkUtils.hpp"
#include "zensim/zpc_tpls/fmt/format.h"

#define ZS_VULKAN_USE_VMA 1

namespace zs {

  struct Image;
  struct ImageSampler;
  struct ImageView;
  struct Buffer;
  struct VkCommand;
  struct Fence;
  struct BinarySemaphore;
  struct TimelineSemaphore;
  struct SingleUseCommandBuffer;
  struct Framebuffer;
  struct RenderPass;
  struct RenderPassBuilder;
  struct RenderPassDesc;
  struct Swapchain;
  struct SwapchainBuilder;
  struct ShaderModule;
  struct Pipeline;
  struct PipelineBuilder;
  struct GraphicsPipelineDesc;
  struct DescriptorSetLayoutBuilder;
  struct DescriptorPool;
  struct ExecutionContext;
  struct VkTexture;
  struct QueryPool;
  struct TransientBufferDesc;
  struct TransientImageDesc;

  struct Vulkan;

  /// @note CAUTION: must match the member order defined in VulkanContext
  enum vk_queue_e {
    graphics = 0,
    compute,
    transfer,
    dedicated_compute,
    dedicated_transfer,
    num_queue_types
  };
  enum vk_cmd_usage_e { reuse = 0, single_use, reset };

  /// @brief Descriptor type enum matching spirv_cross::ShaderResources categories
  /// @note Maps to corresponding vk::DescriptorType values
  enum vk_descriptor_e {
    // Buffer descriptors (non-dynamic)
    uniform = 0,              ///< Uniform buffer - maps to eUniformBuffer
    storage,                  ///< Storage buffer - maps to eStorageBuffer
    
    // Buffer descriptors (dynamic - offset specified at bind time)
    uniform_dynamic,          ///< Uniform buffer dynamic - maps to eUniformBufferDynamic
    storage_dynamic,          ///< Storage buffer dynamic - maps to eStorageBufferDynamic
    
    // Texel buffer descriptors
    uniform_texel,            ///< Uniform texel buffer - maps to eUniformTexelBuffer
    storage_texel,            ///< Storage texel buffer - maps to eStorageTexelBuffer
    
    // Image/sampler descriptors
    image_sampler,            ///< Combined image sampler - maps to eCombinedImageSampler
    sampled_image,            ///< Sampled image (separate) - maps to eSampledImage
    storage_image,            ///< Storage image - maps to eStorageImage
    sampler,                  ///< Sampler (separate) - maps to eSampler
    
    // Attachment descriptors
    input_attachment,         ///< Input attachment - maps to eInputAttachment
    
    // Ray tracing descriptors
    acceleration_structure,   ///< Acceleration structure (KHR) - maps to eAccelerationStructureKHR
    
    // Other descriptors
    inline_uniform_block,     ///< Inline uniform block - maps to eInlineUniformBlock
    
    num_descriptor_types
  };

  /// @brief Helper to convert vk_descriptor_e to vk::DescriptorType
  inline vk::DescriptorType to_vk_descriptor_type(vk_descriptor_e e) {
    switch (e) {
      case vk_descriptor_e::uniform:
        return vk::DescriptorType::eUniformBuffer;
      case vk_descriptor_e::storage:
        return vk::DescriptorType::eStorageBuffer;
      case vk_descriptor_e::uniform_dynamic:
        return vk::DescriptorType::eUniformBufferDynamic;
      case vk_descriptor_e::storage_dynamic:
        return vk::DescriptorType::eStorageBufferDynamic;
      case vk_descriptor_e::uniform_texel:
        return vk::DescriptorType::eUniformTexelBuffer;
      case vk_descriptor_e::storage_texel:
        return vk::DescriptorType::eStorageTexelBuffer;
      case vk_descriptor_e::image_sampler:
        return vk::DescriptorType::eCombinedImageSampler;
      case vk_descriptor_e::sampled_image:
        return vk::DescriptorType::eSampledImage;
      case vk_descriptor_e::storage_image:
        return vk::DescriptorType::eStorageImage;
      case vk_descriptor_e::sampler:
        return vk::DescriptorType::eSampler;
      case vk_descriptor_e::input_attachment:
        return vk::DescriptorType::eInputAttachment;
      case vk_descriptor_e::acceleration_structure:
        return vk::DescriptorType::eAccelerationStructureKHR;
      case vk_descriptor_e::inline_uniform_block:
        return vk::DescriptorType::eInlineUniformBlock;
      default:
        return vk::DescriptorType::eUniformBuffer;
    }
  }

  /// @brief Get descriptor type name string for debugging
  inline const char* descriptor_type_name(vk_descriptor_e e) {
    switch (e) {
      case vk_descriptor_e::uniform: return "uniform";
      case vk_descriptor_e::storage: return "storage";
      case vk_descriptor_e::uniform_dynamic: return "uniform_dynamic";
      case vk_descriptor_e::storage_dynamic: return "storage_dynamic";
      case vk_descriptor_e::uniform_texel: return "uniform_texel";
      case vk_descriptor_e::storage_texel: return "storage_texel";
      case vk_descriptor_e::image_sampler: return "image_sampler";
      case vk_descriptor_e::sampled_image: return "sampled_image";
      case vk_descriptor_e::storage_image: return "storage_image";
      case vk_descriptor_e::sampler: return "sampler";
      case vk_descriptor_e::input_attachment: return "input_attachment";
      case vk_descriptor_e::acceleration_structure: return "acceleration_structure";
      case vk_descriptor_e::inline_uniform_block: return "inline_uniform_block";
      default: return "unknown";
    }
  }

  using vk_handle_t = i32;
  using image_handle_t = vk_handle_t;
  using buffer_handle_t = vk_handle_t;

  static constexpr u32 num_buffered_frames = 3;  // generally 2 or 3
  static constexpr u32 num_max_default_resources = 1000;
  static constexpr u32 num_max_bindless_resources = 1000;
  static constexpr u32 bindless_texture_binding = 0;

  /// @note wrapper class for SwapchainBuilder, behave like Unique<SwapchainBuilder>
  struct SwapchainBuilderOwner {
    SwapchainBuilderOwner() = default;
    SwapchainBuilderOwner(void *) noexcept;
    ~SwapchainBuilderOwner();

    SwapchainBuilderOwner(SwapchainBuilderOwner &&o) noexcept;
    SwapchainBuilderOwner &operator=(SwapchainBuilderOwner &&o);
    SwapchainBuilderOwner(const SwapchainBuilderOwner &o) = delete;
    SwapchainBuilderOwner &operator=(const SwapchainBuilderOwner &o) = delete;

    void reset(void * = nullptr);
    operator bool() const noexcept { return _handle; }
    explicit operator SwapchainBuilder *() noexcept {
      return static_cast<SwapchainBuilder *>(_handle);
    }

    void *_handle{nullptr};
  };

  struct ZPC_CORE_API VulkanContext {
    Vulkan &driver() const noexcept;
    VulkanContext(int devid, vk::Instance instance, vk::PhysicalDevice device,
                  const ZS_VK_DISPATCH_LOADER_DYNAMIC &instDispatcher);
    ~VulkanContext() noexcept = default;
    VulkanContext(VulkanContext &&) = default;
    VulkanContext &operator=(VulkanContext &&) = default;
    VulkanContext(const VulkanContext &) = delete;
    VulkanContext &operator=(const VulkanContext &) = delete;

    auto getDevId() const noexcept { return devid; }

    /// behaviors
    void reset();

    ///
    /// queue
    ///
    u32 numDistinctQueueFamilies() const noexcept { return uniqueQueueFamilyIndices.size(); }

    vk::PhysicalDevice getPhysicalDevice() const noexcept { return physicalDevice; }
    vk::Device getDevice() const noexcept { return device; }
    int getQueueFamilyIndex(vk_queue_e e = vk_queue_e::graphics) const noexcept {
      return queueFamilyIndices[e];
    }
    const auto &getQueueFamilyPropertyByIndex(int i) const noexcept { return queueFamilyProps[i]; }
    const auto &getQueueFamilyPropertyByFamily(vk_queue_e e) const noexcept {
      return queueFamilyProps[queueFamilyMaps[e]];
    }
    bool retrieveQueue(vk::Queue &q, vk_queue_e e = vk_queue_e::graphics, u32 i = 0) const noexcept;
    u32 getNumQueues(vk_queue_e e = vk_queue_e::graphics) const {
      if (auto id = queueFamilyMaps[e]; id != -1) return queueFamilyProps[id].queueCount;
      return 0;
    }
    vk::Queue getQueue(vk_queue_e e = vk_queue_e::graphics, u32 i = 0) const {
      auto index = queueFamilyIndices[e];
      if (index == -1) throw std::runtime_error("queue does not exist.");
      return device.getQueue(index, i, dispatcher);
    }
    vk::Queue getLastQueue(vk_queue_e e = vk_queue_e::graphics) const {
      return getQueue(e, getNumQueues(e) - 1);
    }
    /// @note usually queried for dedicated queue types (e.g. compute/transfer)
    bool isQueueValid(vk_queue_e e) const { return queueFamilyIndices[e] != -1; }

    void sync() const { device.waitIdle(dispatcher); }

    ///
    /// property queries
    ///
    const VmaAllocator &allocator() const noexcept { return defaultAllocator; }
    VmaAllocator &allocator() noexcept { return defaultAllocator; }

    bool supportDepthResolveModes(vk::ResolveModeFlags expected) const noexcept {
      return (expected & depthStencilResolveProperties.supportedDepthResolveModes) == expected;
    }
    bool supportBindless() const {
      return supportedVk12Features.descriptorBindingPartiallyBound
             && supportedVk12Features.runtimeDescriptorArray;
    }
    bool supportTrueBindless() const {
      return supportBindless() && supportedVk12Features.descriptorBindingVariableDescriptorCount
             && supportedVk12Features.shaderSampledImageArrayNonUniformIndexing;
    }
    bool supportGraphics() const { return queueFamilyIndices[vk_queue_e::graphics] != -1; }
    /// @note usually called right before swapchain creation for assurance
    bool supportSurface(vk::SurfaceKHR surface) const;

    // various descriptor types
    // samplers
    inline u32 maxPerStageDescriptorUpdateAfterBindSamplers() const noexcept;
    inline u32 maxDescriptorSetUpdateAfterBindSamplers() const noexcept;
    inline u32 maxPerStageDescriptorSamplers() const noexcept;
    // sampled image
    inline u32 maxPerStageDescriptorUpdateAfterBindSampledImages() const noexcept;
    inline u32 maxPerStageDescriptorSampledImages() const noexcept;
    // storage image
    inline u32 maxPerStageDescriptorUpdateAfterBindStorageImages() const noexcept;
    inline u32 maxPerStageDescriptorStorageImages() const noexcept;
    // storage buffer
    inline u32 maxPerStageDescriptorUpdateAfterBindStorageBuffers() const noexcept;
    inline u32 maxPerStageDescriptorStorageBuffers() const noexcept;
    // uniform buffer
    inline u32 maxPerStageDescriptorUpdateAfterBindUniformBuffers() const noexcept;
    inline u32 maxPerStageDescriptorUniformBuffers() const noexcept;
    // input attachment
    inline u32 maxPerStageDescriptorUpdateAfterBindInputAttachments() const noexcept;
    inline u32 maxPerStageDescriptorInputAttachments() const noexcept;

    u32 numMemoryTypes() const { return memoryProperties.memoryTypeCount; }
    u32 findMemoryType(u32 memoryTypeBits, vk::MemoryPropertyFlags properties) const;
    vk::Format findSupportedFormat(const std::vector<vk::Format> &candidates,
                                   vk::ImageTiling tiling, vk::FormatFeatureFlags features) const;
    vk::FormatProperties getFormatProperties(vk::Format) const noexcept;

    ///
    /// descriptor
    ///
    vk::DescriptorPool descriptorPool() const noexcept { return defaultDescriptorPool; }

    /// resource builders
    void setupDescriptorPool();
    void destructDescriptorPool();
    // should not delete this then acquire again for same usage
    void acquireSet(vk::DescriptorSetLayout descriptorSetLayout, vk::DescriptorSet &set) const {
      set = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{}
                                              .setDescriptorPool(defaultDescriptorPool)
                                              .setPSetLayouts(&descriptorSetLayout)
                                              .setDescriptorSetCount(1))[0];
      /// @note from lve
      // Might want to create a "DescriptorPoolManager" class that handles this case, and builds
      // a new pool whenever an old pool fills up. But this is beyond our current scope
    }
    const auto &bindlessPool() const noexcept { return bindlessDescriptorPool; }
    const auto &bindlessSetLayout() const noexcept { return bindlessDescriptorSetLayout; }
    const auto &bindlessSet() const noexcept { return bindlessDescriptorSet; }

    SwapchainBuilder &swapchain(vk::SurfaceKHR surface = VK_NULL_HANDLE, bool reset = false);
    PipelineBuilder pipeline();
    RenderPassBuilder renderpass();
    RenderPass createRenderPass(const RenderPassDesc &desc);
    DescriptorSetLayoutBuilder setlayout();
    ExecutionContext &env();  // thread-safe

    /// @note command buffer
    VkCommand createCommandBuffer(vk_cmd_usage_e usage,
                                  vk_queue_e queueFamily = vk_queue_e::graphics, bool begin = false,
                                  const source_location &loc = source_location::current());

    /// @note combined image sampler/ storage image (render target)
    image_handle_t registerImage(const VkTexture &img);
    buffer_handle_t registerBuffer(const Buffer &buffer);

    /// @note buffer
    struct BufferDesc {
      vk::DeviceSize size{0};
      vk::BufferUsageFlags usage{};
      vk::MemoryPropertyFlags memoryProperties{vk::MemoryPropertyFlagBits::eDeviceLocal};
    };

    struct ImageDesc {
      vk::ImageCreateInfo imageCI{};
      vk::MemoryPropertyFlags memoryProperties{vk::MemoryPropertyFlagBits::eDeviceLocal};
      bool createView{true};
    };

    struct SamplerDesc {
      vk::SamplerCreateInfo samplerCI{};
    };

    struct ShaderModuleDesc {
      const u32 *spirvCode{nullptr};
      size_t size{0};
      vk::ShaderStageFlagBits stageFlag{};
    };

    struct PipelineLayoutDesc {
      std::vector<vk::PushConstantRange> pushConstantRanges{};
    };

    struct ComputePipelineDesc {
      const ShaderModule *shader{nullptr};
      vk::PipelineLayout pipelineLayout{VK_NULL_HANDLE};
      u32 pushConstantSize{0};
    };

    struct TextureDesc {
      ImageDesc image{};
      vk::SamplerCreateInfo samplerCI{};
      vk::ImageLayout imageLayout{vk::ImageLayout::eShaderReadOnlyOptimal};
    };

    Buffer createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                        vk::MemoryPropertyFlags props = vk::MemoryPropertyFlagBits::eDeviceLocal,
                        const source_location &loc = source_location::current());
    Buffer createBuffer(const BufferDesc &desc,
                        const source_location &loc = source_location::current());
    Buffer createStagingBuffer(vk::DeviceSize size,
                               vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eTransferSrc,
                               const source_location &loc = source_location::current());

    /// @note image/ sampler/ texture
    ImageSampler createSampler(const vk::SamplerCreateInfo &,
                               const source_location &loc = source_location::current());
    ImageSampler createSampler(const SamplerDesc &desc,
                               const source_location &loc = source_location::current());
    ImageSampler createDefaultSampler(const source_location &loc = source_location::current());

    Image createImage(vk::ImageCreateInfo imageCI,
                      vk::MemoryPropertyFlags props = vk::MemoryPropertyFlagBits::eDeviceLocal,
                      bool createView = true,
                      const source_location &loc = source_location::current());
    Image createImage(const ImageDesc &desc, const source_location &loc = source_location::current());
    Image create2DImage(const vk::Extent2D &dim, vk::Format format = vk::Format::eR8G8B8A8Unorm,
                        vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled,
                        vk::MemoryPropertyFlags props = vk::MemoryPropertyFlagBits::eDeviceLocal,
                        bool mipmaps = false, bool createView = true, bool enableTransfer = true,
                        vk::SampleCountFlagBits sampleBits = vk::SampleCountFlagBits::e1,
                        const source_location &loc = source_location::current());
    Image createOptimal2DImage(const vk::Extent2D &dim,
                               vk::Format format = vk::Format::eR8G8B8A8Unorm,
                               vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled,
                               vk::MemoryPropertyFlags props
                               = vk::MemoryPropertyFlagBits::eDeviceLocal,
                               bool mipmaps = false, bool createView = true,
                               bool enableTransfer = true,
                               vk::SampleCountFlagBits sampleBits = vk::SampleCountFlagBits::e1,
                               const source_location &loc = source_location::current());
    Image createInputAttachment(const vk::Extent2D &dim,
                                vk::Format format = vk::Format::eR8G8B8A8Unorm,
                                vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eSampled,
                                bool enableTransfer = true,
                                const source_location &loc = source_location::current());

    ImageView create2DImageView(vk::Image image, vk::Format format = vk::Format::eR8G8B8A8Unorm,
                                vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor,
                                u32 levels = VK_REMAINING_MIP_LEVELS,
                                const void *pNextImageView = nullptr,
                                const source_location &loc = source_location::current());

    Framebuffer createFramebuffer(const std::vector<vk::ImageView> &imageViews, vk::Extent2D size,
                                  vk::RenderPass renderPass,
                                  const source_location &loc = source_location::current());

    /// @note query pool
    QueryPool createQueryPool(vk::QueryType queryType, u32 queryCount);

    /// @note synchronization primitives
    BinarySemaphore createBinarySemaphore(const source_location &loc = source_location::current());
    TimelineSemaphore createTimelineSemaphore(
        u64 initialValue = 0, const source_location &loc = source_location::current());

    /// @note descriptor
    DescriptorPool createDescriptorPool(const std::vector<vk::DescriptorPoolSize> &poolSizes,
                                        u32 maxSets = 1000,
                                        const source_location &loc = source_location::current());
    void writeDescriptorSet(const vk::DescriptorBufferInfo &bufferInfo, vk::DescriptorSet dstSet,
                            vk::DescriptorType type, u32 binding, u32 dstArrayNo = 0);
    void writeDescriptorSet(const vk::DescriptorImageInfo &imageInfo, vk::DescriptorSet dstSet,
                            vk::DescriptorType type, u32 binding, u32 dstArrayNo = 0);

    /// @note shader
    ShaderModule createShaderModule(const std::vector<char> &code,
                                    vk::ShaderStageFlagBits stageFlag);
    ShaderModule createShaderModule(const u32 *spirvCode, size_t size,
                                    vk::ShaderStageFlagBits stageFlag);
    ShaderModule createShaderModule(const ShaderModuleDesc &desc);
    ShaderModule createShaderModuleFromGlsl(const char *glslCode, vk::ShaderStageFlagBits stageFlag,
                                            std::string_view moduleName);
    std::vector<u32> compileHlslToSpirv(const char *hlslCode, vk::ShaderStageFlagBits stage,
                                        std::string_view moduleName, std::string_view entryPoint);
    ShaderModule createShaderModuleFromHlsl(const char *hlslCode, vk::ShaderStageFlagBits stageFlag,
                                            std::string_view moduleName,
                                            std::string_view entryPoint = "main");

    vk::PipelineLayout createPipelineLayout(
        const PipelineLayoutDesc &desc,
        const std::vector<vk::DescriptorSetLayout> &setLayouts = {});
    Pipeline createComputePipeline(const ComputePipelineDesc &desc);
    Pipeline createGraphicsPipeline(const GraphicsPipelineDesc &desc, vk::RenderPass renderPass,
                                    const std::vector<vk::DescriptorSetLayout> &setLayouts = {});
    Pipeline createGraphicsPipeline(const GraphicsPipelineDesc &desc, vk::RenderPass renderPass,
                                    const std::vector<ShaderModule> &shaderModules);
    VkTexture createTexture(const TextureDesc &desc,
                            const source_location &loc = source_location::current());

    int devid;
    vk::PhysicalDevice physicalDevice;
    vk::Device device;                         // currently dedicated for rendering
    ZS_VK_DISPATCH_LOADER_DYNAMIC dispatcher;  // store device-specific calls
    // graphics queue family should also be used for presentation if swapchain required

    // maps to physicalDevice's queue family properties
    int queueFamilyIndices[num_queue_types];  // graphicsQueueFamilyIndex, computeQueueFamilyIndex,
                                              // transferQueueFamilyIndex;
    // maps to vk::Device's (distinct) queue families created
    int queueFamilyMaps[num_queue_types];     // graphicsQueueFamilyMap, computeQueueFamilyMap,
                                              // transferQueueFamilyMap;

    std::vector<u32> uniqueQueueFamilyIndices;
    std::vector<vk::QueueFamilyProperties> queueFamilyProps;

    vk::PhysicalDeviceMemoryProperties memoryProperties;
    vk::PhysicalDeviceDepthStencilResolveProperties depthStencilResolveProperties;
    vk::PhysicalDeviceDescriptorIndexingProperties descriptorIndexingProperties;
    vk::PhysicalDeviceProperties2 deviceProperties;

    VkPhysicalDeviceVulkan12Features supportedVk12Features, enabledVk12Features;
    VkPhysicalDeviceVulkan13Features supportedVk13Features, enabledVk13Features;
    VkPhysicalDeviceFeatures2 supportedDeviceFeatures, enabledDeviceFeatures;
    vk::DescriptorPool defaultDescriptorPool;
    VmaAllocator defaultAllocator;
    // bindless resources
    vk::DescriptorPool bindlessDescriptorPool;
    vk::DescriptorSetLayout bindlessDescriptorSetLayout;
    vk::DescriptorSet bindlessDescriptorSet;
    std::vector<const VkTexture *> registeredImages;
    std::vector<const Buffer *> registeredBuffers;

  protected:
    /// resource builders

    // generally at most one swapchain is associated with a context, thus reuse preferred
    SwapchainBuilderOwner swapchainBuilder;
  };

  // samplers
  u32 VulkanContext::maxPerStageDescriptorUpdateAfterBindSamplers() const noexcept {
    return descriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindSamplers;
  }
  u32 VulkanContext::maxPerStageDescriptorSamplers() const noexcept {
    return deviceProperties.properties.limits.maxPerStageDescriptorSamplers;
  }
  u32 VulkanContext::maxDescriptorSetUpdateAfterBindSamplers() const noexcept {
    return descriptorIndexingProperties.maxDescriptorSetUpdateAfterBindSamplers;
  }
  // sampled image
  u32 VulkanContext::maxPerStageDescriptorUpdateAfterBindSampledImages() const noexcept {
    return descriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindSampledImages;
  }
  u32 VulkanContext::maxPerStageDescriptorSampledImages() const noexcept {
    return deviceProperties.properties.limits.maxPerStageDescriptorSampledImages;
  }
  // storage image
  u32 VulkanContext::maxPerStageDescriptorUpdateAfterBindStorageImages() const noexcept {
    return descriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindStorageImages;
  }
  u32 VulkanContext::maxPerStageDescriptorStorageImages() const noexcept {
    return deviceProperties.properties.limits.maxPerStageDescriptorStorageImages;
  }
  // storage buffer
  u32 VulkanContext::maxPerStageDescriptorUpdateAfterBindStorageBuffers() const noexcept {
    return descriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindStorageBuffers;
  }
  u32 VulkanContext::maxPerStageDescriptorStorageBuffers() const noexcept {
    return deviceProperties.properties.limits.maxPerStageDescriptorStorageBuffers;
  }
  // uniform buffer
  u32 VulkanContext::maxPerStageDescriptorUpdateAfterBindUniformBuffers() const noexcept {
    return descriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindUniformBuffers;
  }
  u32 VulkanContext::maxPerStageDescriptorUniformBuffers() const noexcept {
    return deviceProperties.properties.limits.maxPerStageDescriptorUniformBuffers;
  }
  // input attachment
  u32 VulkanContext::maxPerStageDescriptorUpdateAfterBindInputAttachments() const noexcept {
    return descriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindInputAttachments;
  }
  u32 VulkanContext::maxPerStageDescriptorInputAttachments() const noexcept {
    return deviceProperties.properties.limits.maxPerStageDescriptorInputAttachments;
  }

  struct ZPC_CORE_API ExecutionContext {
    ExecutionContext(VulkanContext &ctx);
    ~ExecutionContext();

    struct ZPC_CORE_API PoolFamily {
      vk::CommandPool reusePool;      // submit multiple times
      vk::CommandPool singleUsePool;  // submit once
      vk::CommandPool resetPool;      // reset and re-record
      vk::Queue queue;
      std::vector<vk::Queue> allQueues;
      VulkanContext *pctx{nullptr};

      VkCommand *primaryCmd;
      Fence *fence;
      std::vector<VkCommand *> secondaryCmds;
      std::vector<vk::CommandBuffer> secondaryCmdHandles;

      vk::CommandPool cmdpool(vk_cmd_usage_e usage = vk_cmd_usage_e::reset) const {
        switch (usage) {
          case vk_cmd_usage_e::reuse:
            return reusePool;
          case vk_cmd_usage_e::single_use:
            return singleUsePool;
          case vk_cmd_usage_e::reset:
            return resetPool;
          default:;
        }
        return resetPool;
      }

      vk::CommandBuffer createCommandBuffer(
          vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary, bool begin = true,
          const vk::CommandBufferInheritanceInfo *pInheritanceInfo = nullptr,
          vk_cmd_usage_e usage = vk_cmd_usage_e::single_use) const {
        const auto &cmdPool = cmdpool(usage);

        std::vector<vk::CommandBuffer> cmd = pctx->device.allocateCommandBuffers(
            vk::CommandBufferAllocateInfo{cmdPool, level, (u32)1}, pctx->dispatcher);

        // if (usage == vk_cmd_usage_e::reset) cmds.push_back(cmd[0]);

        if (begin) {
          vk::CommandBufferUsageFlags usageFlags{};
          if (usage == vk_cmd_usage_e::single_use || usage == vk_cmd_usage_e::reset)
            usageFlags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
          else
            usageFlags = vk::CommandBufferUsageFlagBits::eSimultaneousUse;
          cmd[0].begin(vk::CommandBufferBeginInfo{usageFlags, pInheritanceInfo});
        }

        return cmd[0];
      }
      VkCommand createVkCommand(vk_cmd_usage_e usage, bool begin = false,
                                const source_location &loc = source_location::current());
      void submit(u32 count, const vk::CommandBuffer *cmds, vk::Fence fence,
                  vk_cmd_usage_e usage = vk_cmd_usage_e::single_use) {
        for (u32 i = 0; i < count; i++) cmds[i].end();

        vk::SubmitInfo submit{};
        submit.setCommandBufferCount(count).setPCommandBuffers(cmds);
        if (auto res = queue.submit(1, &submit, fence, pctx->dispatcher);
            res != vk::Result::eSuccess)
          throw std::runtime_error(fmt::format("failed to submit {} commands to queue.", count));
        if (usage == vk_cmd_usage_e::single_use)
          pctx->device.freeCommandBuffers(singleUsePool, count, cmds, pctx->dispatcher);
      }

      /// @note reuse is mandatory for secondary commands here
      VkCommand &acquireSecondaryVkCommand();
      VkCommand &acquireSecondaryVkCommand(int k);
      const VkCommand &retrieveSecondaryVkCommand(int k) const;
      auto numSecondaryVkCommand() const noexcept { return secondaryCmds.size(); }
      std::vector<vk::CommandBuffer> retrieveSecondaryVkCommands(int n = -1) const;

      void submit(const vk::CommandBuffer &cmd, vk::Fence fence,
                  vk_cmd_usage_e usage = vk_cmd_usage_e::single_use) {
        submit(1, &cmd, fence, usage);
      }
    };

    PoolFamily &pools(vk_queue_e e = vk_queue_e::graphics) {
      if (ctx.queueFamilyMaps[e] >= poolFamilies.size() || ctx.queueFamilyMaps[e] < 0)
        throw std::runtime_error(fmt::format("accessing {}-th pool while there are {} in total.",
                                             ctx.queueFamilyMaps[e], poolFamilies.size()));
      return poolFamilies[ctx.queueFamilyMaps[e]];
    }
    void resetCmds(vk_cmd_usage_e usage, vk_queue_e e = vk_queue_e::graphics) {
      ctx.device.resetCommandPool(pools(e).cmdpool(usage), {}, ctx.dispatcher);
    }

    std::vector<PoolFamily> poolFamilies;

  protected:
    VulkanContext &ctx;
  };

  u32 check_current_working_contexts();

}  // namespace zs