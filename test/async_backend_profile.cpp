#include <cassert>

#include "zensim/execution/AsyncBackendProfile.hpp"

int main() {
  using namespace zs;

  const auto vulkan = make_vulkan_backend_profile();
  const auto cuda = make_cuda_backend_profile();

  assert(vulkan.backendCode == async_backend_code(AsyncBackend::vulkan));
  assert(cuda.backendCode == async_backend_code(AsyncBackend::cuda));
  assert(vulkan.core_utility_coverage());
  assert(cuda.core_utility_coverage());
  assert(vulkan.supports_utility(async_utility_present));
  assert(!cuda.supports_utility(async_utility_present));
  assert(cuda.supports_utility(async_utility_collective));
  assert(!vulkan.supports_utility(async_utility_collective));
  assert(vulkan.supports_queue(async_queue_code(AsyncQueueClass::graphics)));
  assert(!cuda.supports_queue(async_queue_code(AsyncQueueClass::graphics)));
  assert(cuda.supports_queue(async_queue_code(AsyncQueueClass::collective)));
  assert(!vulkan.supports_queue(async_queue_code(AsyncQueueClass::collective)));

  const auto comparison = compare_backend_profiles(vulkan, cuda);
  assert(comparison.crossPlatformDelta > 0);
  assert(comparison.performanceDelta < 0);
  assert(comparison.interactiveDelta > 0);
  assert(comparison.sameCoreUtilityCoverage);
  assert(comparison.sharedUtilityMask == async_utility_core_mask);

  AsyncBackendSelectionRequest graphicsRequest{};
  graphicsRequest.domainCode = async_domain_code(AsyncDomain::graphics);
  graphicsRequest.queueCode = async_queue_code(AsyncQueueClass::graphics);
  graphicsRequest.preferenceCode = async_preference_cross_platform;
  graphicsRequest.requirePresent = true;
  graphicsRequest.requireMobileDeployment = true;
  graphicsRequest.requireHostInteractivity = true;

  const auto graphicsSelection = select_backend_profile(graphicsRequest);
  assert(graphicsSelection.backendCode == async_backend_code(AsyncBackend::vulkan));
  assert(graphicsSelection.vulkanScore > graphicsSelection.cudaScore);
  assert(graphicsSelection.sameCoreUtilityCoverage);

  AsyncBackendSelectionRequest computeRequest{};
  computeRequest.domainCode = async_domain_code(AsyncDomain::compute);
  computeRequest.queueCode = async_queue_code(AsyncQueueClass::compute);
  computeRequest.preferenceCode = async_preference_peak_performance;
  computeRequest.requireCollective = true;

  const auto computeSelection = select_backend_profile(computeRequest);
  assert(computeSelection.backendCode == async_backend_code(AsyncBackend::cuda));
  assert(computeSelection.cudaScore > computeSelection.vulkanScore);

  void *vkQueue = reinterpret_cast<void *>(static_cast<uintptr_t>(0x1111));
  const auto vkEndpoint = make_profile_endpoint(vulkan, 1, 3, vkQueue, AsyncQueueClass::graphics,
                                                "vk-interactive");
  assert(vkEndpoint.backend == AsyncBackend::vulkan);
  assert(vkEndpoint.queue == AsyncQueueClass::graphics);
  assert(vkEndpoint.device == 1);
  assert(vkEndpoint.stream == 3);
  assert(vkEndpoint.nativeHandle == vkQueue);
  assert(vkEndpoint.label == "vk-interactive");

  void *cudaStream = reinterpret_cast<void *>(static_cast<uintptr_t>(0x2222));
  const auto cudaEndpoint = make_profile_endpoint(cuda, 2, 5, cudaStream,
                                                  AsyncQueueClass::compute, "cuda-throughput");
  assert(cudaEndpoint.backend == AsyncBackend::cuda);
  assert(cudaEndpoint.queue == AsyncQueueClass::compute);
  assert(cudaEndpoint.device == 2);
  assert(cudaEndpoint.stream == 5);
  assert(cudaEndpoint.nativeHandle == cudaStream);
  assert(cudaEndpoint.label == "cuda-throughput");

  return 0;
}