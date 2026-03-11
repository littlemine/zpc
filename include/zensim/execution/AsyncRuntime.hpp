#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "zensim/ZpcAsync.hpp"
#include "zensim/ZpcFunction.hpp"
#include "zensim/container/ConcurrentQueue.hpp"
#include "zensim/execution/ConcurrencyPrimitive.hpp"
#include "zensim/execution/Intrinsics.hpp"
#include "zensim/execution/ManagedThread.hpp"
#include "zensim/types/ImplPattern.hpp"

namespace zs {

  enum class AsyncDomain : u8 { control, inter_node, process, thread, compute, graphics };
  enum class AsyncBackend : u8 {
    inline_host,
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
    control,
    io,
    compute,
    transfer,
    graphics,
    present,
    collective,
    copy
  };
  enum class AsyncTaskStatus : u8 {
    pending,
    running,
    suspended,
    completed,
    cancelled,
    failed
  };

  inline bool is_terminal(AsyncTaskStatus status) noexcept {
    return status == AsyncTaskStatus::completed || status == AsyncTaskStatus::cancelled
        || status == AsyncTaskStatus::failed;
  }

  struct AsyncEndpoint {
    AsyncBackend backend{AsyncBackend::inline_host};
    AsyncQueueClass queue{AsyncQueueClass::control};
    NodeID node{0};
    ProcID process{-1};
    i32 device{-1};
    StreamID stream{-1};
    void *nativeHandle{nullptr};
    SmallString label{};
  };

  inline AsyncEndpoint make_host_endpoint(AsyncBackend backend = AsyncBackend::inline_host,
                                          AsyncQueueClass queue = AsyncQueueClass::control,
                                          SmallString label = {}) {
    return AsyncEndpoint{backend, queue, 0, -1, -1, -1, nullptr, label};
  }
  inline AsyncEndpoint make_asio_endpoint(NodeID node, ProcID process, void *ioContext,
                                          SmallString label = {}) {
    return AsyncEndpoint{AsyncBackend::asio, AsyncQueueClass::io, node, process, -1, -1,
                         ioContext, label};
  }
  inline AsyncEndpoint make_process_endpoint(NodeID node, ProcID process, void *processHandle,
                                             SmallString label = {}) {
    return AsyncEndpoint{AsyncBackend::process, AsyncQueueClass::control, node, process, -1, -1,
                         processHandle, label};
  }
  inline AsyncEndpoint make_cuda_endpoint(i32 device, StreamID stream, void *cudaStream,
                                          AsyncQueueClass queue = AsyncQueueClass::compute,
                                          SmallString label = {}) {
    return AsyncEndpoint{AsyncBackend::cuda, queue, 0, -1, device, stream, cudaStream, label};
  }
  inline AsyncEndpoint make_nccl_endpoint(i32 device, StreamID stream, void *communicator,
                                          SmallString label = {}) {
    return AsyncEndpoint{AsyncBackend::nccl, AsyncQueueClass::collective, 0, -1, device, stream,
                         communicator, label};
  }
  inline AsyncEndpoint make_rocm_endpoint(i32 device, StreamID stream, void *hipStream,
                                          AsyncQueueClass queue = AsyncQueueClass::compute,
                                          SmallString label = {}) {
    return AsyncEndpoint{AsyncBackend::rocm, queue, 0, -1, device, stream, hipStream, label};
  }
  inline AsyncEndpoint make_sycl_endpoint(i32 device, StreamID stream, void *syclQueue,
                                          AsyncQueueClass queue = AsyncQueueClass::compute,
                                          SmallString label = {}) {
    return AsyncEndpoint{AsyncBackend::sycl, queue, 0, -1, device, stream, syclQueue, label};
  }
  inline AsyncEndpoint make_vulkan_endpoint(i32 device, StreamID queueFamily, void *vkQueue,
                                            AsyncQueueClass queue = AsyncQueueClass::graphics,
                                            SmallString label = {}) {
    return AsyncEndpoint{AsyncBackend::vulkan, queue, 0, -1, device, queueFamily, vkQueue,
                         label};
  }

