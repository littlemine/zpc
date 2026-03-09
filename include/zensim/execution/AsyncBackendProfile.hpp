#pragma once

#include "zensim/execution/AsyncRuntime.hpp"
#include "zensim/reflect/ZpcReflectAnnotations.hpp"

namespace zs {

  inline constexpr u8 async_backend_code(AsyncBackend backend) noexcept {
    return static_cast<u8>(backend);
  }

  inline constexpr AsyncBackend async_backend_from_code(u8 code) noexcept {
    return static_cast<AsyncBackend>(code);
  }

  inline constexpr u8 async_domain_code(AsyncDomain domain) noexcept {
    return static_cast<u8>(domain);
  }

  inline constexpr AsyncDomain async_domain_from_code(u8 code) noexcept {
    return static_cast<AsyncDomain>(code);
  }

  inline constexpr u8 async_queue_code(AsyncQueueClass queue) noexcept {
    return static_cast<u8>(queue);
  }

  inline constexpr AsyncQueueClass async_queue_from_code(u8 code) noexcept {
    return static_cast<AsyncQueueClass>(code);
  }

  inline constexpr u8 async_preference_balanced = 0;
  inline constexpr u8 async_preference_cross_platform = 1;
  inline constexpr u8 async_preference_peak_performance = 2;
  inline constexpr u8 async_preference_interactive = 3;

  inline constexpr u32 async_utility_dispatch = 1u << 0;
  inline constexpr u32 async_utility_async_copy = 1u << 1;
  inline constexpr u32 async_utility_barrier_sync = 1u << 2;
  inline constexpr u32 async_utility_event_sync = 1u << 3;
  inline constexpr u32 async_utility_staging = 1u << 4;
  inline constexpr u32 async_utility_profiling = 1u << 5;
  inline constexpr u32 async_utility_present = 1u << 6;
  inline constexpr u32 async_utility_collective = 1u << 7;
  inline constexpr u32 async_utility_core_mask = async_utility_dispatch | async_utility_async_copy
                                               | async_utility_barrier_sync
                                               | async_utility_event_sync
                                               | async_utility_staging
                                               | async_utility_profiling;

  inline const char *async_backend_name_from_code(u8 code) noexcept {
    switch (async_backend_from_code(code)) {
      case AsyncBackend::vulkan:
        return "vulkan";
      case AsyncBackend::cuda:
        return "cuda";
      case AsyncBackend::thread_pool:
        return "thread_pool";
      case AsyncBackend::inline_host:
        return "inline_host";
      case AsyncBackend::asio:
        return "asio";
      case AsyncBackend::process:
        return "process";
      case AsyncBackend::nccl:
        return "nccl";
      case AsyncBackend::rocm:
        return "rocm";
      case AsyncBackend::sycl:
        return "sycl";
      case AsyncBackend::custom:
        return "custom";
      default:
        return "unknown";
    }
  }

  inline const char *async_preference_name(u8 preferenceCode) noexcept {
    switch (preferenceCode) {
      case async_preference_cross_platform:
        return "cross_platform";
      case async_preference_peak_performance:
        return "peak_performance";
      case async_preference_interactive:
        return "interactive";
      case async_preference_balanced:
        return "balanced";
      default:
        return "unknown";
    }
  }

  struct ZS_REFLECT AsyncBackendProfile {
    ZS_PROPERTY(name = "backend_code") u8 backendCode{async_backend_code(AsyncBackend::inline_host)};
    ZS_PROPERTY(name = "utility_mask") u32 utilityMask{0};
    ZS_PROPERTY(name = "queue_capacity") u32 queueCapacity{1};
    ZS_PROPERTY(name = "transfer_capacity") u32 transferCapacity{0};
    ZS_PROPERTY(name = "cross_platform_score") u32 crossPlatformScore{0};
    ZS_PROPERTY(name = "performance_score") u32 performanceScore{0};
    ZS_PROPERTY(name = "interactive_score") u32 interactiveScore{0};
    ZS_PROPERTY(name = "mobile_ready") bool mobileReady{false};
    ZS_PROPERTY(name = "presentation_ready") bool presentationReady{false};
    ZS_PROPERTY(name = "collective_ready") bool collectiveReady{false};

    ZS_METHOD(category = "preset") void set_vulkan_defaults() noexcept {
      backendCode = async_backend_code(AsyncBackend::vulkan);
      utilityMask = async_utility_core_mask | async_utility_present;
      queueCapacity = 3;
      transferCapacity = 1;
      crossPlatformScore = 10;
      performanceScore = 7;
      interactiveScore = 9;
      mobileReady = true;
      presentationReady = true;
      collectiveReady = false;
    }

