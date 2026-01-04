// vulkan memory allocator impl
#define VK_ENABLE_BETA_EXTENSIONS
// to use VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR
#include "vulkan/vulkan_core.h"
#include "zensim/Platform.hpp"
//
#if defined(ZS_PLATFORM_OSX)
#  include "vulkan/vulkan_beta.h"
#endif
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"
//
#include "zensim/vulkan/VkContext.hpp"
#include "zensim/vulkan/VkDeviceConfig.hpp"
//
#include <iostream>
#include <map>
#include <set>
#include <thread>

#include "zensim/vulkan/VkBuffer.hpp"
#include "zensim/vulkan/VkCommand.hpp"
#include "zensim/vulkan/VkDescriptor.hpp"
#include "zensim/vulkan/VkImage.hpp"
#include "zensim/vulkan/VkPipeline.hpp"
#include "zensim/vulkan/VkQueryPool.hpp"
#include "zensim/vulkan/VkRenderPass.hpp"
#include "zensim/vulkan/VkShader.hpp"
#include "zensim/vulkan/VkSwapchain.hpp"
#include "zensim/vulkan/Vulkan.hpp"

//
#include "zensim/Logger.hpp"
#include "zensim/ZpcReflection.hpp"
#include "zensim/execution/ConcurrencyPrimitive.hpp"
#include "zensim/types/Iterator.h"
#include "zensim/types/SourceLocation.hpp"
#include "zensim/zpc_tpls/fmt/color.h"
#include "zensim/zpc_tpls/fmt/format.h"
#include "zensim/zpc_tpls/fmt/std.h"
#include "zensim/zpc_tpls/magic_enum/magic_enum.hpp"

namespace zs {

  using ContextEnvs = std::map<int, ExecutionContext>;
  using WorkerEnvs = std::map<std::thread::id, ContextEnvs>;

  // now moved to Vulkan singleton
  // namespace {
  //   static WorkerEnvs g_workingContexts;
  //   static Mutex g_mtx{};
  // }  // namespace

  ///
  ///
  /// vulkan swapchain builder
  ///
  ///
  SwapchainBuilderOwner::SwapchainBuilderOwner(void* handle) noexcept : _handle{handle} {}
  SwapchainBuilderOwner::~SwapchainBuilderOwner() {
    if (_handle) {
      delete static_cast<SwapchainBuilder*>(_handle);
      _handle = nullptr;
    }
  }

  SwapchainBuilderOwner::SwapchainBuilderOwner(SwapchainBuilderOwner&& o) noexcept
      : _handle{zs::exchange(o._handle, nullptr)} {}

  SwapchainBuilderOwner& SwapchainBuilderOwner::operator=(SwapchainBuilderOwner&& o) {
    if (_handle) delete static_cast<SwapchainBuilder*>(_handle);
    _handle = zs::exchange(o._handle, nullptr);
    return *this;
  }

  void SwapchainBuilderOwner::reset(void* handle) {
    if (_handle) delete static_cast<SwapchainBuilder*>(_handle);
    _handle = handle;
  }

  ///
  ///
  /// vulkan context
  ///
  ///
  Vulkan& VulkanContext::driver() const noexcept { return Vulkan::driver(); }

  void VulkanContext::reset() {
    /// clear builders
    // if (swapchainBuilder) swapchainBuilder.reset(nullptr);
    /// clear execution resources
    // handled by Vulkan

    destructDescriptorPool();

    vmaDestroyAllocator(defaultAllocator);
    defaultAllocator = 0;  // ref: nvpro-core

    /// destroy logical device
    if (device) {
      device.destroy(nullptr, dispatcher);
      device = vk::Device{};
    }
    fmt::print("vulkan context [{}] (of {}) has been successfully reset.\n", devid,
               driver().num_devices());
  }