  struct AsyncTaskDesc {
    SmallString label{};
    AsyncDomain domain{AsyncDomain::control};
    AsyncQueueClass queue{AsyncQueueClass::control};
    int priority{0};
    size_t grainSize{1};
    bool profile{false};
  };

  class AsyncEvent {
  public:
    using callback_t = function<void()>;

    AsyncEvent() : _state{zs::make_shared<SharedState>()} {}

    static AsyncEvent create() { return AsyncEvent{}; }

    void wait() const {
      _state->cv.wait(_state->mutex,
                      [state = _state.get()] { return is_terminal(state->status.load()); });
    }

    bool wait_for(i64 timeoutMs) const {
      return _state->cv.wait_for(_state->mutex, timeoutMs,
                                 [state = _state.get()] { return is_terminal(state->status.load()); });
    }

    AsyncTaskStatus status() const noexcept {
      return _state->status.load();
    }

    bool ready() const noexcept { return is_terminal(status()); }

    void complete(AsyncTaskStatus status = AsyncTaskStatus::completed) const { transition(status); }

    void on_complete(callback_t callback) const {
      bool invokeNow = false;
      {
        std::lock_guard<Mutex> lock(_state->mutex);
        if (is_terminal(_state->status.load()))
          invokeNow = true;
        else
          _state->callbacks.push_back(zs::move(callback));
      }
      if (invokeNow) callback();
    }

  private:
    struct SharedState {
      mutable Mutex mutex{};
      ConditionVariable cv{};
      Atomic<AsyncTaskStatus> status{AsyncTaskStatus::pending};
      std::vector<callback_t> callbacks{};
    };

    void transition(AsyncTaskStatus next) const {
      std::vector<callback_t> callbacks;
      {
        std::lock_guard<Mutex> lock(_state->mutex);
        const auto previous = _state->status.load();
        if (is_terminal(previous)) return;
        _state->status.store(next);
        if (is_terminal(next)) callbacks.swap(_state->callbacks);
      }
      _state->cv.notify_all();
      for (auto &callback : callbacks) callback();
    }

    void mark_running() const { transition(AsyncTaskStatus::running); }
    void mark_suspended() const { transition(AsyncTaskStatus::suspended); }
    void mark_completed() const { transition(AsyncTaskStatus::completed); }
    void mark_cancelled() const { transition(AsyncTaskStatus::cancelled); }
    void mark_failed() const { transition(AsyncTaskStatus::failed); }

    Shared<SharedState> _state;

    friend class AsyncExecutor;
    friend class AsyncInlineExecutor;
    friend class AsyncNativeQueueExecutor;
    friend class AsyncThreadPoolExecutor;
    friend class AsyncRuntime;
  };

  struct AsyncExecutionContext {
    u64 submissionId{0};
    const AsyncTaskDesc *desc{nullptr};
    const AsyncEndpoint *endpoint{nullptr};
    AsyncStopToken cancellation{};
  };

  using AsyncStep = function<AsyncPollStatus(AsyncExecutionContext &)>;

  struct AsyncSubmission {
    SmallString executor{"inline"};
    AsyncTaskDesc desc{};
    AsyncEndpoint endpoint{};
    AsyncStep step{};
    std::vector<AsyncEvent> prerequisites{};
    AsyncStopToken cancellation{};
    bool stopOnPrerequisiteFailure{true};
  };

  struct AsyncSubmissionState;

  class AsyncExecutor {
  public:
    virtual ~AsyncExecutor() = default;

    virtual AsyncBackend backend() const noexcept = 0;
    virtual std::string_view name() const noexcept = 0;
    virtual AsyncEvent submit(Shared<AsyncSubmissionState> state) = 0;
  };

