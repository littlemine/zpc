#pragma once

#include <memory>
#include <string>

#include "zensim/execution/AsyncBackendProfile.hpp"
#include "zensim/execution/AsyncRuntime.hpp"
#include "zensim/reflect/ZpcReflectAnnotations.hpp"

#if defined(ZS_ENABLE_CUDA) && ZS_ENABLE_CUDA
#  include "zensim/cuda/Cuda.h"
#endif

#if defined(ZS_ENABLE_VULKAN) && ZS_ENABLE_VULKAN
#  include "zensim/vulkan/VkContext.hpp"
#endif

namespace zs {

  inline constexpr u32 async_native_capability_submit = 1u << 0;
  inline constexpr u32 async_native_capability_sync = 1u << 1;
  inline constexpr u32 async_native_capability_record = 1u << 2;
  inline constexpr u32 async_native_capability_wait = 1u << 3;
  inline constexpr u32 async_native_capability_profile = 1u << 4;

  struct ZS_REFLECT AsyncNativeQueueDescriptor {
    ZS_PROPERTY(name = "backend_code") u8 backendCode{async_backend_code(AsyncBackend::inline_host)};
    ZS_PROPERTY(name = "queue_code") u8 queueCode{async_queue_code(AsyncQueueClass::compute)};
    ZS_PROPERTY(name = "device") i32 device{-1};
    ZS_PROPERTY(name = "stream_or_queue_id") StreamID streamOrQueueId{-1};
    ZS_PROPERTY(name = "capability_mask") u32 capabilityMask{0};
    ZS_PROPERTY(name = "sync_after_submit") bool syncAfterSubmit{false};
    ZS_PROPERTY(name = "record_after_submit") bool recordAfterSubmit{false};
    ZS_PROPERTY(name = "interactive") bool interactive{false};

    ZS_METHOD(category = "query") bool supports(u32 capabilityBit) const noexcept {
      return (capabilityMask & capabilityBit) == capabilityBit;
    }

    ZS_METHOD(category = "query") const char *backend_name() const noexcept {
      return async_backend_name_from_code(backendCode);
    }
  };

  struct AsyncNativeQueueOps {
    using queue_handle_fn = void *(*)(void *binding) noexcept;
    using signal_handle_fn = void *(*)(void *binding) noexcept;
    using sync_fn = bool (*)(void *binding) noexcept;
    using record_fn = bool (*)(void *binding) noexcept;
    using wait_fn = bool (*)(void *binding, void *foreignSignal) noexcept;

    queue_handle_fn queueHandle{};
    signal_handle_fn signalHandle{};
    sync_fn sync{};
    record_fn record{};
    wait_fn wait{};
  };

  struct AsyncNativeQueueBinding {
    std::shared_ptr<void> storage{};
    void *opaque{nullptr};
    AsyncNativeQueueOps ops{};

    void *queue_handle() const noexcept {
      return ops.queueHandle && opaque ? ops.queueHandle(opaque) : nullptr;
    }

    void *signal_handle() const noexcept {
      return ops.signalHandle && opaque ? ops.signalHandle(opaque) : nullptr;
    }

    bool sync() const noexcept { return ops.sync && opaque ? ops.sync(opaque) : true; }
    bool record() const noexcept { return ops.record && opaque ? ops.record(opaque) : true; }
    bool wait(void *foreignSignal) const noexcept {
      return ops.wait && opaque ? ops.wait(opaque, foreignSignal) : true;
    }
  };

  inline AsyncEndpoint make_native_queue_endpoint(const AsyncNativeQueueDescriptor &descriptor,
                                                  const AsyncNativeQueueBinding &binding,
                                                  SmallString label = {}) {
    const auto backend = async_backend_from_code(descriptor.backendCode);
    const auto queue = async_queue_from_code(descriptor.queueCode);
    switch (backend) {
      case AsyncBackend::cuda:
        return make_cuda_endpoint(descriptor.device, descriptor.streamOrQueueId,
                                  binding.queue_handle(), queue, label);
      case AsyncBackend::vulkan:
        return make_vulkan_endpoint(descriptor.device, descriptor.streamOrQueueId,
                                    binding.queue_handle(), queue, label);
      default:
        return make_host_endpoint(backend, queue, label);
    }
  }