  VulkanContext::VulkanContext(int devId, vk::Instance instance, vk::PhysicalDevice phydev,
                               const ZS_VK_DISPATCH_LOADER_DYNAMIC& instDispatcher)
      : devid{devId}, physicalDevice{phydev}, device{}, dispatcher{instDispatcher} {
    /// @note logical device
    std::vector<vk::ExtensionProperties> devExts
        = physicalDevice.enumerateDeviceExtensionProperties();
    vk::PhysicalDeviceProperties devProps = physicalDevice.getProperties();

    /// queue family
    queueFamilyProps = physicalDevice.getQueueFamilyProperties();
    for (auto& queueFamilyIndex : queueFamilyIndices) queueFamilyIndex = -1;
    for (auto& queueFamilyMap : queueFamilyMaps) queueFamilyMap = -1;
    int graphicsAndCompute = -1;
    for (int i = 0; i != queueFamilyProps.size(); ++i) {
      int both = 0;
      auto& q = queueFamilyProps[i];
      if (q.queueCount == 0) continue;
      if (queueFamilyIndices[vk_queue_e::graphics] == -1
          && (q.queueFlags & vk::QueueFlagBits::eGraphics)) {
        queueFamilyIndices[vk_queue_e::graphics] = i;
        ZS_WARN_IF(!(q.queueFlags & vk::QueueFlagBits::eTransfer),
                   "the selected graphics queue family cannot transfer!");
        both++;
      }
      if (queueFamilyIndices[vk_queue_e::compute] == -1
          && (q.queueFlags & vk::QueueFlagBits::eCompute)) {
        queueFamilyIndices[vk_queue_e::compute] = i;
        both++;
      }
      if (queueFamilyIndices[vk_queue_e::transfer] == -1
          && (q.queueFlags & vk::QueueFlagBits::eTransfer))
        queueFamilyIndices[vk_queue_e::transfer] = i;

      if (both == 2) graphicsAndCompute = i;

      if (queueFamilyIndices[vk_queue_e::dedicated_transfer] == -1
          && (q.queueFlags & vk::QueueFlagBits::eTransfer) && both == 0) {
        queueFamilyIndices[vk_queue_e::dedicated_transfer] = i;
      }

      /// queue family info
      fmt::print(
          "\n\t====> {}-th queue family has {} queue(s).\n\tQueue "
          "capabilities [graphics: {}, compute: {}, transfer: {}, sparse binding: {}, \n\t\tvideo "
          "encode: {}, video decode: {}]\n",
          i, q.queueCount, static_cast<bool>(q.queueFlags & vk::QueueFlagBits::eGraphics),
          static_cast<bool>(q.queueFlags & vk::QueueFlagBits::eCompute),
          static_cast<bool>(q.queueFlags & vk::QueueFlagBits::eTransfer),
          static_cast<bool>(q.queueFlags & vk::QueueFlagBits::eSparseBinding),
          static_cast<bool>(q.queueFlags & vk::QueueFlagBits::eVideoEncodeKHR),
          static_cast<bool>(q.queueFlags & vk::QueueFlagBits::eVideoDecodeKHR));
    }
    if (graphicsAndCompute == -1)
      throw std::runtime_error(
          "there should be at least a queue that supports both graphics and compute!");
    queueFamilyIndices[vk_queue_e::graphics] = queueFamilyIndices[vk_queue_e::compute]
        = graphicsAndCompute;

    for (int i = 0; i != queueFamilyProps.size(); ++i) {
      auto& q = queueFamilyProps[i];
      if (q.queueCount == 0 || !(q.queueFlags & vk::QueueFlagBits::eCompute)
          || i == queueFamilyIndices[vk_queue_e::graphics])
        continue;
      if (queueFamilyIndices[vk_queue_e::dedicated_compute] == -1
          || i != queueFamilyIndices[vk_queue_e::dedicated_transfer])
        queueFamilyIndices[vk_queue_e::dedicated_compute] = i;
    }

    ZS_ERROR_IF(queueFamilyIndices[vk_queue_e::graphics] == -1,
                "graphics queue family does not exist!");
    fmt::print(
        "selected queue family [{}] for graphics! (compute: {}, transfer: {}, dedicated compute: "
        "{}, dedicated transfer: {})\n",
        queueFamilyIndices[vk_queue_e::graphics], queueFamilyIndices[vk_queue_e::compute],
        queueFamilyIndices[vk_queue_e::transfer], queueFamilyIndices[vk_queue_e::dedicated_compute],
        queueFamilyIndices[vk_queue_e::dedicated_transfer]);

    std::set<u32> uniqueQueueFamilyIndices{(u32)queueFamilyIndices[vk_queue_e::graphics],
                                           (u32)queueFamilyIndices[vk_queue_e::compute],
                                           (u32)queueFamilyIndices[vk_queue_e::transfer],
                                           (u32)queueFamilyIndices[vk_queue_e::dedicated_compute],
                                           (u32)queueFamilyIndices[vk_queue_e::dedicated_transfer]};
    uniqueQueueFamilyIndices.erase(-1);
    this->uniqueQueueFamilyIndices.reserve(uniqueQueueFamilyIndices.size());
    std::vector<vk::DeviceQueueCreateInfo> dqCIs(uniqueQueueFamilyIndices.size());
    std::vector<std::vector<float>> uniqueQueuePriorities(uniqueQueueFamilyIndices.size());
    {
      u32 i = 0;
      for (auto index : uniqueQueueFamilyIndices) {
        const auto& queueFamilyProp = queueFamilyProps[i];
        this->uniqueQueueFamilyIndices.push_back(index);
        uniqueQueuePriorities[i].resize(queueFamilyProp.queueCount);
        for (auto& v : uniqueQueuePriorities[i]) v = 0.5f;
        dqCIs[i]
            .setQueueCount(queueFamilyProp.queueCount)
            .setQueueFamilyIndex(index)
            .setQueuePriorities(uniqueQueuePriorities[i]);
        // .setPQueuePriorities(uniqueQueuePriorities[i].data());

        if (queueFamilyIndices[vk_queue_e::graphics] == index)
          queueFamilyMaps[vk_queue_e::graphics] = i;
        if (queueFamilyIndices[vk_queue_e::compute] == index)
          queueFamilyMaps[vk_queue_e::compute] = i;
        if (queueFamilyIndices[vk_queue_e::transfer] == index)
          queueFamilyMaps[vk_queue_e::transfer] = i;
        if (queueFamilyIndices[vk_queue_e::dedicated_compute] == index)
          queueFamilyMaps[vk_queue_e::dedicated_compute] = i;
        if (queueFamilyIndices[vk_queue_e::dedicated_transfer] == index)
          queueFamilyMaps[vk_queue_e::dedicated_transfer] = i;

        i++;
      }
      fmt::print(
          "queue family maps [graphics: {} ({} queues), compute: {} ({} queues), transfer: {} ({} "
          "queues), dedicated compute: {} ({} queues), dedicated transfer: {} ({} queues)]\n",
          queueFamilyMaps[vk_queue_e::graphics],
          queueFamilyProps[queueFamilyMaps[vk_queue_e::graphics]].queueCount,
          queueFamilyMaps[vk_queue_e::compute],
          queueFamilyProps[queueFamilyMaps[vk_queue_e::compute]].queueCount,
          queueFamilyMaps[vk_queue_e::transfer],
          queueFamilyProps[queueFamilyMaps[vk_queue_e::transfer]].queueCount,
          queueFamilyMaps[vk_queue_e::dedicated_compute] != -1
              ? queueFamilyMaps[vk_queue_e::dedicated_compute]
              : -1,
          queueFamilyMaps[vk_queue_e::dedicated_compute] != -1
              ? queueFamilyProps[queueFamilyMaps[vk_queue_e::dedicated_compute]].queueCount
              : -1,
          queueFamilyMaps[vk_queue_e::dedicated_transfer] != -1
              ? queueFamilyMaps[vk_queue_e::dedicated_transfer]
              : -1,
          queueFamilyMaps[vk_queue_e::dedicated_transfer] != -1
              ? queueFamilyProps[queueFamilyMaps[vk_queue_e::dedicated_transfer]].queueCount
              : -1);
    }

    /// @note Query supported features with full feature chain (Vulkan 1.2 + 1.3)
    VkPhysicalDeviceVulkan13Features supportedVk13Features{};
    supportedVk13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    supportedVk13Features.pNext = nullptr;

    VkPhysicalDeviceVulkan12Features supportedVk12Features{};
    supportedVk12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    supportedVk12Features.pNext = &supportedVk13Features;

    VkPhysicalDeviceFeatures2 devFeatures2{};
    devFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    devFeatures2.pNext = &supportedVk12Features;

    dispatcher.vkGetPhysicalDeviceFeatures2(physicalDevice, &devFeatures2);

    this->supportedVk12Features = supportedVk12Features;
    this->supportedVk13Features = supportedVk13Features;
    this->supportedDeviceFeatures = devFeatures2;

    // query properties 2
    vk::PhysicalDeviceDescriptorIndexingProperties descriptorIndexingProperties;
    vk::PhysicalDeviceDepthStencilResolveProperties dsResolveProperties{};
    dsResolveProperties.pNext = &descriptorIndexingProperties;
    vk::PhysicalDeviceProperties2 devProperties{};
    devProperties.pNext = &dsResolveProperties;
    physicalDevice.getProperties2(&devProperties);

    this->descriptorIndexingProperties = descriptorIndexingProperties;
    this->depthStencilResolveProperties = dsResolveProperties;
    this->deviceProperties = devProperties;

    /// @note Create device configuration using VkDeviceConfig builder
    auto configBuilder = VkDeviceConfig::createBuilder(
        devExts, devFeatures2, supportedVk12Features, supportedVk13Features);

    // Configure desired features declaratively
    configBuilder
        .withSwapchain(true)                // Presentation support
        .withRayTracing(true)               // Ray tracing (filtered if unsupported)
        .withDynamicState(true)             // Dynamic pipeline states
        .withBindless(true)                 // Bindless descriptors
        .withTimelineSemaphore(true)        // Timeline semaphores
        .withSynchronization2(true)         // Vulkan 1.3 synchronization
        .withDynamicRendering(true)         // Vulkan 1.3 dynamic rendering
        .withMaintenance4(true)             // Vulkan 1.3 maintenance
        .withGeometryShader(true)           // Geometry shader stage
        .withTessellation(true)             // Tessellation stages
        .filterBySupported(devExts, devFeatures2, supportedVk12Features, supportedVk13Features);

    // Get flattened extension list
    auto enabledExtensions = configBuilder.extensions.flatten();

    // Count ray tracing extensions for backward compatibility
    int rtPreds = 0;
    constexpr int rtRequiredPreds = 5;
    for (const auto& ext : configBuilder.extensions.rayTracing) {
        if (std::find(enabledExtensions.begin(), enabledExtensions.end(), ext) 
            != enabledExtensions.end()) {
            rtPreds++;
        }
    }

    /// @note Create feature structures
    vk::PhysicalDeviceFeatures2 features{};
    vk::PhysicalDeviceVulkan13Features vk13Features{};
    vk::PhysicalDeviceVulkan12Features vk12Features{};

    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicStateFeaturesEXT{};
    vk::PhysicalDeviceExtendedDynamicState2FeaturesEXT extendedDynamicState2FeaturesEXT{};
    vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT extendedDynamicState3FeaturesEXT{};
    vk::PhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rtPipeFeatures{};

#ifdef ZS_PLATFORM_OSX
    VkPhysicalDevicePortabilitySubsetFeaturesKHR portabilityFeatures{};
    portabilityFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PORTABILITY_SUBSET_FEATURES_KHR;
    portabilityFeatures.pNext = nullptr;
#endif

    // Apply configuration to Vulkan structures
    configBuilder.features.applyToVkStructures(
        features, vk12Features, vk13Features,
        extendedDynamicStateFeaturesEXT, extendedDynamicState2FeaturesEXT, 
        extendedDynamicState3FeaturesEXT,
        asFeatures, rtPipeFeatures
#ifdef ZS_PLATFORM_OSX
        , portabilityFeatures
#endif
    );

    // Store enabled features
    this->enabledDeviceFeatures = features;
    this->enabledVk12Features = vk12Features;
    this->enabledVk13Features = vk13Features;

    /// @note Setup pNext chain
    // Chain: [portability ->] extDynamicState -> extDynamicState2 -> extDynamicState3 
    //        -> vk13Features -> vk12Features [-> rtPipeFeatures -> asFeatures]
    
    extendedDynamicStateFeaturesEXT.setPNext(&extendedDynamicState2FeaturesEXT);
    extendedDynamicState2FeaturesEXT.setPNext(&extendedDynamicState3FeaturesEXT);
    extendedDynamicState3FeaturesEXT.setPNext(&vk13Features);
    vk13Features.pNext = &vk12Features;

    // Add ray tracing features if fully supported
    if (rtPreds == rtRequiredPreds) {
        vk12Features.pNext = &rtPipeFeatures;
        rtPipeFeatures.pNext = &asFeatures;
    } else {
        vk12Features.pNext = nullptr;
    }

    /// @note Create device
    vk::DeviceCreateInfo devCI{};
    devCI.setQueueCreateInfoCount(static_cast<u32>(dqCIs.size()))
         .setPQueueCreateInfos(dqCIs.data())
         .setEnabledLayerCount(0)
         .setPpEnabledLayerNames(nullptr)
         .setEnabledExtensionCount(static_cast<u32>(enabledExtensions.size()))
         .setPpEnabledExtensionNames(enabledExtensions.data())
         .setPEnabledFeatures(&features.features);

#ifdef ZS_PLATFORM_OSX
    portabilityFeatures.pNext = &extendedDynamicStateFeaturesEXT;
    devCI.setPNext(&portabilityFeatures);
#else
    devCI.setPNext(&extendedDynamicStateFeaturesEXT);
#endif

    device = physicalDevice.createDevice(devCI, nullptr, dispatcher);
    dispatcher.init(device);
    ZS_ERROR_IF(!device, fmt::format("Vulkan device [{}] failed initialization!\n", devid));

    VkPhysicalDeviceMemoryProperties tmp;
    dispatcher.vkGetPhysicalDeviceMemoryProperties(physicalDevice, &tmp);
    memoryProperties = tmp;

    /// setup additional resources
    // descriptor pool
    setupDescriptorPool();

    // vma allocator
    {
      VmaVulkanFunctions vulkanFunctions = {};
      vulkanFunctions.vkGetInstanceProcAddr = dispatcher.vkGetInstanceProcAddr;
      vulkanFunctions.vkGetDeviceProcAddr = dispatcher.vkGetDeviceProcAddr;

      VmaAllocatorCreateInfo allocatorCreateInfo = {};
      allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
      allocatorCreateInfo.physicalDevice = physicalDevice;
      allocatorCreateInfo.device = device;
      allocatorCreateInfo.instance = instance;
      allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;

      vmaCreateAllocator(&allocatorCreateInfo, &this->defaultAllocator);
    }

    /// @note Display enhanced device info
    fmt::print(fg(fmt::color::cyan),
        "\t[Device {}] {}\n"
        "\t  Queue families: Graphics={}, Compute={}, Transfer={}\n"
        "\t  Features: RT={}, Bindless={}, Timeline={}\n"
        "\t  Vulkan 1.3: Sync2={}, DynamicRender={}, Maintenance4={}\n"
        "\t  Enabled {} extension(s):",
        devid, devProps.deviceName.data(),
        queueFamilyIndices[vk_queue_e::graphics],
        queueFamilyIndices[vk_queue_e::compute],
        queueFamilyIndices[vk_queue_e::transfer],
        (rtPreds == rtRequiredPreds ? "Yes" : "No"),
        (supportBindless() ? "Yes" : "No"),
        (vk12Features.timelineSemaphore ? "Yes" : "No"),
        (configBuilder.features.synchronization2 ? "Yes" : "No"),
        (configBuilder.features.dynamicRendering ? "Yes" : "No"),
        (configBuilder.features.maintenance4 ? "Yes" : "No"),
        enabledExtensions.size());

    u32 accum = 0;
    for (auto ext : enabledExtensions) {
      if ((accum++) % 2 == 0) fmt::print("\n\t    ");
      fmt::print("{}\t", ext);
    }

    fmt::print("\n\t  Managing {} memory type(s) in total:\n",
               memoryProperties.memoryTypeCount);
    for (u32 typeIndex = 0; typeIndex < memoryProperties.memoryTypeCount; ++typeIndex) {
      auto propertyFlags = memoryProperties.memoryTypes[typeIndex].propertyFlags;
      using BitType = typename RM_REF_T(propertyFlags)::MaskType;
      std::string tag;
      if (propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal) tag += "device_local; ";
      if (propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent) tag += "host_coherent; ";
      if (propertyFlags & vk::MemoryPropertyFlagBits::eHostCached) tag += "host_cached; ";
      if (propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible) tag += "host_visible; ";
      if (propertyFlags & vk::MemoryPropertyFlagBits::eProtected) tag += "protected; ";
      if (propertyFlags & vk::MemoryPropertyFlagBits::eLazilyAllocated) tag += "lazily_allocated; ";
      tag += "...";
      fmt::print("\t    [{}] {:0>10b} ({})\n", typeIndex, 
                 static_cast<BitType>(propertyFlags), tag);
    }

    fmt::print("\t  [DESCRIPTOR LIMITS]\n");
    fmt::print("\t    Samplers: {} (update_after_bind: {})\n",
               maxPerStageDescriptorSamplers(), 
               maxPerStageDescriptorUpdateAfterBindSamplers());
    fmt::print("\t    Sampled images: {} (update_after_bind: {})\n",
               maxPerStageDescriptorSampledImages(),
               maxPerStageDescriptorUpdateAfterBindSampledImages());
    fmt::print("\t    Storage images: {} (update_after_bind: {})\n",
               maxPerStageDescriptorStorageImages(),
               maxPerStageDescriptorUpdateAfterBindStorageImages());
    fmt::print("\t    Storage buffers: {} (update_after_bind: {})\n",
               maxPerStageDescriptorStorageBuffers(),
               maxPerStageDescriptorUpdateAfterBindStorageBuffers());
    fmt::print("\t    Uniform buffers: {} (update_after_bind: {})\n",
               maxPerStageDescriptorUniformBuffers(),
               maxPerStageDescriptorUpdateAfterBindUniformBuffers());
    fmt::print("\t    Input attachments: {} (update_after_bind: {})\n",
               maxPerStageDescriptorInputAttachments(),
               maxPerStageDescriptorUpdateAfterBindInputAttachments());
  }