    ZS_METHOD(category = "preset") void set_cuda_defaults() noexcept {
      backendCode = async_backend_code(AsyncBackend::cuda);
      utilityMask = async_utility_core_mask | async_utility_collective;
      queueCapacity = 2;
      transferCapacity = 2;
      crossPlatformScore = 4;
      performanceScore = 10;
      interactiveScore = 6;
      mobileReady = false;
      presentationReady = false;
      collectiveReady = true;
    }

    ZS_METHOD(category = "query") bool supports_utility(u32 utilityBit) const noexcept {
      return (utilityMask & utilityBit) == utilityBit;
    }

    ZS_METHOD(category = "query") bool supports_queue(u8 queueCode) const noexcept {
      switch (async_queue_from_code(queueCode)) {
        case AsyncQueueClass::compute:
        case AsyncQueueClass::transfer:
        case AsyncQueueClass::copy:
          return true;
        case AsyncQueueClass::graphics:
        case AsyncQueueClass::present:
          return presentationReady;
        case AsyncQueueClass::collective:
          return collectiveReady;
        default:
          return queueCapacity != 0;
      }
    }

    ZS_METHOD(category = "query") bool core_utility_coverage() const noexcept {
      return (utilityMask & async_utility_core_mask) == async_utility_core_mask;
    }

    ZS_METHOD(category = "query") const char *backend_name() const noexcept {
      return async_backend_name_from_code(backendCode);
    }
  };

  struct ZS_REFLECT AsyncBackendSelectionRequest {
    ZS_PROPERTY(name = "domain_code") u8 domainCode{async_domain_code(AsyncDomain::compute)};
    ZS_PROPERTY(name = "queue_code") u8 queueCode{async_queue_code(AsyncQueueClass::compute)};
    ZS_PROPERTY(name = "preference_code") u8 preferenceCode{async_preference_balanced};
    ZS_PROPERTY(name = "require_present") bool requirePresent{false};
    ZS_PROPERTY(name = "require_collective") bool requireCollective{false};
    ZS_PROPERTY(name = "require_mobile_deployment") bool requireMobileDeployment{false};
    ZS_PROPERTY(name = "require_host_interactivity") bool requireHostInteractivity{false};

    ZS_METHOD(category = "query") const char *preference_name() const noexcept {
      return async_preference_name(preferenceCode);
    }
  };

  struct ZS_REFLECT AsyncBackendSelection {
    ZS_PROPERTY(name = "backend_code") u8 backendCode{async_backend_code(AsyncBackend::inline_host)};
    ZS_PROPERTY(name = "vulkan_score") i32 vulkanScore{0};
    ZS_PROPERTY(name = "cuda_score") i32 cudaScore{0};
    ZS_PROPERTY(name = "shared_utility_mask") u32 sharedUtilityMask{0};
    ZS_PROPERTY(name = "same_core_utility_coverage") bool sameCoreUtilityCoverage{false};

    ZS_METHOD(category = "query") const char *backend_name() const noexcept {
      return async_backend_name_from_code(backendCode);
    }

    ZS_METHOD(category = "summary") const char *summary() const noexcept {
      if (backendCode == async_backend_code(AsyncBackend::vulkan))
        return "vulkan selected: broader deployment reach, graphics or present support, and better interactive fit";
      if (backendCode == async_backend_code(AsyncBackend::cuda))
        return "cuda selected: stronger compute throughput and collective-oriented performance path";
      return "no backend selected";
    }
  };

  struct ZS_REFLECT AsyncBackendComparison {
    ZS_PROPERTY(name = "left_backend_code") u8 leftBackendCode{async_backend_code(AsyncBackend::inline_host)};
    ZS_PROPERTY(name = "right_backend_code") u8 rightBackendCode{async_backend_code(AsyncBackend::inline_host)};
    ZS_PROPERTY(name = "cross_platform_delta") i32 crossPlatformDelta{0};
    ZS_PROPERTY(name = "performance_delta") i32 performanceDelta{0};
    ZS_PROPERTY(name = "interactive_delta") i32 interactiveDelta{0};
    ZS_PROPERTY(name = "shared_utility_mask") u32 sharedUtilityMask{0};
    ZS_PROPERTY(name = "same_core_utility_coverage") bool sameCoreUtilityCoverage{false};

