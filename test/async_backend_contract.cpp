#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>

#include "zensim/execution/AsyncRuntime.hpp"

namespace {

  struct RecordingExecutor : zs::AsyncExecutor {
    explicit RecordingExecutor(std::string executorName, zs::AsyncBackend backendId)
        : backendValue{backendId}, executorName{zs::move(executorName)}, delegate{"record-delegate"} {}

    zs::AsyncBackend backend() const noexcept override { return backendValue; }
    std::string_view name() const noexcept override { return executorName; }

    zs::AsyncEvent submit(std::shared_ptr<zs::AsyncSubmissionState> state) override {
      ++submitCount;
      observedBackend = state->endpoint.backend;
      observedQueue = state->endpoint.queue;
      observedNode = state->endpoint.node;
      observedProcess = state->endpoint.process;
      observedDevice = state->endpoint.device;
      observedStream = state->endpoint.stream;
      observedHandle = state->endpoint.nativeHandle;
      observedLabel = state->endpoint.label;
      observedSubmissionId = state->id;

      return delegate.submit(zs::move(state));
    }

    zs::AsyncBackend backendValue;
    std::string executorName;
    std::atomic<int> submitCount{0};
    zs::AsyncBackend observedBackend{zs::AsyncBackend::inline_host};
    zs::AsyncQueueClass observedQueue{zs::AsyncQueueClass::control};
    zs::NodeID observedNode{0};
    zs::ProcID observedProcess{-1};
    int observedDevice{-1};
    zs::StreamID observedStream{-1};
    void *observedHandle{nullptr};
    zs::SmallString observedLabel{};
    zs::u64 observedSubmissionId{0};
    zs::AsyncInlineExecutor delegate;
  };

}  // namespace

int main() {
  using namespace zs;

  AsyncRuntime runtime{1};

  auto vkExecutor = std::make_shared<RecordingExecutor>("vk_record", AsyncBackend::vulkan);
  runtime.register_executor("vk_record", vkExecutor);
  assert(runtime.contains_executor("vk_record"));

  void *vkQueue = reinterpret_cast<void *>(static_cast<uintptr_t>(0x1234));
  auto vkHandle = runtime.submit(AsyncSubmission{
      "vk_record",
      AsyncTaskDesc{"vk-pass", AsyncDomain::graphics, AsyncQueueClass::graphics},
      make_vulkan_endpoint(2, 5, vkQueue, AsyncQueueClass::graphics, "vk-main"),
      [&](AsyncExecutionContext &ctx) {
        assert(ctx.endpoint != nullptr);
        assert(ctx.endpoint->backend == AsyncBackend::vulkan);
        assert(ctx.endpoint->queue == AsyncQueueClass::graphics);
        assert(ctx.endpoint->device == 2);
        assert(ctx.endpoint->stream == 5);
        assert(ctx.endpoint->nativeHandle == vkQueue);
        assert(ctx.endpoint->label == "vk-main");
        return AsyncPollStatus::completed;
      }});

  assert(vkHandle.event().wait_for(std::chrono::seconds(2)));
  assert(vkHandle.status() == AsyncTaskStatus::completed);
  assert(vkExecutor->submitCount.load() == 1);
  assert(vkExecutor->observedBackend == AsyncBackend::vulkan);
  assert(vkExecutor->observedQueue == AsyncQueueClass::graphics);
  assert(vkExecutor->observedDevice == 2);
  assert(vkExecutor->observedStream == 5);
  assert(vkExecutor->observedHandle == vkQueue);
  assert(vkExecutor->observedLabel == "vk-main");
  assert(vkExecutor->observedSubmissionId == vkHandle.id());

  auto cudaExecutor = std::make_shared<RecordingExecutor>("cuda_record", AsyncBackend::cuda);
  runtime.register_executor("cuda_record", cudaExecutor);

  void *cudaStream = reinterpret_cast<void *>(static_cast<uintptr_t>(0x5678));
  auto cudaHandle = runtime.submit(AsyncSubmission{
      "cuda_record",
      AsyncTaskDesc{"cuda-pass", AsyncDomain::compute, AsyncQueueClass::compute},
      make_cuda_endpoint(3, 7, cudaStream, AsyncQueueClass::transfer, "cuda-copy"),
      [&](AsyncExecutionContext &ctx) {
        assert(ctx.endpoint != nullptr);
        assert(ctx.endpoint->backend == AsyncBackend::cuda);
        assert(ctx.endpoint->queue == AsyncQueueClass::transfer);
        assert(ctx.endpoint->device == 3);
        assert(ctx.endpoint->stream == 7);
        assert(ctx.endpoint->nativeHandle == cudaStream);
        assert(ctx.endpoint->label == "cuda-copy");
        return AsyncPollStatus::completed;
      }});

  assert(cudaHandle.event().wait_for(std::chrono::seconds(2)));
  assert(cudaHandle.status() == AsyncTaskStatus::completed);
  assert(cudaExecutor->submitCount.load() == 1);
  assert(cudaExecutor->observedBackend == AsyncBackend::cuda);
  assert(cudaExecutor->observedQueue == AsyncQueueClass::transfer);
  assert(cudaExecutor->observedDevice == 3);
  assert(cudaExecutor->observedStream == 7);
  assert(cudaExecutor->observedHandle == cudaStream);
  assert(cudaExecutor->observedLabel == "cuda-copy");

  bool missingExecutorThrown = false;
  try {
    (void)runtime.submit(AsyncSubmission{
        "missing_executor",
        AsyncTaskDesc{"missing", AsyncDomain::control, AsyncQueueClass::control},
        make_host_endpoint(),
        [&](AsyncExecutionContext &) { return AsyncPollStatus::completed; }});
  } catch (const StaticException<> &) {
    missingExecutorThrown = true;
  }
  assert(missingExecutorThrown);

  return 0;
}