  void VulkanContext::destructDescriptorPool() {
    /// clear resources
    if (supportBindless() && bindlessDescriptorPool != VK_NULL_HANDLE) {
      // descriptor pool resources
      bindlessDescriptorSet = VK_NULL_HANDLE;

      device.destroyDescriptorSetLayout(bindlessDescriptorSetLayout, nullptr, dispatcher);
      bindlessDescriptorSetLayout = VK_NULL_HANDLE;

      device.resetDescriptorPool(bindlessDescriptorPool, vk::DescriptorPoolResetFlags{},
                                 dispatcher);
      device.destroyDescriptorPool(bindlessDescriptorPool, nullptr, dispatcher);
      bindlessDescriptorPool = VK_NULL_HANDLE;
    }
    if (defaultDescriptorPool != VK_NULL_HANDLE) {
      device.resetDescriptorPool(defaultDescriptorPool, vk::DescriptorPoolResetFlags{}, dispatcher);
      device.destroyDescriptorPool(defaultDescriptorPool, nullptr, dispatcher);
      defaultDescriptorPool = VK_NULL_HANDLE;
    }
  }

  void VulkanContext::setupDescriptorPool() {
    /// @brief Calculate appropriate pool sizes based on device limits
    auto calcPoolSize = [](u32 deviceLimit, u32 maxDesired = 1000) -> u32 {
      // Use at most half of device limit, capped at maxDesired
      return std::min(maxDesired, std::max(1u, deviceLimit / 2));
    };

    // Calculate sizes based on device limits
    const u32 uniformPoolSize = calcPoolSize(maxPerStageDescriptorUniformBuffers(), 1000);
    const u32 storagePoolSize = calcPoolSize(maxPerStageDescriptorStorageBuffers(), 1000);
    const u32 samplerPoolSize = calcPoolSize(maxPerStageDescriptorSamplers(), 500);
    const u32 sampledImagePoolSize = calcPoolSize(maxPerStageDescriptorSampledImages(), 1000);
    const u32 storageImagePoolSize = calcPoolSize(maxPerStageDescriptorStorageImages(), 500);
    const u32 inputAttachmentPoolSize = calcPoolSize(maxPerStageDescriptorInputAttachments(), 256);

    /// Default pool - supports common descriptor types
    std::vector<vk::DescriptorPoolSize> defaultPoolSizes;
    defaultPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(uniformPoolSize)
        .setType(vk::DescriptorType::eUniformBufferDynamic));
    defaultPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(uniformPoolSize)
        .setType(vk::DescriptorType::eUniformBuffer));
    defaultPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(sampledImagePoolSize)
        .setType(vk::DescriptorType::eCombinedImageSampler));
    defaultPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(storagePoolSize)
        .setType(vk::DescriptorType::eStorageBuffer));
    defaultPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(storagePoolSize)
        .setType(vk::DescriptorType::eStorageBufferDynamic));
    defaultPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(storageImagePoolSize)
        .setType(vk::DescriptorType::eStorageImage));
    defaultPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(inputAttachmentPoolSize)
        .setType(vk::DescriptorType::eInputAttachment));
    defaultPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(samplerPoolSize)
        .setType(vk::DescriptorType::eSampler));
    defaultPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(sampledImagePoolSize)
        .setType(vk::DescriptorType::eSampledImage));

    const u32 defaultMaxSets = uniformPoolSize + sampledImagePoolSize + storagePoolSize 
                               + storageImagePoolSize + inputAttachmentPoolSize;

    // vk::DescriptorPoolCreateFlags flag = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    vk::DescriptorPoolCreateFlags flag{};
    defaultDescriptorPool
        = device.createDescriptorPool(vk::DescriptorPoolCreateInfo{}
                                          .setPoolSizeCount(static_cast<u32>(defaultPoolSizes.size()))
                                          .setPPoolSizes(defaultPoolSizes.data())
                                          .setMaxSets(defaultMaxSets)
                                          .setFlags(flag),
                                      nullptr, dispatcher);

    bindlessDescriptorPool = VK_NULL_HANDLE;
    bindlessDescriptorSetLayout = VK_NULL_HANDLE;
    bindlessDescriptorSet = VK_NULL_HANDLE;

    if (!supportBindless()) return;
    flag |= vk::DescriptorPoolCreateFlagBits::eUpdateAfterBindEXT;

    /// Bindless pool - uses update-after-bind limits
    auto calcBindlessSize = [](u32 deviceLimit, u32 maxDesired = 1000) -> u32 {
      return std::min(maxDesired, std::max(1u, deviceLimit / 4));
    };

    const u32 bindlessUniformSize = calcBindlessSize(
        maxPerStageDescriptorUpdateAfterBindUniformBuffers(), 1000);
    const u32 bindlessStorageSize = calcBindlessSize(
        maxPerStageDescriptorUpdateAfterBindStorageBuffers(), 1000);
    const u32 bindlessSamplerSize = calcBindlessSize(
        std::min(maxDescriptorSetUpdateAfterBindSamplers(),
                 maxPerStageDescriptorUpdateAfterBindSamplers()), 500);
    const u32 bindlessSampledImageSize = calcBindlessSize(
        maxPerStageDescriptorUpdateAfterBindSampledImages(), 1000);
    const u32 bindlessStorageImageSize = calcBindlessSize(
        maxPerStageDescriptorUpdateAfterBindStorageImages(), 500);
    const u32 bindlessInputAttachmentSize = calcBindlessSize(
        maxPerStageDescriptorUpdateAfterBindInputAttachments(), 256);

    std::vector<vk::DescriptorPoolSize> bindlessPoolSizes;
    bindlessPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(bindlessUniformSize)
        .setType(vk::DescriptorType::eUniformBuffer));
    bindlessPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(bindlessSampledImageSize)
        .setType(vk::DescriptorType::eCombinedImageSampler));
    bindlessPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(bindlessStorageSize)
        .setType(vk::DescriptorType::eStorageBuffer));
    bindlessPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(bindlessStorageImageSize)
        .setType(vk::DescriptorType::eStorageImage));
    bindlessPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(bindlessInputAttachmentSize)
        .setType(vk::DescriptorType::eInputAttachment));
    bindlessPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(bindlessSamplerSize)
        .setType(vk::DescriptorType::eSampler));
    bindlessPoolSizes.push_back(vk::DescriptorPoolSize()
        .setDescriptorCount(bindlessSampledImageSize)
        .setType(vk::DescriptorType::eSampledImage));

    const u32 bindlessMaxSets = bindlessUniformSize + bindlessSampledImageSize + bindlessStorageSize
                                + bindlessStorageImageSize + bindlessInputAttachmentSize;

    bindlessDescriptorPool = device.createDescriptorPool(
        vk::DescriptorPoolCreateInfo{}
            .setPoolSizeCount(static_cast<u32>(bindlessPoolSizes.size()))
            .setPPoolSizes(bindlessPoolSizes.data())
            .setMaxSets(bindlessMaxSets)
            .setFlags(flag),
        nullptr, dispatcher);

    /// Bindless set layout
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    u32 bindingIndex = bindless_texture_binding;

    bindings.push_back(vk::DescriptorSetLayoutBinding{}
        .setBinding(bindingIndex++)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setDescriptorCount(bindlessUniformSize)
        .setStageFlags(vk::ShaderStageFlagBits::eAll));

    bindings.push_back(vk::DescriptorSetLayoutBinding{}
        .setBinding(bindingIndex++)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(std::min(bindlessSampledImageSize, bindlessSamplerSize))
        .setStageFlags(vk::ShaderStageFlagBits::eAll));

    bindings.push_back(vk::DescriptorSetLayoutBinding{}
        .setBinding(bindingIndex++)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(bindlessStorageSize)
        .setStageFlags(vk::ShaderStageFlagBits::eAll));

    bindings.push_back(vk::DescriptorSetLayoutBinding{}
        .setBinding(bindingIndex++)
        .setDescriptorType(vk::DescriptorType::eStorageImage)
        .setDescriptorCount(bindlessStorageImageSize)
        .setStageFlags(vk::ShaderStageFlagBits::eAll));

    bindings.push_back(vk::DescriptorSetLayoutBinding{}
        .setBinding(bindingIndex++)
        .setDescriptorType(vk::DescriptorType::eInputAttachment)
        .setDescriptorCount(bindlessInputAttachmentSize)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment));

    vk::DescriptorBindingFlags bindlessFlag = vk::DescriptorBindingFlagBits::ePartiallyBound
                                              | vk::DescriptorBindingFlagBits::eUpdateAfterBind;
    std::vector<vk::DescriptorBindingFlags> bindingFlags(bindings.size(), bindlessFlag);
    bindingFlags.back() = vk::DescriptorBindingFlagBits::ePartiallyBound;

    auto extendedInfo = vk::DescriptorSetLayoutBindingFlagsCreateInfo{}
        .setBindingCount(static_cast<u32>(bindingFlags.size()))
        .setPBindingFlags(bindingFlags.data());

    bindlessDescriptorSetLayout = device.createDescriptorSetLayout(
        vk::DescriptorSetLayoutCreateInfo{}
            .setFlags(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPoolEXT)
            .setBindingCount(static_cast<u32>(bindings.size()))
            .setPBindings(bindings.data())
            .setPNext(&extendedInfo),
        nullptr, dispatcher);

    bindlessDescriptorSet
        = device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{}
                                            .setDescriptorPool(bindlessDescriptorPool)
                                            .setPSetLayouts(&bindlessDescriptorSetLayout)
                                            .setDescriptorSetCount(1))[0];

    fmt::print("\t  [DESCRIPTOR POOL ALLOCATION]\n");
    fmt::print("\t    Default pool - maxSets: {}\n", defaultMaxSets);
    fmt::print("\t    Bindless pool - maxSets: {}\n", bindlessMaxSets);
    fmt::print("\t    Bindless bindings: uniform={}, sampler={}, storage={}, storageImg={}, input={}\n",
               bindlessUniformSize, std::min(bindlessSampledImageSize, bindlessSamplerSize),
               bindlessStorageSize, bindlessStorageImageSize, bindlessInputAttachmentSize);
  }

  ExecutionContext& VulkanContext::env() {
    WorkerEnvs::iterator workerIter;
    ContextEnvs::iterator iter;
    auto& g_mtx = Vulkan::instance().mutex<Mutex>();
    g_mtx.lock();
    bool tag;
    std::tie(workerIter, tag) = Vulkan::instance().working_contexts<WorkerEnvs>().try_emplace(
        std::this_thread::get_id(), ContextEnvs{});
    std::tie(iter, tag) = workerIter->second.try_emplace(devid, *this);
    g_mtx.unlock();
    return iter->second;
  }
  u32 check_current_working_contexts() {
    return Vulkan::instance().working_contexts<WorkerEnvs>().size();
  }

  ///
  /// builders
  ///
  SwapchainBuilder& VulkanContext::swapchain(vk::SurfaceKHR surface, bool reset) {
    if ((!swapchainBuilder || reset
         || ((SwapchainBuilder*)swapchainBuilder)->getSurface() != surface)
        && surface != VK_NULL_HANDLE)
      swapchainBuilder.reset(new SwapchainBuilder(*this, surface));
    if (swapchainBuilder)
      return *(SwapchainBuilder*)swapchainBuilder;
    else
      throw std::runtime_error(
          "swapchain builder of the vk context must be initialized by a surface first before use");
  }
  PipelineBuilder VulkanContext::pipeline() { return PipelineBuilder{*this}; }
  RenderPassBuilder VulkanContext::renderpass() { return RenderPassBuilder(*this); }
  DescriptorSetLayoutBuilder VulkanContext::setlayout() {
    return DescriptorSetLayoutBuilder{*this};
  }

  image_handle_t VulkanContext::registerImage(const VkTexture& img) {
    if (!supportBindless()) return (image_handle_t)-1;
    image_handle_t ret = registeredImages.size();
    registeredImages.push_back(&img);

    vk::DescriptorImageInfo imageInfo{};
    imageInfo.sampler = img.sampler;
    imageInfo.imageView = (vk::ImageView)img.image.get();
    imageInfo.imageLayout = img.imageLayout;

    // if ((vk::ImageView)img.image.get() == VK_NULL_HANDLE)
    //   throw std::runtime_error("the registered texture image view handle is null\n");

    vk::WriteDescriptorSet write{};
    write.dstSet = bindlessDescriptorSet;
    write.descriptorCount = 1;
    write.dstArrayElement = ret;
    write.pImageInfo = &imageInfo;

    std::vector<vk::WriteDescriptorSet> writes{};
    // Bindless layout bindings (from setupDescriptorPool):
    // 0: uniform, 1: combined_image_sampler, 2: storage, 3: storage_image, 4: input_attachment
    constexpr u32 bindlessCombinedImageSamplerBinding = bindless_texture_binding + 1;
    constexpr u32 bindlessStorageImageBinding = bindless_texture_binding + 3;
    
    if ((img.image.get().usage & vk::ImageUsageFlagBits::eSampled)
        == vk::ImageUsageFlagBits::eSampled) {
      write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
      write.dstBinding = bindlessCombinedImageSamplerBinding;
      writes.push_back(write);
    }
    if ((img.image.get().usage & vk::ImageUsageFlagBits::eStorage)
        == vk::ImageUsageFlagBits::eStorage) {
      write.descriptorType = vk::DescriptorType::eStorageImage;
      write.dstBinding = bindlessStorageImageBinding;
      writes.push_back(write);
    }

    device.updateDescriptorSets(writes.size(), writes.data(), 0, nullptr, dispatcher);
    return ret;
  }

  buffer_handle_t VulkanContext::registerBuffer(const Buffer& buffer) {
    if (!supportBindless()) return (buffer_handle_t)-1;
    buffer_handle_t ret = registeredBuffers.size();
    registeredBuffers.push_back(&buffer);

    vk::DescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = buffer.getSize();

    vk::WriteDescriptorSet write{};
    write.dstSet = bindlessDescriptorSet;
    write.descriptorCount = 1;
    write.dstArrayElement = ret;
    write.pBufferInfo = &bufferInfo;

    std::vector<vk::WriteDescriptorSet> writes{};
    
    // Bindless layout bindings (from setupDescriptorPool):
    // 0: uniform, 1: combined_image_sampler, 2: storage, 3: storage_image, 4: input_attachment
    constexpr u32 bindlessUniformBinding = bindless_texture_binding;
    constexpr u32 bindlessStorageBinding = bindless_texture_binding + 2;
    
    if ((buffer.usageFlags & vk::BufferUsageFlagBits::eUniformBuffer)
        == vk::BufferUsageFlagBits::eUniformBuffer) {
      write.descriptorType = vk::DescriptorType::eUniformBuffer;
      write.dstBinding = bindlessUniformBinding;
      writes.push_back(write);
    }
    if ((buffer.usageFlags & vk::BufferUsageFlagBits::eStorageBuffer)
        == vk::BufferUsageFlagBits::eStorageBuffer) {
      write.descriptorType = vk::DescriptorType::eStorageBuffer;
      write.dstBinding = bindlessStorageBinding;
      writes.push_back(write);
    }

    device.updateDescriptorSets(writes.size(), writes.data(), 0, nullptr, dispatcher);
    return ret;
  }

  Buffer VulkanContext::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                                     vk::MemoryPropertyFlags props, const source_location& loc) {
    Buffer buffer(*this);

    vk::BufferCreateInfo bufCI{};
    bufCI.setUsage(usage);
    bufCI.setSize(size);
    bufCI.setSharingMode(vk::SharingMode::eExclusive);
    auto buf = device.createBuffer(bufCI, nullptr, dispatcher);

#if ZS_ENABLE_VULKAN_VALIDATION
    vk::DebugUtilsObjectNameInfoEXT objNameInfo{};
    objNameInfo.objectType = vk::ObjectType::eBuffer;
    objNameInfo.objectHandle = reinterpret_cast<u64>((VkBuffer)buf);
    auto name = fmt::format("[[ zs::Buffer (File: {}, Ln {}, Col {}, Device: {}) ]]",
                            loc.file_name(), loc.line(), loc.column(), devid);
    objNameInfo.pObjectName = name.c_str();
    device.setDebugUtilsObjectNameEXT(objNameInfo, dispatcher);
#endif

#if ZS_VULKAN_USE_VMA
    auto bufferReqs = vk::BufferMemoryRequirementsInfo2{}.setBuffer(buf);
    auto dedicatedReqs = vk::MemoryDedicatedRequirements{};
    dedicatedReqs.prefersDedicatedAllocation = true;
    auto memReqs2 = vk::MemoryRequirements2{};
    memReqs2.pNext = &dedicatedReqs;

    device.getBufferMemoryRequirements2(&bufferReqs, &memReqs2, dispatcher);

    auto& memRequirements = memReqs2.memoryRequirements;

    VmaAllocationCreateInfo vmaAllocCI = {};
    if (dedicatedReqs.requiresDedicatedAllocation)
      vmaAllocCI.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    vmaAllocCI.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    vmaAllocCI.usage = VMA_MEMORY_USAGE_UNKNOWN;
    // vmaAllocCI.usage = vk_to_vma_memory_usage(props);  // deprecated
    vmaAllocCI.requiredFlags = static_cast<VkMemoryPropertyFlags>(props);
    vmaAllocCI.priority = 1.f;

    VmaAllocationInfo allocationDetail;
    VmaAllocation allocation = nullptr;
    VkResult result
        = vmaAllocateMemory(allocator(), reinterpret_cast<VkMemoryRequirements*>(&memRequirements),
                            &vmaAllocCI, &allocation, &allocationDetail);
    if (result != VK_SUCCESS)
      throw std::runtime_error(fmt::format("buffer allocation of {} bytes failed!", size));

    device.bindBufferMemory(buf, allocationDetail.deviceMemory, allocationDetail.offset,
                            dispatcher);
#else
    vk::MemoryRequirements memRequirements = device.getBufferMemoryRequirements(buf, dispatcher);
    u32 memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, props);
    vk::MemoryAllocateInfo allocInfo{memRequirements.size, memoryTypeIndex};
    auto mem = device.allocateMemory(allocInfo, nullptr, dispatcher);

    device.bindBufferMemory(buf, mem, 0, dispatcher);
