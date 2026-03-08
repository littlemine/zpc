#pragma once
#include "zensim/ZpcAsync.hpp"
#include "zensim/ZpcFunction.hpp"
#include "zensim/container/ConcurrentQueue.hpp"
#include "zensim/execution/AsyncEvent.hpp"
#include "zensim/execution/ManagedThread.hpp"
#include "zensim/types/SmallVector.hpp"

namespace zs {

  /// =========================================================================
  /// Enums — domain, backend, queue classification
  /// =========================================================================

  enum class AsyncDomain : u8 {
    control = 0,
    inter_node,
    process,
    thread,
    compute,
    graphics
  };

  enum class AsyncBackend : u8 {
    inline_host = 0,
    thread_pool,
    asio,
    process,
    cuda,
    nccl,
    rocm,
    sycl,
    vulkan,
    custom
  };

  enum class AsyncQueueClass : u8 {
    control = 0,
    io,
    compute,
    transfer,
    graphics,
    present,
    collective,
    copy
  };

  /// =========================================================================
  /// AsyncEndpoint — where work runs
  /// =========================================================================

  struct AsyncEndpoint {
    using SmallString = BasicSmallString<>;

    AsyncBackend backend{AsyncBackend::inline_host};
    AsyncQueueClass queue{AsyncQueueClass::control};
    i32 device{-1};        // -1 = host
    StreamID stream{-1};   // stream/queue index
    void* nativeHandle{nullptr};
    SmallString label{};
  };

  inline AsyncEndpoint make_host_endpoint(
      AsyncBackend backend = AsyncBackend::inline_host,
      AsyncQueueClass queue = AsyncQueueClass::control,
      const char* label = "") {
    return AsyncEndpoint{backend, queue, -1, -1, nullptr, label};
  }

  /// =========================================================================
  /// AsyncTaskDesc — metadata for a submitted task
  /// =========================================================================

  struct AsyncTaskDesc {
    using SmallString = BasicSmallString<>;

    SmallString label{};
    AsyncDomain domain{AsyncDomain::control};
    AsyncQueueClass queue{AsyncQueueClass::control};
    u8 priority{128};  // 0=highest, 255=lowest
    u32 grainSize{1};
    bool profile{false};
  };

  /// =========================================================================
  /// AsyncExecutionContext — passed to task step functions
  /// =========================================================================

  struct AsyncExecutionContext {
    u64 submissionId{0};
    const AsyncTaskDesc* desc{nullptr};
    const AsyncEndpoint* endpoint{nullptr};
    AsyncStopToken cancellation{};
  };

  /// Step function type
  using AsyncStep = function<AsyncPollStatus(AsyncExecutionContext&)>;

  /// =========================================================================
  /// AsyncSubmissionState — internal state held by the runtime per task
  /// =========================================================================

  struct AsyncSubmissionState {
    u64 id{0};
    AsyncTaskDesc desc{};
    AsyncEndpoint endpoint{};
    AsyncStep step{};
    AsyncStopToken cancellation{};
    AsyncEvent event{};
    AsyncTaskStatus status{AsyncTaskStatus::pending};
    bool stopOnPrerequisiteFailure{true};
    StaticVector<AsyncEvent, 8> prerequisites{};
  };

  /// =========================================================================
  /// AsyncSubmission — user-facing task description for submit()
  /// =========================================================================

  struct AsyncSubmission {
    using SmallString = BasicSmallString<>;

    SmallString executor{};
    AsyncTaskDesc desc{};
    AsyncEndpoint endpoint{};
    AsyncStep step{};
    StaticVector<AsyncEvent, 8> prerequisites{};
    AsyncStopToken cancellation{};
    bool stopOnPrerequisiteFailure{true};
  };

  /// =========================================================================
  /// AsyncExecutor — abstract base for executor implementations
  /// =========================================================================

  struct AsyncExecutor {
    virtual ~AsyncExecutor() = default;
    virtual AsyncBackend backend() const = 0;
    virtual const char* name() const = 0;
    virtual AsyncEvent submit(AsyncSubmissionState* state) = 0;
    virtual void shutdown() {}
  };

  /// =========================================================================
  /// AsyncInlineExecutor — runs task synchronously in the caller's thread
  /// =========================================================================

  struct AsyncInlineExecutor : AsyncExecutor {
    using SmallString = BasicSmallString<>;

    explicit AsyncInlineExecutor(const char* executorName = "inline")
        : _name{executorName} {}

    AsyncBackend backend() const override { return AsyncBackend::inline_host; }
    const char* name() const override { return _name.asChars(); }

