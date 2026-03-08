#include <cassert>
#include <memory>

#include "zensim/execution/AsyncNativeQueueAdapter.hpp"

namespace {

  struct FakeNativeQueueState {
    void *queueHandle{reinterpret_cast<void *>(static_cast<uintptr_t>(0xAAAA))};
    void *signalHandle{reinterpret_cast<void *>(static_cast<uintptr_t>(0xBBBB))};
    int syncCount{0};
    int recordCount{0};
    int waitCount{0};
    void *lastForeignSignal{nullptr};
  };

  void *fake_queue_handle(void *binding) noexcept {
    return static_cast<FakeNativeQueueState *>(binding)->queueHandle;
  }

  void *fake_signal_handle(void *binding) noexcept {
    return static_cast<FakeNativeQueueState *>(binding)->signalHandle;
  }

  bool fake_sync(void *binding) noexcept {
    ++static_cast<FakeNativeQueueState *>(binding)->syncCount;
    return true;
  }

  bool fake_record(void *binding) noexcept {
    ++static_cast<FakeNativeQueueState *>(binding)->recordCount;
    return true;
  }

  bool fake_wait(void *binding, void *foreignSignal) noexcept {
    auto *state = static_cast<FakeNativeQueueState *>(binding);
    ++state->waitCount;
    state->lastForeignSignal = foreignSignal;
    return true;
  }

}  // namespace

int main() {
  using namespace zs;

  auto storage = std::make_shared<FakeNativeQueueState>();
  AsyncNativeQueueBinding binding{};
  binding.storage = storage;
  binding.opaque = storage.get();
  binding.ops.queueHandle = &fake_queue_handle;
  binding.ops.signalHandle = &fake_signal_handle;
  binding.ops.sync = &fake_sync;
  binding.ops.record = &fake_record;
  binding.ops.wait = &fake_wait;

  AsyncNativeQueueDescriptor descriptor{};
  descriptor.backendCode = async_backend_code(AsyncBackend::cuda);
  descriptor.queueCode = async_queue_code(AsyncQueueClass::compute);
  descriptor.device = 4;
  descriptor.streamOrQueueId = 7;
  descriptor.capabilityMask = async_native_capability_submit | async_native_capability_sync
                            | async_native_capability_record | async_native_capability_wait;
  descriptor.syncAfterSubmit = true;
  descriptor.recordAfterSubmit = true;

  const auto endpoint = make_native_queue_endpoint(descriptor, binding, "native-cuda");
  assert(endpoint.backend == AsyncBackend::cuda);
  assert(endpoint.queue == AsyncQueueClass::compute);
  assert(endpoint.device == 4);
  assert(endpoint.stream == 7);
  assert(endpoint.nativeHandle == storage->queueHandle);
  assert(endpoint.label == "native-cuda");

  assert(descriptor.supports(async_native_capability_sync));
  assert(!descriptor.supports(async_native_capability_profile));
  assert(descriptor.backend_name() == std::string_view{"cuda"});

  AsyncRuntime runtime{1};
  runtime.register_executor("native_queue",
                            make_native_queue_executor("native_queue", descriptor, binding));

  int ran = 0;
  auto handle = runtime.submit(AsyncSubmission{
      "native_queue",
      AsyncTaskDesc{"native-step", AsyncDomain::compute, AsyncQueueClass::compute},
      make_host_endpoint(),
      [&](AsyncExecutionContext &ctx) {
        ++ran;
        assert(ctx.endpoint != nullptr);
        assert(ctx.endpoint->backend == AsyncBackend::cuda);
        assert(ctx.endpoint->nativeHandle == storage->queueHandle);
        assert(ctx.endpoint->device == 4);
        assert(ctx.endpoint->stream == 7);
        return AsyncPollStatus::completed;
      }});

  handle.event().wait();
  assert(handle.status() == AsyncTaskStatus::completed);
  assert(ran == 1);
  assert(storage->recordCount == 1);
  assert(storage->syncCount == 1);

  assert(binding.wait(reinterpret_cast<void *>(static_cast<uintptr_t>(0xCCCC))));
  assert(storage->waitCount == 1);
  assert(storage->lastForeignSignal == reinterpret_cast<void *>(static_cast<uintptr_t>(0xCCCC)));

  AsyncNativeQueueDescriptor vulkanDescriptor{};
  vulkanDescriptor.backendCode = async_backend_code(AsyncBackend::vulkan);
  vulkanDescriptor.queueCode = async_queue_code(AsyncQueueClass::graphics);
  vulkanDescriptor.device = 1;
  vulkanDescriptor.streamOrQueueId = 2;
  vulkanDescriptor.capabilityMask = async_native_capability_submit | async_native_capability_sync;
  vulkanDescriptor.syncAfterSubmit = true;
  vulkanDescriptor.interactive = true;

  const auto comparison = compare_backend_profiles(make_vulkan_backend_profile(),
                                                   make_cuda_backend_profile());
  assert(comparison.crossPlatformDelta > 0);
  assert(comparison.performanceDelta < 0);
  assert(vulkanDescriptor.backend_name() == std::string_view{"vulkan"});

  return 0;
}