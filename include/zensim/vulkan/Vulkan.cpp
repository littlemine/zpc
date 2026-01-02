#include "Vulkan.hpp"

#include <iostream>
#include <map>
#include <set>
#include <thread>

// resources
#include "zensim/vulkan/VkBuffer.hpp"
#include "zensim/vulkan/VkDescriptor.hpp"
#include "zensim/vulkan/VkImage.hpp"
#include "zensim/vulkan/VkPipeline.hpp"
#include "zensim/vulkan/VkRenderPass.hpp"
#include "zensim/vulkan/VkSwapchain.hpp"

//
#include "zensim/Logger.hpp"
#include "zensim/Platform.hpp"
#include "zensim/ZpcReflection.hpp"
#include "zensim/execution/ConcurrencyPrimitive.hpp"
#include "zensim/types/Iterator.h"
#include "zensim/types/SourceLocation.hpp"
#include "zensim/zpc_tpls/fmt/color.h"
#include "zensim/zpc_tpls/fmt/format.h"

namespace {
  std::set<const char *> g_vulkanInstanceExtensions;
  std::map<int, std::set<const char *>> g_vulkanDeviceExtensions;
}  // namespace

namespace zs {

  using ContextEnvs = std::map<int, ExecutionContext>;
  using WorkerEnvs = std::map<std::thread::id, ContextEnvs>;

  // Helper: Get required instance extensions based on platform
  static std::vector<const char *> getRequiredInstanceExtensions() {
    std::vector<const char *> extensions = {
      "VK_KHR_surface",
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
#if ZS_ENABLE_VULKAN_VALIDATION
      "VK_EXT_debug_utils",
#endif
#if defined(ZS_PLATFORM_WINDOWS)
      "VK_KHR_win32_surface"
#elif defined(ZS_PLATFORM_OSX)
      "VK_EXT_metal_surface",
      VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
#elif defined(ZS_PLATFORM_LINUX)
      "VK_KHR_xcb_surface"
#else
      static_assert(false, "unsupported platform for Vulkan instance creation!");
#endif
    };
    return extensions;
  }

  // Helper: Get required validation layers
  static std::vector<const char *> getRequiredValidationLayers() {
#if ZS_ENABLE_VULKAN_VALIDATION
    return {"VK_LAYER_KHRONOS_validation"};
#else
    return {};
#endif
  }