#endif

    buffer.size = size;
    buffer.usageFlags = usage;
    buffer.alignment = memRequirements.alignment;
    buffer.buffer = buf;

#if ZS_VULKAN_USE_VMA
    buffer.allocation = allocation;
#else
    VkMemory memory{*this};
    memory.mem = mem;
    memory.memSize = memRequirements.size;
    memory.memoryPropertyFlags = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
    buffer.pmem = std::make_shared<VkMemory>(std::move(memory));
#endif

    return buffer;
  }
  Buffer VulkanContext::createStagingBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                                            const source_location& loc) {
    return createBuffer(
        size, usage,
        vk::MemoryPropertyFlagBits::eHostVisible /* | vk::MemoryPropertyFlagBits::eHostCoherent*/,
        loc);
  }

  ImageSampler VulkanContext::createSampler(const vk::SamplerCreateInfo& samplerCI,
                                            const source_location& loc) {
    ImageSampler ret{*this};
    ret.sampler = device.createSampler(samplerCI, nullptr, dispatcher);

#if ZS_ENABLE_VULKAN_VALIDATION
    vk::DebugUtilsObjectNameInfoEXT objNameInfo{};
    objNameInfo.objectType = vk::ObjectType::eSampler;
    objNameInfo.objectHandle = reinterpret_cast<u64>((VkSampler)(*ret));
    auto name = fmt::format("[[ zs::Sampler (File: {}, Ln {}, Col {}, Device: {}) ]]",
                            loc.file_name(), loc.line(), loc.column(), devid);
    objNameInfo.pObjectName = name.c_str();
    device.setDebugUtilsObjectNameEXT(objNameInfo, dispatcher);
#endif
    return ret;
  }
  ImageSampler VulkanContext::createDefaultSampler(const source_location& loc) {
    return createSampler(vk::SamplerCreateInfo{}
                             .setMaxAnisotropy(1.f)
                             .setMagFilter(vk::Filter::eLinear)
                             .setMinFilter(vk::Filter::eLinear)
                             .setMipmapMode(vk::SamplerMipmapMode::eLinear)
                             .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
                             .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
                             .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
                             .setBorderColor(vk::BorderColor::eFloatOpaqueWhite),
                         loc);
  }

  Image VulkanContext::createImage(vk::ImageCreateInfo imageCI, vk::MemoryPropertyFlags props,
                                   bool createView, const source_location& loc) {
    Image image{*this};
    image.usage = imageCI.usage;
    image.extent = imageCI.extent;
    image.mipLevels = imageCI.mipLevels;

    auto img = device.createImage(imageCI, nullptr, dispatcher);

#if ZS_ENABLE_VULKAN_VALIDATION
    vk::DebugUtilsObjectNameInfoEXT objNameInfo{};
    objNameInfo.objectType = vk::ObjectType::eImage;
    objNameInfo.objectHandle = reinterpret_cast<u64>((VkImage)img);
    auto name = fmt::format("[[ zs::Image (File: {}, Ln {}, Col {}, Device: {}) ]]",
                            loc.file_name(), loc.line(), loc.column(), devid);
    objNameInfo.pObjectName = name.c_str();
    device.setDebugUtilsObjectNameEXT(objNameInfo, dispatcher);
#endif

#if ZS_VULKAN_USE_VMA
    auto imageReqs = vk::ImageMemoryRequirementsInfo2{}.setImage(img);
    auto dedicatedReqs = vk::MemoryDedicatedRequirements{};
    dedicatedReqs.prefersDedicatedAllocation = true;
    auto memReqs2 = vk::MemoryRequirements2{};
    memReqs2.pNext = &dedicatedReqs;

    device.getImageMemoryRequirements2(&imageReqs, &memReqs2, dispatcher);

    auto& memRequirements = memReqs2.memoryRequirements;

    VmaAllocationCreateInfo vmaAllocCI = {};
    if (dedicatedReqs.requiresDedicatedAllocation)
      vmaAllocCI.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    vmaAllocCI.usage = VMA_MEMORY_USAGE_UNKNOWN;
    // vmaAllocCI.usage = vk_to_vma_memory_usage(props);  // deprecated
    vmaAllocCI.requiredFlags = static_cast<VkMemoryPropertyFlags>(props);
    vmaAllocCI.priority = 1.f;

    VmaAllocationInfo allocationDetail;
    VmaAllocation allocation = nullptr;
    VkResult result
        = vmaAllocateMemory(allocator(), reinterpret_cast<VkMemoryRequirements*>(&memRequirements),
                            &vmaAllocCI, &allocation, &allocationDetail);
    if (result != VK_SUCCESS)
      throw std::runtime_error(fmt::format("image allocation of dim [{}, {}] failed!",
                                           imageCI.extent.width, imageCI.extent.height));

    device.bindImageMemory(img, allocationDetail.deviceMemory, allocationDetail.offset, dispatcher);
#else
    vk::MemoryRequirements memRequirements = device.getImageMemoryRequirements(img, dispatcher);
    u32 memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, props);
    vk::MemoryAllocateInfo allocInfo{memRequirements.size, memoryTypeIndex};
    auto mem = device.allocateMemory(allocInfo, nullptr, dispatcher);

    device.bindImageMemory(img, mem, 0, dispatcher);