  struct AsyncSubmissionState {
    u64 id{0};
    SmallString executor{};
    AsyncTaskDesc desc{};
    AsyncEndpoint endpoint{};
    AsyncStopToken cancellation{};
    AsyncStep step{};
    AsyncEvent event{};
    atomic_bool inFlight{false};
  };

  namespace detail {
    inline void finalize_async_submission(const Shared<AsyncSubmissionState> &state,
                                          AsyncPollStatus poll) {
      if (poll == AsyncPollStatus::suspend)
        state->event.complete(AsyncTaskStatus::suspended);
      else if (poll == AsyncPollStatus::completed)
        state->event.complete(AsyncTaskStatus::completed);
      else if (poll == AsyncPollStatus::cancelled)
        state->event.complete(AsyncTaskStatus::cancelled);
      else
        state->event.complete(AsyncTaskStatus::failed);
      state->inFlight.store(false);
    }
  }  // namespace detail

  class AsyncSubmissionHandle {
  public:
    AsyncSubmissionHandle() = default;

    u64 id() const noexcept { return _state ? _state->id : 0; }
    AsyncEvent event() const noexcept { return _state ? _state->event : AsyncEvent{}; }
    AsyncTaskStatus status() const noexcept {
      return _state ? _state->event.status() : AsyncTaskStatus::failed;
    }
    bool valid() const noexcept { return static_cast<bool>(_state); }

  private:
    explicit AsyncSubmissionHandle(Shared<AsyncSubmissionState> state)
        : _state{zs::move(state)} {}

    Shared<AsyncSubmissionState> _state{};

    friend class AsyncRuntime;
  };

  class AsyncInlineExecutor : public AsyncExecutor {
  public:
    explicit AsyncInlineExecutor(std::string executorName = "inline")
        : _name{zs::move(executorName)} {}

    AsyncBackend backend() const noexcept override { return AsyncBackend::inline_host; }
    std::string_view name() const noexcept override { return _name; }
    AsyncEvent submit(Shared<AsyncSubmissionState> state) override;

  private:
    std::string _name;
  };

  class AsyncThreadPoolExecutor : public AsyncExecutor {
  public:
    explicit AsyncThreadPoolExecutor(std::string executorName = "thread_pool", size_t workerCount = 1);
    ~AsyncThreadPoolExecutor() override;

    AsyncThreadPoolExecutor(const AsyncThreadPoolExecutor &) = delete;
    AsyncThreadPoolExecutor &operator=(const AsyncThreadPoolExecutor &) = delete;

    AsyncBackend backend() const noexcept override { return AsyncBackend::thread_pool; }
    std::string_view name() const noexcept override { return _name; }
    AsyncEvent submit(Shared<AsyncSubmissionState> state) override;
    void shutdown() noexcept;

  private:
    struct WorkItem {
      Shared<AsyncSubmissionState> state{};
    };

    void worker_loop(ManagedThread &thread);
    void wake_one() noexcept;
    void wake_all() noexcept;

    std::string _name;
    AsyncStopSource _stop{};
    atomic_bool _running{true};
    Atomic<u32> _signal{0};
    size_t _workerCount{0};
    ConcurrentQueue<WorkItem, 4096> _queue{};
    std::vector<Unique<ManagedThread>> _workers{};
  };

  class AsyncRuntime {
  public:
    explicit AsyncRuntime(size_t workerCount = 1);

    bool contains_executor(const char *name) const;
    bool contains_executor(const std::string &name) const;
    void register_executor(std::string name, Shared<AsyncExecutor> executor);
    AsyncSubmissionHandle submit(AsyncSubmission submission);
    bool resume(const AsyncSubmissionHandle &handle);
    static AsyncPollStatus run_step(const Shared<AsyncSubmissionState> &state);

  private:
    Shared<AsyncExecutor> get_executor(const SmallString &name) const;
    void dispatch(const Shared<AsyncSubmissionState> &state,
                  const Shared<AsyncExecutor> &executor) const;

    mutable Mutex _mutex{};
    std::unordered_map<std::string, Shared<AsyncExecutor>> _executors{};
    Atomic<u64> _nextSubmissionId{0};
  };

