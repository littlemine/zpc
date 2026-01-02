#pragma once

#include "vulkan/vulkan.hpp"
#include <vector>
#include <string>

namespace zs {

/// @brief Helper class for configuring Vulkan device features and extensions
struct VkDeviceConfig {
  /// Extension categories
  struct ExtensionSet {
    std::vector<const char*> rayTracing;
    std::vector<const char*> swapchain;
    std::vector<const char*> dynamicState;
    std::vector<const char*> renderPass;
    std::vector<const char*> platform;
    std::vector<const char*> debug;
    std::vector<const char*> synchronization;
    
    std::vector<const char*> flatten() const;
  };

  /// Feature configuration
  struct FeatureConfig {
    // Core features
    bool fragmentStoresAndAtomics = false;
    bool vertexPipelineStoresAndAtomics = false;
    bool fillModeNonSolid = false;
    bool wideLines = false;
    bool independentBlend = false;
    bool geometryShader = false;
    bool tessellationShader = false;

    // Vulkan 1.2 features
    bool timelineSemaphore = false;
    bool descriptorIndexing = false;
    bool bufferDeviceAddress = false;
    
    // Bindless features
    bool descriptorBindingPartiallyBound = false;
    bool runtimeDescriptorArray = false;
    bool descriptorBindingVariableDescriptorCount = false;
    bool shaderSampledImageArrayNonUniformIndexing = false;
    bool descriptorBindingUpdateUnusedWhilePending = false;
    
    // Update after bind features
    bool descriptorBindingUniformBufferUpdateAfterBind = false;
    bool descriptorBindingSampledImageUpdateAfterBind = false;
    bool descriptorBindingStorageBufferUpdateAfterBind = false;
    bool descriptorBindingStorageImageUpdateAfterBind = false;

    // Dynamic state features
    bool extendedDynamicState = false;
    bool extendedDynamicState2 = false;
    bool extendedDynamicState3DepthClampEnable = false;
    bool extendedDynamicState3DepthClipEnable = false;

    // Ray tracing features
    bool accelerationStructure = false;
    bool rayTracingPipeline = false;

    // Vulkan 1.3 features
    bool synchronization2 = false;
    bool dynamicRendering = false;
    bool maintenance4 = false;

    // Platform-specific features
    bool triangleFans = false; // macOS portability

    void applyToVkStructures(
      vk::PhysicalDeviceFeatures2& features,
      vk::PhysicalDeviceVulkan12Features& vk12Features,
      vk::PhysicalDeviceVulkan13Features& vk13Features,
      vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT& extDynamicState,
      vk::PhysicalDeviceExtendedDynamicState2FeaturesEXT& extDynamicState2,
      vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT& extDynamicState3,
      vk::PhysicalDeviceAccelerationStructureFeaturesKHR& asFeatures,
      vk::PhysicalDeviceRayTracingPipelineFeaturesKHR& rtPipeFeatures
#ifdef ZS_PLATFORM_OSX
      , VkPhysicalDevicePortabilitySubsetFeaturesKHR& portabilityFeatures
#endif
    ) const;
  };

  /// Builder pattern for device configuration
  class Builder {
  public:
    Builder& withRayTracing(bool enable = true);
    Builder& withSwapchain(bool enable = true);
    Builder& withDynamicState(bool enable = true);
    Builder& withBindless(bool enable = true);
    Builder& withTimelineSemaphore(bool enable = true);
    Builder& withGeometryShader(bool enable = true);
    Builder& withTessellation(bool enable = true);
    Builder& withSynchronization2(bool enable = true);
    Builder& withDynamicRendering(bool enable = true);
    Builder& withMaintenance4(bool enable = true);
    
    Builder& filterBySupported(
      const std::vector<vk::ExtensionProperties>& supportedExtensions,
      const VkPhysicalDeviceFeatures2& supportedFeatures,
      const VkPhysicalDeviceVulkan12Features& supportedVk12Features,
      const VkPhysicalDeviceVulkan13Features& supportedVk13Features
    );

    ExtensionSet extensions;
    FeatureConfig features;
  };

  /// Query supported features and fill config
  static Builder createBuilder(
    const std::vector<vk::ExtensionProperties>& supportedExtensions,
    const VkPhysicalDeviceFeatures2& supportedFeatures,
    const VkPhysicalDeviceVulkan12Features& supportedVk12Features,
    const VkPhysicalDeviceVulkan13Features& supportedVk13Features
  );

  /// Get required extensions based on platform
  static std::vector<const char*> getRequiredExtensions();
  
  /// Get optional extensions
  static std::vector<const char*> getOptionalExtensions();
};

} // namespace zs