#endif

    image.image = img;
#if ZS_VULKAN_USE_VMA
    image.allocation = allocation;
#else
    VkMemory memory{*this};
    memory.mem = mem;
    memory.memSize = memRequirements.size;
    memory.memoryPropertyFlags = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
    image.pmem = std::make_shared<VkMemory>(std::move(memory));
#endif
    if (createView) {
      image.pview = device.createImageView(
          vk::ImageViewCreateInfo{}
              .setImage(img)
              .setPNext(nullptr)
              .setViewType(vk::ImageViewType::e2D)
              .setFormat(imageCI.format)
              .setSubresourceRange(vk::ImageSubresourceRange{
                  is_depth_stencil_format(imageCI.format) ? vk::ImageAspectFlagBits::eDepth
                                                          : vk::ImageAspectFlagBits::eColor,
                  0, 1 /*VK_REMAINING_MIP_LEVELS*/, 0, 1
                  /*VK_REMAINING_ARRAY_LAYERS*/}),
          nullptr, dispatcher);

#if ZS_ENABLE_VULKAN_VALIDATION
      vk::DebugUtilsObjectNameInfoEXT objNameInfo{};
      objNameInfo.objectType = vk::ObjectType::eImageView;
      objNameInfo.objectHandle = reinterpret_cast<u64>((VkImageView)image.view());
      auto name = fmt::format("[[ zs::ImageView (File: {}, Ln {}, Col {}, Device: {}) ]]",
                              loc.file_name(), loc.line(), loc.column(), devid);
      objNameInfo.pObjectName = name.c_str();
      device.setDebugUtilsObjectNameEXT(objNameInfo, dispatcher);
#endif
    }

    return image;
  }
  Image VulkanContext::create2DImage(const vk::Extent2D& dim, vk::Format format,
                                     vk::ImageUsageFlags usage, vk::MemoryPropertyFlags props,
                                     bool mipmaps, bool createView, bool enableTransfer,
                                     vk::SampleCountFlagBits sampleBits,
                                     const source_location& loc) {
    return createImage(vk::ImageCreateInfo{}
                           .setImageType(vk::ImageType::e2D)
                           .setFormat(format)
                           .setExtent({dim.width, dim.height, (u32)1})
                           .setMipLevels((mipmaps ? get_num_mip_levels(dim) : 1))
                           .setArrayLayers(1)
                           .setUsage(enableTransfer ? (usage | vk::ImageUsageFlagBits::eTransferSrc
                                                       | vk::ImageUsageFlagBits::eTransferDst)
                                                    : usage)
                           .setSamples(sampleBits)
                           //.setTiling(vk::ImageTiling::eOptimal)
                           .setSharingMode(vk::SharingMode::eExclusive),
                       props, createView, loc);
  }
  Image VulkanContext::createOptimal2DImage(const vk::Extent2D& dim, vk::Format format,
                                            vk::ImageUsageFlags usage,
                                            vk::MemoryPropertyFlags props, bool mipmaps,
                                            bool createView, bool enableTransfer,
                                            vk::SampleCountFlagBits sampleBits,
                                            const source_location& loc) {
    return createImage(vk::ImageCreateInfo{}
                           .setImageType(vk::ImageType::e2D)
                           .setFormat(format)
                           .setExtent({dim.width, dim.height, (u32)1})
                           .setMipLevels((mipmaps ? get_num_mip_levels(dim) : 1))
                           .setArrayLayers(1)
                           .setUsage(enableTransfer ? (usage | vk::ImageUsageFlagBits::eTransferSrc
                                                       | vk::ImageUsageFlagBits::eTransferDst)
                                                    : usage)
                           .setSamples(sampleBits)
                           .setTiling(vk::ImageTiling::eOptimal)
                           .setSharingMode(vk::SharingMode::eExclusive),
                       props, createView, loc);
  }
  Image VulkanContext::createInputAttachment(const vk::Extent2D& dim, vk::Format format,
                                             vk::ImageUsageFlags usage, bool enableTransfer,
                                             const source_location& loc) {
    usage |= vk::ImageUsageFlagBits::eInputAttachment;
    return createImage(vk::ImageCreateInfo{}
                           .setImageType(vk::ImageType::e2D)
                           .setFormat(format)
                           .setExtent({dim.width, dim.height, (u32)1})
                           .setMipLevels(1)
                           .setArrayLayers(1)
                           .setUsage(enableTransfer ? (usage | vk::ImageUsageFlagBits::eTransferSrc
                                                       | vk::ImageUsageFlagBits::eTransferDst)
                                                    : usage)
                           .setSamples(vk::SampleCountFlagBits::e1)
                           // .setTiling(vk::ImageTiling::eOptimal)
                           .setSharingMode(vk::SharingMode::eExclusive),
                       vk::MemoryPropertyFlagBits::eDeviceLocal, true, loc);
  }
  ImageView VulkanContext::create2DImageView(vk::Image image, vk::Format format,
                                             vk::ImageAspectFlags aspect, u32 levels,
                                             const void* pNextImageView,
                                             const source_location& loc) {
    ImageView imgv{*this};
    imgv.imgv = device.createImageView(
        vk::ImageViewCreateInfo{}
            .setImage(image)
            .setPNext(pNextImageView)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(format)
            .setSubresourceRange(vk::ImageSubresourceRange{aspect, 0, levels, 0, 1}),
        nullptr, dispatcher);

#if ZS_ENABLE_VULKAN_VALIDATION
    vk::DebugUtilsObjectNameInfoEXT objNameInfo{};
    objNameInfo.objectType = vk::ObjectType::eImageView;
    objNameInfo.objectHandle = reinterpret_cast<u64>((VkImageView)(*imgv));
    auto name = fmt::format("[[ zs::ImageView (File: {}, Ln {}, Col {}, Device: {}) ]]",
                            loc.file_name(), loc.line(), loc.column(), devid);
    objNameInfo.pObjectName = name.c_str();
    device.setDebugUtilsObjectNameEXT(objNameInfo, dispatcher);
#endif
    return imgv;
  }

  Framebuffer VulkanContext::createFramebuffer(const std::vector<vk::ImageView>& imageViews,
                                               vk::Extent2D extent, vk::RenderPass renderPass,
                                               const source_location& loc) {
    Framebuffer obj{*this};
    auto ci = vk::FramebufferCreateInfo{
        {},    renderPass, (u32)imageViews.size(), imageViews.data(), extent.width, extent.height,
        (u32)1};
    obj.framebuffer = device.createFramebuffer(ci, nullptr, dispatcher);

#if ZS_ENABLE_VULKAN_VALIDATION
    vk::DebugUtilsObjectNameInfoEXT objNameInfo{};
    objNameInfo.objectType = vk::ObjectType::eFramebuffer;
    objNameInfo.objectHandle = reinterpret_cast<u64>((VkFramebuffer)(*obj));
    auto name = fmt::format("[[ zs::Framebuffer (File: {}, Ln {}, Col {}, Device: {}) ]]",
                            loc.file_name(), loc.line(), loc.column(), devid);
    objNameInfo.pObjectName = name.c_str();
    device.setDebugUtilsObjectNameEXT(objNameInfo, dispatcher);
#endif
    return obj;
  }

  QueryPool VulkanContext::createQueryPool(vk::QueryType queryType, u32 queryCount) {
    QueryPool q{*this};
    auto info = vk::QueryPoolCreateInfo{}.setQueryType(queryType).setQueryCount(queryCount);
    q.queryPool = this->device.createQueryPool(info, nullptr, dispatcher);
    q.queryType = queryType;
    q.queryCount = queryCount;
    return q;
  }

  BinarySemaphore VulkanContext::createBinarySemaphore(const source_location& loc) {
    BinarySemaphore ret{*this};
    ret.semaphore = device.createSemaphore(vk::SemaphoreCreateInfo{}, nullptr, dispatcher);

#if ZS_ENABLE_VULKAN_VALIDATION
    vk::DebugUtilsObjectNameInfoEXT objNameInfo{};
    objNameInfo.objectType = vk::ObjectType::eSemaphore;
    objNameInfo.objectHandle = reinterpret_cast<u64>((VkSemaphore)ret.semaphore);
    auto name = fmt::format("[[ zs::BinarySemaphore (File: {}, Ln {}, Col {}, Device: {}) ]]",
                            loc.file_name(), loc.line(), loc.column(), devid);
    objNameInfo.pObjectName = name.c_str();
    device.setDebugUtilsObjectNameEXT(objNameInfo, dispatcher);
#endif
    return ret;
  }

  TimelineSemaphore VulkanContext::createTimelineSemaphore(u64 initialValue,
                                                           const source_location& loc) {
    TimelineSemaphore ret{*this, VK_NULL_HANDLE, initialValue};

    vk::SemaphoreTypeCreateInfo timelineCI{};
    timelineCI.setSemaphoreType(vk::SemaphoreType::eTimeline).setInitialValue(initialValue);

    vk::SemaphoreCreateInfo semaphoreCI{};
    semaphoreCI.setPNext(&timelineCI);

    ret.semaphore = device.createSemaphore(semaphoreCI, nullptr, dispatcher);

#if ZS_ENABLE_VULKAN_VALIDATION
    vk::DebugUtilsObjectNameInfoEXT objNameInfo{};
    objNameInfo.objectType = vk::ObjectType::eSemaphore;
    objNameInfo.objectHandle = reinterpret_cast<u64>((VkSemaphore)ret.semaphore);
    auto name = fmt::format("[[ zs::TimelineSemaphore (File: {}, Ln {}, Col {}, Device: {}) ]]",
                            loc.file_name(), loc.line(), loc.column(), devid);
    objNameInfo.pObjectName = name.c_str();
    device.setDebugUtilsObjectNameEXT(objNameInfo, dispatcher);
#endif
    return ret;
  }

  VkCommand VulkanContext::createCommandBuffer(vk_cmd_usage_e usage, vk_queue_e queueFamily,
                                               bool begin, const source_location& loc) {
    auto& pool = env().pools(queueFamily);
    auto cmd = pool.createCommandBuffer(vk::CommandBufferLevel::ePrimary, begin,
                                        /*inheritance info*/ nullptr, usage);

#if ZS_ENABLE_VULKAN_VALIDATION
    vk::DebugUtilsObjectNameInfoEXT objNameInfo{};
    objNameInfo.objectType = vk::ObjectType::eCommandBuffer;
    objNameInfo.objectHandle = reinterpret_cast<u64>((VkCommandBuffer)cmd);
    auto name = fmt::format("[[ zs::CommandBuffer (File: {}, Ln {}, Col {}, Device: {}) ]]",
                            loc.file_name(), loc.line(), loc.column(), devid);
    objNameInfo.pObjectName = name.c_str();
    device.setDebugUtilsObjectNameEXT(objNameInfo, dispatcher);
#endif
    return VkCommand{pool, cmd, usage};
  }

  DescriptorPool VulkanContext::createDescriptorPool(
      const std::vector<vk::DescriptorPoolSize>& poolSizes, u32 maxSets,
      const source_location& loc) {
    /// @note DescriptorPoolSize: descriptorCount, vk::DescriptorType::eUniformBufferDynamic
    auto poolCreateInfo = vk::DescriptorPoolCreateInfo()
                              .setMaxSets(maxSets)
                              .setPoolSizeCount((u32)poolSizes.size())
                              // .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
                              .setPPoolSizes(poolSizes.data());
    DescriptorPool ret{*this};
    ret.descriptorPool = device.createDescriptorPool(poolCreateInfo, nullptr, dispatcher);

#if ZS_ENABLE_VULKAN_VALIDATION
    vk::DebugUtilsObjectNameInfoEXT objNameInfo{};
    objNameInfo.objectType = vk::ObjectType::eDescriptorPool;
    objNameInfo.objectHandle = reinterpret_cast<u64>((VkDescriptorPool)(*ret));
    auto name = fmt::format("[[ zs::DescriptorPool (File: {}, Ln {}, Col {}, Device: {}) ]]",
                            loc.file_name(), loc.line(), loc.column(), devid);
    objNameInfo.pObjectName = name.c_str();
    device.setDebugUtilsObjectNameEXT(objNameInfo, dispatcher);
#endif
    return ret;
  }
  void VulkanContext::writeDescriptorSet(const vk::DescriptorBufferInfo& bufferInfo,
                                         vk::DescriptorSet dstSet, vk::DescriptorType type,
                                         u32 binding, u32 dstArrayNo) {
    auto write = vk::WriteDescriptorSet{}
                     .setDescriptorType(type)
                     .setDstSet(dstSet)
                     .setDstBinding(binding)
                     .setDstArrayElement(dstArrayNo)
                     .setDescriptorCount((u32)1)
                     .setPBufferInfo(&bufferInfo);
    device.updateDescriptorSets(1, &write, 0, nullptr, dispatcher);
  }
  void VulkanContext::writeDescriptorSet(const vk::DescriptorImageInfo& imageInfo,
                                         vk::DescriptorSet dstSet, vk::DescriptorType type,
                                         u32 binding, u32 dstArrayNo) {
    auto write = vk::WriteDescriptorSet{}
                     .setDescriptorType(type)
                     .setDstSet(dstSet)
                     .setDstBinding(binding)
                     .setDstArrayElement(dstArrayNo)
                     .setDescriptorCount((u32)1)
                     .setPImageInfo(&imageInfo);
    device.updateDescriptorSets(1, &write, 0, nullptr, dispatcher);
  }

  ///
  ///
  /// working context (CmdContext)
  ///
  ///
  ExecutionContext::ExecutionContext(VulkanContext& ctx)
      : ctx{ctx}, poolFamilies(ctx.numDistinctQueueFamilies()) {
    for (const auto& [no, family, queueFamilyIndex] :
         enumerate(poolFamilies, ctx.uniqueQueueFamilyIndices)) {
      family.reusePool = ctx.device.createCommandPool(
          vk::CommandPoolCreateInfo{{}, queueFamilyIndex}, nullptr, ctx.dispatcher);
      /// @note for memory allcations, etc.
      family.singleUsePool = ctx.device.createCommandPool(
          vk::CommandPoolCreateInfo{vk::CommandPoolCreateFlagBits::eTransient, queueFamilyIndex},
          nullptr, ctx.dispatcher);
      family.resetPool = ctx.device.createCommandPool(
          vk::CommandPoolCreateInfo{vk::CommandPoolCreateFlagBits::eTransient
                                        | vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                    queueFamilyIndex},
          nullptr, ctx.dispatcher);
      /// setup preset primary command buffers (reuse)
      family.primaryCmd
          = new VkCommand(family,
                          ctx.device.allocateCommandBuffers(
                              vk::CommandBufferAllocateInfo{
                                  family.resetPool, vk::CommandBufferLevel::ePrimary, (u32)1},
                              ctx.dispatcher)[0],
                          vk_cmd_usage_e::reset);
      family.fence = new Fence(ctx, true);

      //
      family.queue = ctx.device.getQueue(queueFamilyIndex, 0, ctx.dispatcher);
      family.allQueues.resize(ctx.getQueueFamilyPropertyByIndex(no).queueCount);
      for (int i = 0; i < family.allQueues.size(); ++i)
        family.allQueues[i] = ctx.device.getQueue(queueFamilyIndex, i, ctx.dispatcher);
      family.pctx = &ctx;
    }
  }
  ExecutionContext::~ExecutionContext() {
    for (auto& family : poolFamilies) {
      /// @brief clear secondary command buffers before destroying command pools
      if (family.primaryCmd) {
        delete family.primaryCmd;
        family.primaryCmd = nullptr;
      }
      if (family.fence) {
        delete family.fence;
        family.fence = nullptr;
      }
      for (auto& ptr : family.secondaryCmds)
        if (ptr) delete ptr;
      family.secondaryCmds.clear();
#if 0
      // reset and reuse
      for (auto& cmd : family.cmds)
        ctx.device.freeCommandBuffers(family.singleUsePool, cmd, ctx.dispatcher);
      family.cmds.clear();
#endif

      ctx.device.resetCommandPool(family.reusePool, vk::CommandPoolResetFlagBits::eReleaseResources,
                                  ctx.dispatcher);
      ctx.device.destroyCommandPool(family.reusePool, nullptr, ctx.dispatcher);

      ctx.device.resetCommandPool(family.singleUsePool,
                                  vk::CommandPoolResetFlagBits::eReleaseResources, ctx.dispatcher);
      ctx.device.destroyCommandPool(family.singleUsePool, nullptr, ctx.dispatcher);

      ctx.device.resetCommandPool(family.resetPool, vk::CommandPoolResetFlagBits::eReleaseResources,
                                  ctx.dispatcher);
      ctx.device.destroyCommandPool(family.resetPool, nullptr, ctx.dispatcher);
    }
  }
  VkCommand ExecutionContext::PoolFamily::createVkCommand(vk_cmd_usage_e usage, bool begin,
                                                          const source_location& loc) {
    const auto& cmdPool = cmdpool(usage);
    std::vector<vk::CommandBuffer> cmd = pctx->device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo{cmdPool, vk::CommandBufferLevel::ePrimary, (u32)1},
        pctx->dispatcher);