    ZS_METHOD(category = "summary") const char *summary() const noexcept {
      if (crossPlatformDelta > 0 && performanceDelta < 0)
        return "left backend leads in deployment reach while right backend leads in peak throughput";
      if (crossPlatformDelta < 0 && performanceDelta > 0)
        return "right backend leads in deployment reach while left backend leads in peak throughput";
      if (interactiveDelta > 0)
        return "left backend is more interactive-friendly";
      if (interactiveDelta < 0)
        return "right backend is more interactive-friendly";
      return "backends are closely matched on the compared dimensions";
    }
  };

  inline AsyncBackendProfile make_vulkan_backend_profile() noexcept {
    AsyncBackendProfile profile{};
    profile.set_vulkan_defaults();
    return profile;
  }

  inline AsyncBackendProfile make_cuda_backend_profile() noexcept {
    AsyncBackendProfile profile{};
    profile.set_cuda_defaults();
    return profile;
  }

  inline AsyncBackendComparison compare_backend_profiles(const AsyncBackendProfile &left,
                                                         const AsyncBackendProfile &right) noexcept {
    AsyncBackendComparison comparison{};
    comparison.leftBackendCode = left.backendCode;
    comparison.rightBackendCode = right.backendCode;
    comparison.crossPlatformDelta = static_cast<i32>(left.crossPlatformScore)
                                  - static_cast<i32>(right.crossPlatformScore);
    comparison.performanceDelta = static_cast<i32>(left.performanceScore)
                                - static_cast<i32>(right.performanceScore);
    comparison.interactiveDelta = static_cast<i32>(left.interactiveScore)
                                - static_cast<i32>(right.interactiveScore);
    comparison.sharedUtilityMask = left.utilityMask & right.utilityMask;
    comparison.sameCoreUtilityCoverage = (comparison.sharedUtilityMask & async_utility_core_mask)
                                      == async_utility_core_mask;
    return comparison;
  }

  inline i32 score_backend_for_request(const AsyncBackendProfile &profile,
                                       const AsyncBackendSelectionRequest &request) noexcept {
    i32 score = 0;

    if (profile.supports_queue(request.queueCode))
      score += 4;
    else
      score -= 10;

    if (request.requirePresent && !profile.presentationReady) score -= 20;
    if (request.requireCollective && !profile.collectiveReady) score -= 20;
    if (request.requireMobileDeployment && !profile.mobileReady) score -= 20;
    if (request.requireHostInteractivity) score += static_cast<i32>(profile.interactiveScore);

    switch (request.preferenceCode) {
      case async_preference_cross_platform:
        score += static_cast<i32>(profile.crossPlatformScore) * 2;
        break;
      case async_preference_peak_performance:
        score += static_cast<i32>(profile.performanceScore) * 2;
        break;
      case async_preference_interactive:
        score += static_cast<i32>(profile.interactiveScore) * 2;
        break;
      default:
        score += static_cast<i32>(profile.crossPlatformScore);
        score += static_cast<i32>(profile.performanceScore);
        score += static_cast<i32>(profile.interactiveScore);
        break;
    }

    if (async_domain_from_code(request.domainCode) == AsyncDomain::graphics && profile.presentationReady)
      score += 5;
    if (async_domain_from_code(request.domainCode) == AsyncDomain::compute && profile.collectiveReady)
      score += 2;
    return score;
  }

  inline AsyncBackendSelection select_backend_profile(
      const AsyncBackendSelectionRequest &request) noexcept {
    const auto vulkan = make_vulkan_backend_profile();
    const auto cuda = make_cuda_backend_profile();

    AsyncBackendSelection selection{};
    selection.vulkanScore = score_backend_for_request(vulkan, request);
    selection.cudaScore = score_backend_for_request(cuda, request);
    selection.sharedUtilityMask = vulkan.utilityMask & cuda.utilityMask;
    selection.sameCoreUtilityCoverage = (selection.sharedUtilityMask & async_utility_core_mask)
                                     == async_utility_core_mask;
    selection.backendCode = selection.vulkanScore >= selection.cudaScore ? vulkan.backendCode
                                                                         : cuda.backendCode;
    return selection;
  }

  inline AsyncEndpoint make_profile_endpoint(const AsyncBackendProfile &profile, i32 device,
                                             StreamID streamOrQueue, void *nativeHandle,
                                             AsyncQueueClass queue = AsyncQueueClass::compute,
                                             SmallString label = {}) {
    switch (async_backend_from_code(profile.backendCode)) {
      case AsyncBackend::vulkan:
        return make_vulkan_endpoint(device, streamOrQueue, nativeHandle, queue, label);
      case AsyncBackend::cuda:
        return make_cuda_endpoint(device, streamOrQueue, nativeHandle, queue, label);
      default:
        return make_host_endpoint(AsyncBackend::inline_host, queue, label);
    }
  }

}  // namespace zs