    AsyncEvent submit(AsyncSubmissionState* state) override {
      auto evt = AsyncEvent::create();
      if (!state || !state->step) {
        evt.complete(AsyncTaskStatus::failed);
        return evt;
      }

      // Check prerequisites
      for (size_t i = 0; i < state->prerequisites.size(); ++i) {
        state->prerequisites[i].wait();
        if (state->stopOnPrerequisiteFailure
            && state->prerequisites[i].status() != AsyncTaskStatus::completed) {
          evt.complete(AsyncTaskStatus::cancelled);
          return evt;
        }
      }

      AsyncExecutionContext ctx{};
      ctx.submissionId = state->id;
      ctx.desc = &state->desc;
      ctx.endpoint = &state->endpoint;
      ctx.cancellation = state->cancellation;

      auto poll = state->step(ctx);
      switch (poll) {
        case AsyncPollStatus::completed:
          evt.complete(AsyncTaskStatus::completed);
          break;
        case AsyncPollStatus::cancelled:
          evt.complete(AsyncTaskStatus::cancelled);
          break;
        case AsyncPollStatus::failed:
          evt.complete(AsyncTaskStatus::failed);
          break;
        case AsyncPollStatus::suspend:
          // For inline executor, suspended means the task paused mid-execution.
          // The runtime can resume it later.
          state->status = AsyncTaskStatus::suspended;
          state->event = evt;
          return evt;
      }
      return evt;
    }

  private:
    SmallString _name;
  };

  /// =========================================================================
  /// AsyncThreadPoolExecutor — dispatches work to ManagedThread workers
  /// =========================================================================

  struct AsyncThreadPoolExecutor : AsyncExecutor {
    using SmallString = BasicSmallString<>;

    struct WorkItem {
      AsyncSubmissionState* state{nullptr};
      AsyncEvent event{};
    };

    explicit AsyncThreadPoolExecutor(const char* executorName = "thread_pool",
                                     size_t workerCount = 1)
        : _name{executorName}, _running{true} {
      if (workerCount == 0) workerCount = 1;
      if (workerCount > 16) workerCount = 16;  // reasonable cap
      _workerCount = workerCount;
      for (size_t i = 0; i < workerCount; ++i) {
        _workers[i].start(
            [this](ManagedThread& self) {
              while (!self.stop_requested()) {
                WorkItem item;
                if (_queue.try_dequeue(item)) {
                  run_item_(item);
                } else {
                  // Wait for new work via futex
                  auto cur = _signal.load(std::memory_order_acquire);
                  if (_queue.empty_approx() && !self.stop_requested()) {
                    Futex::wait_for(&_signal, cur, 10);  // 10ms timeout
                  }
                }
              }
              // Drain remaining items on shutdown
              WorkItem item;
              while (_queue.try_dequeue(item)) run_item_(item);
            },
            executorName);
      }
    }

    ~AsyncThreadPoolExecutor() override { shutdown(); }

    void shutdown() override {
      if (!_running.exchange(false)) return;
      for (size_t i = 0; i < _workerCount; ++i) _workers[i].request_stop();
      wake_all_();
      for (size_t i = 0; i < _workerCount; ++i) _workers[i].join();
    }

    AsyncBackend backend() const override { return AsyncBackend::thread_pool; }
    const char* name() const override { return _name.asChars(); }

    AsyncEvent submit(AsyncSubmissionState* state) override {
      auto evt = AsyncEvent::create();
      if (!state || !state->step) {
        evt.complete(AsyncTaskStatus::failed);
        return evt;
      }

      WorkItem item{state, evt};
      _queue.try_enqueue(zs::move(item));
      wake_one_();
      return evt;
    }

  private:
    void run_item_(WorkItem& item) {
      auto* state = item.state;
      if (!state || !state->step) {
        item.event.complete(AsyncTaskStatus::failed);
        return;
      }

      // Check prerequisites
      for (size_t i = 0; i < state->prerequisites.size(); ++i) {
        state->prerequisites[i].wait();
        if (state->stopOnPrerequisiteFailure
            && state->prerequisites[i].status() != AsyncTaskStatus::completed) {
          item.event.complete(AsyncTaskStatus::cancelled);
          return;
        }
      }

      AsyncExecutionContext ctx{};
      ctx.submissionId = state->id;
      ctx.desc = &state->desc;
      ctx.endpoint = &state->endpoint;
      ctx.cancellation = state->cancellation;

      auto poll = state->step(ctx);
      switch (poll) {
        case AsyncPollStatus::completed:
          item.event.complete(AsyncTaskStatus::completed);
          break;
        case AsyncPollStatus::cancelled:
          item.event.complete(AsyncTaskStatus::cancelled);
          break;
        case AsyncPollStatus::failed:
          item.event.complete(AsyncTaskStatus::failed);
          break;
        case AsyncPollStatus::suspend:
          state->status = AsyncTaskStatus::suspended;
          state->event = item.event;
          break;
      }
    }

    void wake_one_() {
      _signal.fetch_add(1, std::memory_order_release);
      Futex::wake(&_signal, 1);
    }
    void wake_all_() {
      _signal.fetch_add(1, std::memory_order_release);
      Futex::wake(&_signal);
    }

    SmallString _name;
    std::atomic<bool> _running{false};
    std::atomic<u32> _signal{0};
    size_t _workerCount{0};
    ManagedThread _workers[16]{};
    ConcurrentQueue<WorkItem, 4096> _queue;
  };

}  // namespace zs