#if ZS_ENABLE_VULKAN_VALIDATION
    for (const auto& cmd_ : cmd) {
      vk::DebugUtilsObjectNameInfoEXT objNameInfo{};
      objNameInfo.objectType = vk::ObjectType::eCommandBuffer;
      objNameInfo.objectHandle = reinterpret_cast<u64>((VkCommandBuffer)cmd_);
      auto name = fmt::format(
          "[[ zs::CommandBuffer (File: {}, Ln {}, Col {}, Device: {}, Thread: {}) ]]",
          loc.file_name(), loc.line(), loc.column(), pctx->getDevId(), std::this_thread::get_id());
      objNameInfo.pObjectName = name.c_str();
      pctx->device.setDebugUtilsObjectNameEXT(objNameInfo, pctx->dispatcher);
    }
#endif

    VkCommand ret{*this, cmd[0], usage};
    if (begin) ret.begin();
    return ret;
  }
  VkCommand& ExecutionContext::PoolFamily::acquireSecondaryVkCommand() {
    auto cmdPtr
        = new VkCommand(*this,
                        createCommandBuffer(vk::CommandBufferLevel::eSecondary, false,
                                            /*inheritance info*/ nullptr, vk_cmd_usage_e::reset),
                        vk_cmd_usage_e::reset);
    secondaryCmds.emplace_back(cmdPtr);
    secondaryCmdHandles.emplace_back(*secondaryCmds.back());
    return *secondaryCmds.back();
  }
  VkCommand& ExecutionContext::PoolFamily::acquireSecondaryVkCommand(int k) {
    while (k >= secondaryCmds.size()) acquireSecondaryVkCommand();
    return *secondaryCmds[k];
  }

  const VkCommand& ExecutionContext::PoolFamily::retrieveSecondaryVkCommand(int k) const {
    assert(k >= 0 && k < secondaryCmds.size());
    return *secondaryCmds[k];
  }

  std::vector<vk::CommandBuffer> ExecutionContext::PoolFamily::retrieveSecondaryVkCommands(
      int n) const {
    if (n < 0 || n >= secondaryCmdHandles.size()) return secondaryCmdHandles;
    std::vector<vk::CommandBuffer> ret(n);
    for (int i = 0; i < n; ++i) ret.push_back(secondaryCmdHandles[i]);
    return ret;
  }

}  // namespace zs