  inline AsyncEvent AsyncInlineExecutor::submit(Shared<AsyncSubmissionState> state) {
    if (state->cancellation.stop_requested() || state->cancellation.interrupt_requested()) {
      state->event.mark_cancelled();
      state->inFlight.store(false);
      return state->event;
    }
    state->event.mark_running();
    detail::finalize_async_submission(state, AsyncRuntime::run_step(state));
    return state->event;
  }

  inline AsyncThreadPoolExecutor::AsyncThreadPoolExecutor(std::string executorName, size_t workerCount)
      : _name{zs::move(executorName)} {
    if (workerCount == 0) workerCount = 1;
    if (workerCount > 16) workerCount = 16;
    _workerCount = workerCount;
    _workers.reserve(workerCount);
    for (size_t i = 0; i != workerCount; ++i) {
      auto worker = zs::make_unique<ManagedThread>();
      worker->start([this](ManagedThread &thread) { worker_loop(thread); }, "async-worker");
      _workers.push_back(zs::move(worker));
    }
  }

  inline AsyncThreadPoolExecutor::~AsyncThreadPoolExecutor() { shutdown(); }

  inline void AsyncThreadPoolExecutor::shutdown() noexcept {
    if (!_running.exchange(false)) return;
    _stop.request_stop();
    wake_all();
    for (auto &worker : _workers)
      if (worker && worker->joinable()) worker->join();
    _workers.clear();
  }

  inline AsyncEvent AsyncThreadPoolExecutor::submit(Shared<AsyncSubmissionState> state) {
    if (!state) return {};
    if (!_running.load()) {
      state->event.mark_failed();
      state->inFlight.store(false);
      return state->event;
    }

    if (state->cancellation.stop_requested() || state->cancellation.interrupt_requested()) {
      state->event.mark_cancelled();
      state->inFlight.store(false);
      return state->event;
    }

    WorkItem item{state};
    size_t enqueueAttempts = 0;
    while (!_queue.try_enqueue(zs::move(item))) {
      if (!_running.load() || _stop.stop_requested()) {
        state->event.mark_failed();
        state->inFlight.store(false);
        return state->event;
      }
      if (enqueueAttempts < 64)
        pause_cpu();
      else
        ManagedThread::yield_current();
      ++enqueueAttempts;
    }
    wake_one();
    return state->event;
  }

  inline void AsyncThreadPoolExecutor::wake_one() noexcept {
    _signal.fetch_add(1);
    Futex::wake(&_signal, 1);
  }

  inline void AsyncThreadPoolExecutor::wake_all() noexcept {
    _signal.fetch_add(1);
    Futex::wake(&_signal);
  }

  inline void AsyncThreadPoolExecutor::worker_loop(ManagedThread &thread) {
    size_t idleAttempts = 0;
    for (;;) {
      WorkItem item;
      if (!_queue.try_dequeue(item)) {
        if ((_stop.stop_requested() || thread.stop_requested()) && _queue.empty_approx()) return;
        if (idleAttempts < 64) {
          ++idleAttempts;
          pause_cpu();
          continue;
        }
        if (idleAttempts < 128) {
          ++idleAttempts;
          ManagedThread::yield_current();
          continue;
        }
        const auto currentSignal = _signal.load();
        if (_queue.empty_approx() && !_stop.stop_requested() && !thread.stop_requested())
          Futex::wait_for(&_signal, currentSignal, 10);
        idleAttempts = 0;
        continue;
      }
      idleAttempts = 0;

      auto &state = item.state;
      if (!state) continue;

      if (state->cancellation.stop_requested() || state->cancellation.interrupt_requested()) {
        state->event.mark_cancelled();
        state->inFlight.store(false);
        continue;
      }

      state->event.mark_running();
      detail::finalize_async_submission(state, AsyncRuntime::run_step(state));
    }
  }