  class AsyncNativeQueueExecutor : public AsyncExecutor {
  public:
    AsyncNativeQueueExecutor(std::string executorName, AsyncNativeQueueDescriptor descriptor,
                             AsyncNativeQueueBinding binding)
        : _name{std::move(executorName)}, _descriptor{descriptor}, _binding{std::move(binding)} {}

    AsyncBackend backend() const noexcept override {
      return async_backend_from_code(_descriptor.backendCode);
    }

    std::string_view name() const noexcept override { return _name; }

    const AsyncNativeQueueDescriptor &descriptor() const noexcept { return _descriptor; }
    const AsyncNativeQueueBinding &binding() const noexcept { return _binding; }

    AsyncEvent submit(std::shared_ptr<AsyncSubmissionState> state) override {
      if (!state) return AsyncEvent{};
      state->endpoint = make_native_queue_endpoint(_descriptor, _binding, state->desc.label);
      if (state->cancellation.stop_requested() || state->cancellation.interrupt_requested()) {
        state->event.mark_cancelled();
        state->inFlight.store(false, std::memory_order_release);
        return state->event;
      }

      state->event.mark_running();
      const auto poll = AsyncRuntime::run_step(state);

      bool backendOk = true;
      if (poll == AsyncPollStatus::completed) {
        if (_descriptor.recordAfterSubmit && _descriptor.supports(async_native_capability_record))
          backendOk = _binding.record();
        if (backendOk && _descriptor.syncAfterSubmit
            && _descriptor.supports(async_native_capability_sync))
          backendOk = _binding.sync();
      }

      if (!backendOk)
        state->event.mark_failed();
      else if (poll == AsyncPollStatus::suspend)
        state->event.mark_suspended();
      else if (poll == AsyncPollStatus::completed)
        state->event.mark_completed();
      else if (poll == AsyncPollStatus::cancelled)
        state->event.mark_cancelled();
      else
        state->event.mark_failed();

      state->inFlight.store(false, std::memory_order_release);
      return state->event;
    }

  private:
    std::string _name;
    AsyncNativeQueueDescriptor _descriptor{};
    AsyncNativeQueueBinding _binding{};
  };

  inline std::shared_ptr<AsyncNativeQueueExecutor> make_native_queue_executor(
      std::string executorName, AsyncNativeQueueDescriptor descriptor,
      AsyncNativeQueueBinding binding) {
    return std::make_shared<AsyncNativeQueueExecutor>(std::move(executorName), descriptor,
                                                      std::move(binding));
  }

#if defined(ZS_ENABLE_CUDA) && ZS_ENABLE_CUDA

  struct AsyncCudaNativeQueueState {
    Cuda::CudaContext *context{nullptr};
    StreamID streamId{0};
  };

  inline void *async_cuda_queue_handle(void *binding) noexcept {
    auto *state = static_cast<AsyncCudaNativeQueueState *>(binding);
    return state && state->context ? state->context->streamSpare(state->streamId) : nullptr;
  }

  inline void *async_cuda_signal_handle(void *binding) noexcept {
    auto *state = static_cast<AsyncCudaNativeQueueState *>(binding);
    return state && state->context ? state->context->eventSpare(state->streamId) : nullptr;
  }

  inline bool async_cuda_sync_queue(void *binding) noexcept {
    auto *state = static_cast<AsyncCudaNativeQueueState *>(binding);
    if (!state || !state->context) return false;
    state->context->syncStreamSpare(state->streamId);
    return true;
  }

  inline bool async_cuda_record_queue(void *binding) noexcept {
    auto *state = static_cast<AsyncCudaNativeQueueState *>(binding);
    if (!state || !state->context) return false;
    state->context->recordEventSpare(state->streamId);
    return true;
  }