  /// @ref: dokipen3d/vulkanHppMinimalExample
  static VKAPI_ATTR VkBool32 VKAPI_CALL
  zsvk_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                      VkDebugUtilsMessageTypeFlagsEXT messageType,
                      const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData) {
    const char* severityStr = "UNKNOWN";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
      severityStr = "ERROR";
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
      severityStr = "WARNING";
    
    std::cerr << fmt::format("[VALIDATION LAYER - {}]: {}\n", severityStr, pCallbackData->pMessage);
    
    if (pCallbackData->pObjects && pCallbackData->objectCount > 0) {
      std::vector<const char *> names;
      names.reserve(pCallbackData->objectCount);
      for (int i = 0; i < pCallbackData->objectCount; ++i) {
        if (pCallbackData->pObjects[i].pObjectName) {
          names.push_back(pCallbackData->pObjects[i].pObjectName);
        }
      }
      if (!names.empty()) {
        std::cerr << "[VALIDATION LAYER OBJECT NAME(S)]: ";
        for (size_t i = 0; i < names.size(); ++i) {
          std::cerr << "[" << i << "] \"" << names[i] << "\"" << (i < names.size() - 1 ? "; " : "\n");
        }
      }
    }
    return VK_FALSE;
  }

  Vulkan &Vulkan::instance() {
    static Vulkan s_instance{};
    return s_instance;
  }

  Vulkan &Vulkan::driver() noexcept { return instance(); }
  size_t Vulkan::num_devices() noexcept { return instance()._contexts.size(); }
  vk::Instance Vulkan::vk_inst() noexcept { return instance()._instance; }
  const ZS_VK_DISPATCH_LOADER_DYNAMIC &Vulkan::vk_inst_dispatcher() noexcept {
    return instance()._dispatcher;
  }
  VulkanContext &Vulkan::context(int devid) { return driver()._contexts[devid]; }
  VulkanContext &Vulkan::context() { return instance()._contexts[instance()._defaultContext]; }

  /// @ref:
  /// https://github.com/KhronosGroup/Vulkan-Hpp/blob/main/README.md#extensions--per-device-function-pointers
  Vulkan::Vulkan() {
    try {
      fmt::print(fg(fmt::color::dodger_blue), "[Vulkan] Initializing Vulkan subsystem...\n");

      /// @note Initialize dynamic dispatcher
      ZS_VK_DYNAMIC_LOADER dl;
      PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr
          = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
      
      if (!vkGetInstanceProcAddr) {
        throw std::runtime_error("Failed to load vkGetInstanceProcAddr from Vulkan loader");
      }
      
      _dispatcher.init(vkGetInstanceProcAddr);

      /// @note Create Vulkan instance
      vk::ApplicationInfo appInfo{"zpc_app", 0, "zpc", 0, VK_API_VERSION_1_3};

      auto extensions = getRequiredInstanceExtensions();
      auto enabledLayers = getRequiredValidationLayers();

      vk::InstanceCreateInfo instCI{};
      instCI.setPApplicationInfo(&appInfo)
            .setEnabledLayerCount(static_cast<u32>(enabledLayers.size()))
            .setPpEnabledLayerNames(enabledLayers.data())
            .setEnabledExtensionCount(static_cast<u32>(extensions.size()))
            .setPpEnabledExtensionNames(extensions.data());

#ifdef ZS_PLATFORM_OSX
      instCI.flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
#endif

#if ZS_ENABLE_VULKAN_VALIDATION
      std::vector<vk::ValidationFeatureEnableEXT> enabledValidationFeatures{
        vk::ValidationFeatureEnableEXT::eDebugPrintf
      };
      vk::ValidationFeaturesEXT validationFeatures{};
      validationFeatures.enabledValidationFeatureCount = static_cast<u32>(enabledValidationFeatures.size());
      validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures.data();
      instCI.setPNext(&validationFeatures);
#endif

      _instance = vk::createInstance(instCI);
      _dispatcher.init(_instance);

      fmt::print(fg(fmt::color::green), "\t[Vulkan] Instance created successfully\n");

      /// @note Setup debug messenger
#if ZS_ENABLE_VULKAN_VALIDATION
      _messenger = _instance.createDebugUtilsMessengerEXT(
          vk::DebugUtilsMessengerCreateInfoEXT{
              {},
              vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
                  | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning,
              vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral
                  | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
                  | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
              zsvk_debug_callback},
          nullptr, _dispatcher);
#endif

      /// @note Initialize physical devices and contexts
      auto physicalDevices = _instance.enumeratePhysicalDevices(_dispatcher);
      
      if (physicalDevices.empty()) {
        throw std::runtime_error("No Vulkan-capable physical devices found!");
      }

      fmt::print(fg(fmt::color::cyan), 
                 "\t[Vulkan] Detected {} physical device(s)\n", physicalDevices.size());

      _defaultContext = -1;
      for (size_t i = 0; i < physicalDevices.size(); ++i) {
        auto &physDev = physicalDevices[i];
        auto props = physDev.getProperties();
        
        fmt::print("\t  [Device {}] {}\n", i, props.deviceName.data());

        try {
          _contexts.emplace_back(static_cast<int>(i), _instance, physDev, _dispatcher);
          
          if (_defaultContext == -1 && _contexts.back().supportGraphics()) {
            _defaultContext = static_cast<int>(i);
            fmt::print(fg(fmt::color::lime_green), "\t    -> Selected as default\n");
          }
        } catch (const std::exception &e) {
          fmt::print(fg(fmt::color::orange_red),
                     "\t    -> Failed to create context: {}\n", e.what());
        }
      }

      if (_defaultContext == -1 && !_contexts.empty()) {
        _defaultContext = 0;
      }

      if (_contexts.empty()) {
        throw std::runtime_error("Failed to create any Vulkan device context!");
      }

      _workingContexts = new WorkerEnvs();
      _mutex = new Mutex();

      fmt::print(fg(fmt::color::green), 
                 "[Vulkan] Initialization complete\n");

    } catch (const std::exception &e) {
      fmt::print(fg(fmt::color::red), "[Vulkan Error] {}\n", e.what());
      reset();
      throw;
    }
  }  // namespace zs
  void Vulkan::reset() {
    fmt::print("[Vulkan] Shutting down...\n");

    // Clean up working contexts
    if (_workingContexts) {
      delete static_cast<WorkerEnvs *>(_workingContexts);
      _workingContexts = nullptr;
    }
    if (_mutex) {
      delete static_cast<Mutex *>(_mutex);
      _mutex = nullptr;
    }

    // Clear device contexts
    for (auto &ctx : _contexts) {
      try {
        ctx.reset();
      } catch (const std::exception &e) {
        fmt::print(fg(fmt::color::orange_red), 
                   "\t[Warning] Error during context cleanup: {}\n", e.what());
      }
    }
    _contexts.clear();

    // Clean up instance-level resources
    if (_instance) {
      // User-defined cleanup (e.g., surfaces)
      if (_onDestroyCallback) {
        try {
          _onDestroyCallback();
        } catch (const std::exception &e) {
          fmt::print(fg(fmt::color::orange_red),
                     "\t[Warning] Error in user cleanup callback: {}\n", e.what());
        }
      }

      // Destroy debug messenger
      if (_messenger) {
        _instance.destroy(_messenger, nullptr, _dispatcher);
        _messenger = VK_NULL_HANDLE;
      }

      // Destroy instance
      _instance.destroy(nullptr, _dispatcher);
      _instance = vk::Instance{};

      fmt::print(fg(fmt::color::green), "[Vulkan] Shutdown complete\n");
    }
  }
  Vulkan::~Vulkan() {
    reset();
  }

}  // namespace zs