  inline AsyncRuntime::AsyncRuntime(size_t workerCount) {
    register_executor("inline", zs::make_shared<AsyncInlineExecutor>());
    register_executor("thread_pool",
              zs::make_shared<AsyncThreadPoolExecutor>("thread_pool", workerCount));
  }

  inline bool AsyncRuntime::contains_executor(const char *name) const {
    return contains_executor(std::string{name});
  }

  inline bool AsyncRuntime::contains_executor(const std::string &name) const {
    std::lock_guard<Mutex> lock(_mutex);
    return _executors.find(name) != _executors.end();
  }

  inline void AsyncRuntime::register_executor(std::string name,
                                              Shared<AsyncExecutor> executor) {
    std::lock_guard<Mutex> lock(_mutex);
    _executors.insert_or_assign(zs::move(name), zs::move(executor));
  }

  inline Shared<AsyncExecutor> AsyncRuntime::get_executor(const SmallString &name) const {
    std::lock_guard<Mutex> lock(_mutex);
    if (auto it = _executors.find(name.asChars()); it != _executors.end()) return it->second;
    return {};
  }

  inline AsyncPollStatus AsyncRuntime::run_step(const Shared<AsyncSubmissionState> &state) {
    try {
      AsyncExecutionContext ctx{state->id, &state->desc, &state->endpoint, state->cancellation};
      if (!state->step) return AsyncPollStatus::failed;
      return state->step(ctx);
    } catch (...) {
      return AsyncPollStatus::failed;
    }
  }

  inline void AsyncRuntime::dispatch(const Shared<AsyncSubmissionState> &state,
                                     const Shared<AsyncExecutor> &executor) const {
    bool expected = false;
    if (!state->inFlight.compare_exchange_strong(expected, true)) return;
    executor->submit(state);
  }

  inline AsyncSubmissionHandle AsyncRuntime::submit(AsyncSubmission submission) {
    auto executor = get_executor(submission.executor);
    if (!executor) throw StaticException();

    auto state = zs::make_shared<AsyncSubmissionState>();
    state->id = _nextSubmissionId.fetch_add(1) + 1;
    state->executor = submission.executor;
    state->desc = submission.desc;
    state->endpoint = submission.endpoint;
    state->cancellation = submission.cancellation;
    state->step = zs::move(submission.step);

    AsyncSubmissionHandle handle{state};
    if (submission.prerequisites.empty()) {
      dispatch(state, executor);
      return handle;
    }

    struct DependencyState {
      atomic_size_t remaining{0};
      atomic_bool failed{false};
      atomic_bool cancelled{false};
    };

    auto dependencyState = zs::make_shared<DependencyState>();
    dependencyState->remaining.store(submission.prerequisites.size());

    for (const auto &prerequisite : submission.prerequisites) {
      prerequisite.on_complete([prerequisite, dependencyState, state, executor, this,
                                stopOnFailure = submission.stopOnPrerequisiteFailure]() mutable {
        if (stopOnFailure) {
          const auto prerequisiteStatus = prerequisite.status();
          if (prerequisiteStatus == AsyncTaskStatus::failed)
            dependencyState->failed.store(true);
          else if (prerequisiteStatus == AsyncTaskStatus::cancelled)
            dependencyState->cancelled.store(true);
        }

        if (dependencyState->remaining.fetch_sub(1) == 1) {
          if (dependencyState->failed.load())
            state->event.mark_failed();
          else if (dependencyState->cancelled.load())
            state->event.mark_cancelled();
          else
            dispatch(state, executor);
        }
      });
    }

    return handle;
  }

  inline bool AsyncRuntime::resume(const AsyncSubmissionHandle &handle) {
    if (!handle._state) return false;
    const auto current = handle._state->event.status();
    if (current != AsyncTaskStatus::suspended && current != AsyncTaskStatus::pending) return false;
    auto executor = get_executor(handle._state->executor);
    if (!executor) return false;
    dispatch(handle._state, executor);
    return true;
  }

}  // namespace zs