  inline bool async_cuda_wait_queue(void *binding, void *foreignSignal) noexcept {
    auto *state = static_cast<AsyncCudaNativeQueueState *>(binding);
    if (!state || !state->context) return false;
    state->context->spareStreamWaitForEvent(state->streamId, foreignSignal);
    return true;
  }

  inline AsyncNativeQueueBinding bind_cuda_native_queue(Cuda::CudaContext &context,
                                                        StreamID streamId = 0) {
    auto storage = std::make_shared<AsyncCudaNativeQueueState>();
    storage->context = &context;
    storage->streamId = streamId;
    AsyncNativeQueueBinding binding{};
    binding.opaque = storage.get();
    binding.storage = storage;
    binding.ops.queueHandle = &async_cuda_queue_handle;
    binding.ops.signalHandle = &async_cuda_signal_handle;
    binding.ops.sync = &async_cuda_sync_queue;
    binding.ops.record = &async_cuda_record_queue;
    binding.ops.wait = &async_cuda_wait_queue;
    return binding;
  }

  inline AsyncNativeQueueDescriptor make_cuda_native_queue_descriptor(i32 device,
                                                                      StreamID streamId = 0,
                                                                      bool interactive = false) noexcept {
    return AsyncNativeQueueDescriptor{async_backend_code(AsyncBackend::cuda),
                                      async_queue_code(AsyncQueueClass::compute), device,
                                      streamId,
                                      async_native_capability_submit
                                          | async_native_capability_sync
                                          | async_native_capability_record
                                          | async_native_capability_wait
                                          | async_native_capability_profile,
                                      false, true, interactive};
  }

#endif

#if defined(ZS_ENABLE_VULKAN) && ZS_ENABLE_VULKAN

  struct AsyncVulkanNativeQueueState {
    VulkanContext *context{nullptr};
    vk_queue_e queueFamily{vk_queue_e::graphics};
    u32 queueIndex{0};
  };

  inline void *async_vulkan_queue_handle(void *binding) noexcept {
    auto *state = static_cast<AsyncVulkanNativeQueueState *>(binding);
    if (!state || !state->context) return nullptr;
    return reinterpret_cast<void *>(static_cast<VkQueue>(state->context->getQueue(state->queueFamily,
                                                                                   state->queueIndex)));
  }

  inline bool async_vulkan_sync_queue(void *binding) noexcept {
    auto *state = static_cast<AsyncVulkanNativeQueueState *>(binding);
    if (!state || !state->context) return false;
    state->context->getQueue(state->queueFamily, state->queueIndex).waitIdle(state->context->dispatcher);
    return true;
  }

  inline AsyncNativeQueueBinding bind_vulkan_native_queue(VulkanContext &context,
                                                          vk_queue_e queueFamily = vk_queue_e::graphics,
                                                          u32 queueIndex = 0) {
    auto storage = std::make_shared<AsyncVulkanNativeQueueState>();
    storage->context = &context;
    storage->queueFamily = queueFamily;
    storage->queueIndex = queueIndex;
    AsyncNativeQueueBinding binding{};
    binding.opaque = storage.get();
    binding.storage = storage;
    binding.ops.queueHandle = &async_vulkan_queue_handle;
    binding.ops.sync = &async_vulkan_sync_queue;
    return binding;
  }

  inline AsyncNativeQueueDescriptor make_vulkan_native_queue_descriptor(
      i32 device, StreamID queueFamilyIndex,
      bool interactive = true) noexcept {
    return AsyncNativeQueueDescriptor{async_backend_code(AsyncBackend::vulkan),
                                      async_queue_code(AsyncQueueClass::graphics), device,
                                      queueFamilyIndex,
                                      async_native_capability_submit
                                          | async_native_capability_sync,
                                      true, false, interactive};
  }

#endif

}  // namespace zs