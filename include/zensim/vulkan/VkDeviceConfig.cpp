#include "VkDeviceConfig.hpp"
#include "zensim/Platform.hpp"
#include <algorithm>
#include <set>

namespace zs {

// Extension categories implementation
std::vector<const char*> VkDeviceConfig::ExtensionSet::flatten() const {
  std::vector<const char*> result;
  auto append = [&](const std::vector<const char*>& vec) {
    result.insert(result.end(), vec.begin(), vec.end());
  };
  
  append(rayTracing);
  append(swapchain);
  append(dynamicState);
  append(renderPass);
  append(platform);
  append(debug);
  append(synchronization);
  
  return result;
}

// Feature config implementation
void VkDeviceConfig::FeatureConfig::applyToVkStructures(
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
) const {
  // Core features
  features.features.fragmentStoresAndAtomics = fragmentStoresAndAtomics;
  features.features.vertexPipelineStoresAndAtomics = vertexPipelineStoresAndAtomics;
  features.features.fillModeNonSolid = fillModeNonSolid;
  features.features.wideLines = wideLines;
  features.features.independentBlend = independentBlend;
  features.features.geometryShader = geometryShader;
  features.features.tessellationShader = tessellationShader;

  // Vulkan 1.2 features
  vk12Features.timelineSemaphore = timelineSemaphore;
  vk12Features.descriptorIndexing = descriptorIndexing;
  vk12Features.bufferDeviceAddress = bufferDeviceAddress;
  
  // Bindless features
  vk12Features.descriptorBindingPartiallyBound = descriptorBindingPartiallyBound;
  vk12Features.runtimeDescriptorArray = runtimeDescriptorArray;
  vk12Features.descriptorBindingVariableDescriptorCount = descriptorBindingVariableDescriptorCount;
  vk12Features.shaderSampledImageArrayNonUniformIndexing = shaderSampledImageArrayNonUniformIndexing;
  vk12Features.descriptorBindingUpdateUnusedWhilePending = descriptorBindingUpdateUnusedWhilePending;
  
  // Update after bind features
  vk12Features.descriptorBindingUniformBufferUpdateAfterBind = descriptorBindingUniformBufferUpdateAfterBind;
  vk12Features.descriptorBindingSampledImageUpdateAfterBind = descriptorBindingSampledImageUpdateAfterBind;
  vk12Features.descriptorBindingStorageBufferUpdateAfterBind = descriptorBindingStorageBufferUpdateAfterBind;
  vk12Features.descriptorBindingStorageImageUpdateAfterBind = descriptorBindingStorageImageUpdateAfterBind;

  // Vulkan 1.3 features
  vk13Features.synchronization2 = synchronization2;
  vk13Features.dynamicRendering = dynamicRendering;
  vk13Features.maintenance4 = maintenance4;

  // Dynamic state features
  extDynamicState.extendedDynamicState = extendedDynamicState;
  extDynamicState2.extendedDynamicState2 = extendedDynamicState2;
  extDynamicState3.extendedDynamicState3DepthClampEnable = extendedDynamicState3DepthClampEnable;
  extDynamicState3.extendedDynamicState3DepthClipEnable = extendedDynamicState3DepthClipEnable;

  // Ray tracing features
  asFeatures.accelerationStructure = accelerationStructure;
  rtPipeFeatures.rayTracingPipeline = rayTracingPipeline;

#ifdef ZS_PLATFORM_OSX
  // Platform-specific features
  portabilityFeatures.triangleFans = triangleFans ? VK_TRUE : VK_FALSE;
#endif
}

// Builder implementation
VkDeviceConfig::Builder& VkDeviceConfig::Builder::withRayTracing(bool enable) {
  if (enable) {
    extensions.rayTracing = {
      "VK_KHR_ray_tracing_pipeline",
      "VK_KHR_acceleration_structure",
      "VK_EXT_descriptor_indexing",
      "VK_KHR_buffer_device_address",
      "VK_KHR_deferred_host_operations"
    };
    features.accelerationStructure = true;
    features.rayTracingPipeline = true;
    features.bufferDeviceAddress = true;
    features.descriptorIndexing = true;
  } else {
    extensions.rayTracing.clear();
    features.accelerationStructure = false;
    features.rayTracingPipeline = false;
  }
  return *this;
}

VkDeviceConfig::Builder& VkDeviceConfig::Builder::withSwapchain(bool enable) {
  if (enable) {
    extensions.swapchain = {"VK_KHR_swapchain"};
  } else {
    extensions.swapchain.clear();
  }
  return *this;
}

VkDeviceConfig::Builder& VkDeviceConfig::Builder::withDynamicState(bool enable) {
  if (enable) {
    extensions.dynamicState = {
      VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
      VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME,
      VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME
    };
    features.extendedDynamicState = true;
    features.extendedDynamicState2 = true;
    features.extendedDynamicState3DepthClampEnable = true;
    features.extendedDynamicState3DepthClipEnable = true;
  } else {
    extensions.dynamicState.clear();
    features.extendedDynamicState = false;
    features.extendedDynamicState2 = false;
  }
  return *this;
}

VkDeviceConfig::Builder& VkDeviceConfig::Builder::withBindless(bool enable) {
  if (enable) {
    features.descriptorBindingPartiallyBound = true;
    features.runtimeDescriptorArray = true;
    features.descriptorBindingVariableDescriptorCount = true;
    features.shaderSampledImageArrayNonUniformIndexing = true;
    features.descriptorBindingUpdateUnusedWhilePending = true;
    features.descriptorBindingUniformBufferUpdateAfterBind = true;
    features.descriptorBindingSampledImageUpdateAfterBind = true;
    features.descriptorBindingStorageBufferUpdateAfterBind = true;
    features.descriptorBindingStorageImageUpdateAfterBind = true;
  } else {
    features.descriptorBindingPartiallyBound = false;
    features.runtimeDescriptorArray = false;
  }
  return *this;
}

VkDeviceConfig::Builder& VkDeviceConfig::Builder::withTimelineSemaphore(bool enable) {
  features.timelineSemaphore = enable;
  return *this;
}

VkDeviceConfig::Builder& VkDeviceConfig::Builder::withGeometryShader(bool enable) {
  features.geometryShader = enable;
  return *this;
}

VkDeviceConfig::Builder& VkDeviceConfig::Builder::withTessellation(bool enable) {
  features.tessellationShader = enable;
  return *this;
}

VkDeviceConfig::Builder& VkDeviceConfig::Builder::withSynchronization2(bool enable) {
  if (enable) {
    extensions.synchronization.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    features.synchronization2 = true;
  } else {
    auto& exts = extensions.synchronization;
    exts.erase(std::remove(exts.begin(), exts.end(), VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME), exts.end());
    features.synchronization2 = false;
  }
  return *this;
}

VkDeviceConfig::Builder& VkDeviceConfig::Builder::withDynamicRendering(bool enable) {
  if (enable) {
    extensions.renderPass.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    features.dynamicRendering = true;
  } else {
    auto& exts = extensions.renderPass;
    exts.erase(std::remove(exts.begin(), exts.end(), VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME), exts.end());
    features.dynamicRendering = false;
  }
  return *this;
}

VkDeviceConfig::Builder& VkDeviceConfig::Builder::withMaintenance4(bool enable) {
  if (enable) {
    extensions.renderPass.push_back(VK_KHR_MAINTENANCE_4_EXTENSION_NAME);
    features.maintenance4 = true;
  } else {
    auto& exts = extensions.renderPass;
    exts.erase(std::remove(exts.begin(), exts.end(), VK_KHR_MAINTENANCE_4_EXTENSION_NAME), exts.end());
    features.maintenance4 = false;
  }
  return *this;
}

VkDeviceConfig::Builder& VkDeviceConfig::Builder::filterBySupported(
  const std::vector<vk::ExtensionProperties>& supportedExtensions,
  const VkPhysicalDeviceFeatures2& supportedFeatures,
  const VkPhysicalDeviceVulkan12Features& supportedVk12Features,
  const VkPhysicalDeviceVulkan13Features& supportedVk13Features
) {
  // Build set of supported extension names
  std::set<std::string> supportedNames;
  for (const auto& ext : supportedExtensions) {
    supportedNames.insert(ext.extensionName);
  }

  // Helper to filter extensions
  auto filterExtList = [&](std::vector<const char*>& extList) {
    extList.erase(
      std::remove_if(extList.begin(), extList.end(),
        [&](const char* ext) {
          return supportedNames.find(ext) == supportedNames.end();
        }),
      extList.end()
    );
  };

  // Filter all extension categories
  filterExtList(extensions.rayTracing);
  filterExtList(extensions.swapchain);
  filterExtList(extensions.dynamicState);
  filterExtList(extensions.renderPass);
  filterExtList(extensions.platform);
  filterExtList(extensions.debug);
  filterExtList(extensions.synchronization);

  // Filter features based on support
  #define CHECK_FEATURE(field) features.field = features.field && supportedFeatures.features.field
  CHECK_FEATURE(fragmentStoresAndAtomics);
  CHECK_FEATURE(vertexPipelineStoresAndAtomics);
  CHECK_FEATURE(fillModeNonSolid);
  CHECK_FEATURE(wideLines);
  CHECK_FEATURE(independentBlend);
  CHECK_FEATURE(geometryShader);
  CHECK_FEATURE(tessellationShader);
  #undef CHECK_FEATURE

  // Filter Vulkan 1.2 features
  #define CHECK_VK12_FEATURE(field) features.field = features.field && supportedVk12Features.field
  CHECK_VK12_FEATURE(timelineSemaphore);
  CHECK_VK12_FEATURE(descriptorIndexing);
  CHECK_VK12_FEATURE(bufferDeviceAddress);
  CHECK_VK12_FEATURE(descriptorBindingPartiallyBound);
  CHECK_VK12_FEATURE(runtimeDescriptorArray);
  CHECK_VK12_FEATURE(descriptorBindingVariableDescriptorCount);
  CHECK_VK12_FEATURE(shaderSampledImageArrayNonUniformIndexing);
  CHECK_VK12_FEATURE(descriptorBindingUpdateUnusedWhilePending);
  CHECK_VK12_FEATURE(descriptorBindingUniformBufferUpdateAfterBind);
  CHECK_VK12_FEATURE(descriptorBindingSampledImageUpdateAfterBind);
  CHECK_VK12_FEATURE(descriptorBindingStorageBufferUpdateAfterBind);
  CHECK_VK12_FEATURE(descriptorBindingStorageImageUpdateAfterBind);
  #undef CHECK_VK12_FEATURE

  // Filter Vulkan 1.3 features
  #define CHECK_VK13_FEATURE(field) features.field = features.field && supportedVk13Features.field
  CHECK_VK13_FEATURE(synchronization2);
  CHECK_VK13_FEATURE(dynamicRendering);
  CHECK_VK13_FEATURE(maintenance4);
  #undef CHECK_VK13_FEATURE

  return *this;
}

// Static factory methods
VkDeviceConfig::Builder VkDeviceConfig::createBuilder(
  const std::vector<vk::ExtensionProperties>& supportedExtensions,
  const VkPhysicalDeviceFeatures2& supportedFeatures,
  const VkPhysicalDeviceVulkan12Features& supportedVk12Features,
  const VkPhysicalDeviceVulkan13Features& supportedVk13Features
) {
  Builder builder;
  
  // Set up default configuration
  builder.extensions.renderPass = {
    VK_KHR_MULTIVIEW_EXTENSION_NAME,
    VK_KHR_MAINTENANCE2_EXTENSION_NAME,
    VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
    VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME,
    "VK_KHR_driver_properties"
  };

#ifdef ZS_PLATFORM_OSX
  builder.extensions.platform = {"VK_KHR_portability_subset"};
  builder.features.triangleFans = true;
#endif

#if ZS_ENABLE_VULKAN_VALIDATION
  builder.extensions.debug = {VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME};
#endif

  // Enable core features that are commonly supported
  builder.features.fragmentStoresAndAtomics = true;
  builder.features.vertexPipelineStoresAndAtomics = true;
  builder.features.fillModeNonSolid = true;
  builder.features.wideLines = true;
  builder.features.independentBlend = true;

  // Filter by supported features/extensions
  builder.filterBySupported(supportedExtensions, supportedFeatures, supportedVk12Features, supportedVk13Features);

  return builder;
}

std::vector<const char*> VkDeviceConfig::getRequiredExtensions() {
  return {"VK_KHR_swapchain"};
}

std::vector<const char*> VkDeviceConfig::getOptionalExtensions() {
  std::vector<const char*> optional = {
    "VK_KHR_ray_tracing_pipeline",
    "VK_KHR_acceleration_structure",
    "VK_EXT_descriptor_indexing",
    "VK_KHR_buffer_device_address",
    "VK_KHR_deferred_host_operations",
    VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
    VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME,
    VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
    VK_KHR_MULTIVIEW_EXTENSION_NAME,
    VK_KHR_MAINTENANCE2_EXTENSION_NAME,
    VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
    VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME,
    VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
    "VK_KHR_driver_properties"
  };

#ifdef ZS_PLATFORM_OSX
  optional.push_back("VK_KHR_portability_subset");
#endif

#if ZS_ENABLE_VULKAN_VALIDATION
  optional.push_back(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
#endif

  return optional;
}

} // namespace zs
