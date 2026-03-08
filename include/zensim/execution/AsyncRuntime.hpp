#pragma once
#include "zensim/ZpcAsync.hpp"
#include "zensim/ZpcFunction.hpp"
#include "zensim/execution/AsyncEvent.hpp"
#include "zensim/execution/AsyncExecutor.hpp"
#include "zensim/types/SmallVector.hpp"

namespace zs {

  /// =========================================================================
  /// AsyncSubmissionHandle — lightweight handle for tracking submitted tasks
  /// =========================================================================

  struct AsyncSubmissionHandle {
    u64 id() const noexcept { return _id; }
    AsyncEvent event() const noexcept { return _event; }
    AsyncTaskStatus status() const noexcept {
      // For suspended tasks, the event stays pending — check submission state directly
      if (_state && _state->status == AsyncTaskStatus::suspended)
        return AsyncTaskStatus::suspended;
      return _event.status();
    }
    bool valid() const noexcept { return _id != 0; }

    u64 _id{0};
    AsyncEvent _event{};
    AsyncSubmissionState* _state{nullptr};  // internal, for resume support
  };

  /// =========================================================================
  /// AsyncRuntime — central task scheduling and coordination engine
  /// =========================================================================
  /// Manages named executors, dispatches submissions, tracks state.
  ///
  /// Usage:
  ///   AsyncRuntime runtime{4};  // 4 thread-pool workers
  ///   auto handle = runtime.submit(AsyncSubmission{
  ///       "thread_pool",
  ///       AsyncTaskDesc{"my-task", AsyncDomain::thread, AsyncQueueClass::compute},
  ///       make_host_endpoint(AsyncBackend::thread_pool),
  ///       [](AsyncExecutionContext& ctx) { return AsyncPollStatus::completed; }
  ///   });
  ///   handle.event().wait();

  struct AsyncRuntime {
    static constexpr size_t kMaxExecutors = 16;
    static constexpr size_t kMaxSubmissions = 4096;

    using SmallString = BasicSmallString<>;

    explicit AsyncRuntime(size_t workerCount = 1);
    ~AsyncRuntime();

    AsyncRuntime(const AsyncRuntime&) = delete;
    AsyncRuntime& operator=(const AsyncRuntime&) = delete;

    /// Check if an executor is registered by name.
    bool contains_executor(const char* name) const noexcept;

    /// Register a custom executor. Takes ownership.
    void register_executor(const char* name, AsyncExecutor* executor);

    /// Submit a task. Returns a handle for tracking.
    AsyncSubmissionHandle submit(AsyncSubmission submission);

    /// Resume a suspended task. Returns false if not resumable.
    bool resume(const AsyncSubmissionHandle& handle);

    /// Run a single step of a submission (used internally and for inline execution).
    static AsyncPollStatus run_step(AsyncSubmissionState* state);

  private:
    struct ExecutorEntry {
      SmallString name{};
      AsyncExecutor* executor{nullptr};
      bool owned{false};  // if true, runtime deletes on shutdown
    };

    AsyncExecutor* find_executor_(const char* name) const noexcept;

    ExecutorEntry _executors[kMaxExecutors]{};
    size_t _numExecutors{0};

    // Submission pool — fixed-size, reusable slots
    AsyncSubmissionState _submissions[kMaxSubmissions]{};
    std::atomic<u64> _nextId{1};

    AsyncSubmissionState* alloc_submission_();
  };

  // =========================================================================
  // Inline implementations
  // =========================================================================

  inline AsyncRuntime::AsyncRuntime(size_t workerCount) {
    // Register default executors
    auto* inlineExec = ::new AsyncInlineExecutor("inline");
    register_executor("inline", inlineExec);
    _executors[0].owned = true;

    auto* poolExec = ::new AsyncThreadPoolExecutor("thread_pool", workerCount);
    register_executor("thread_pool", poolExec);
    _executors[1].owned = true;
  }

  inline AsyncRuntime::~AsyncRuntime() {
    // Shutdown and delete owned executors (in reverse order)
    for (size_t i = _numExecutors; i > 0; --i) {
      if (_executors[i - 1].executor) {
        _executors[i - 1].executor->shutdown();
        if (_executors[i - 1].owned) ::delete _executors[i - 1].executor;
      }
    }
  }

  inline bool AsyncRuntime::contains_executor(const char* name) const noexcept {
    return find_executor_(name) != nullptr;
  }

  inline void AsyncRuntime::register_executor(const char* name, AsyncExecutor* executor) {
    if (_numExecutors >= kMaxExecutors || !executor) return;
    _executors[_numExecutors].name = name;
    _executors[_numExecutors].executor = executor;
    _executors[_numExecutors].owned = false;
    ++_numExecutors;
  }

  inline AsyncExecutor* AsyncRuntime::find_executor_(const char* name) const noexcept {
    SmallString target{name};
    for (size_t i = 0; i < _numExecutors; ++i) {
      // Compare SmallStrings character by character
      bool match = true;
      const char* a = _executors[i].name.asChars();
      const char* b = target.asChars();
      for (size_t j = 0; a[j] || b[j]; ++j) {
        if (a[j] != b[j]) {
          match = false;
          break;
        }
      }
      if (match) return _executors[i].executor;
    }
    return nullptr;
  }

  inline AsyncSubmissionState* AsyncRuntime::alloc_submission_() {
    // Simple linear scan for an unused slot (status == pending with id == 0)
    // In production, use a free-list or ring buffer.
    for (size_t i = 0; i < kMaxSubmissions; ++i) {
      if (_submissions[i].id == 0) {
        _submissions[i].id = _nextId.fetch_add(1, std::memory_order_relaxed);
        return &_submissions[i];
      }
    }
    return nullptr;  // pool exhausted
  }

  inline AsyncSubmissionHandle AsyncRuntime::submit(AsyncSubmission submission) {
    AsyncSubmissionHandle handle{};

    auto* executor = find_executor_(submission.executor.asChars());
    if (!executor) return handle;

    auto* state = alloc_submission_();
    if (!state) return handle;

    state->desc = zs::move(submission.desc);
    state->endpoint = zs::move(submission.endpoint);
    state->step = zs::move(submission.step);
    state->cancellation = zs::move(submission.cancellation);
    state->prerequisites = zs::move(submission.prerequisites);
    state->stopOnPrerequisiteFailure = submission.stopOnPrerequisiteFailure;
    state->status = AsyncTaskStatus::pending;

    auto event = executor->submit(state);
    state->event = event;

    handle._id = state->id;
    handle._event = event;
    handle._state = state;
    return handle;
  }

  inline bool AsyncRuntime::resume(const AsyncSubmissionHandle& handle) {
    if (!handle.valid() || !handle._state) return false;
    auto* state = handle._state;
    if (state->status != AsyncTaskStatus::suspended) return false;

    state->status = AsyncTaskStatus::running;

    auto* executor = find_executor_("inline");  // resume always runs inline
    if (!executor) return false;

    AsyncExecutionContext ctx{};
    ctx.submissionId = state->id;
    ctx.desc = &state->desc;
    ctx.endpoint = &state->endpoint;
    ctx.cancellation = state->cancellation;

    auto poll = state->step(ctx);
    switch (poll) {
      case AsyncPollStatus::completed:
        state->event.complete(AsyncTaskStatus::completed);
        state->status = AsyncTaskStatus::completed;
        break;
      case AsyncPollStatus::cancelled:
        state->event.complete(AsyncTaskStatus::cancelled);
        state->status = AsyncTaskStatus::cancelled;
        break;
      case AsyncPollStatus::failed:
        state->event.complete(AsyncTaskStatus::failed);
        state->status = AsyncTaskStatus::failed;
        break;
      case AsyncPollStatus::suspend:
        state->status = AsyncTaskStatus::suspended;
        break;
    }
    return true;
  }

  inline AsyncPollStatus AsyncRuntime::run_step(AsyncSubmissionState* state) {
    if (!state || !state->step) return AsyncPollStatus::failed;

    AsyncExecutionContext ctx{};
    ctx.submissionId = state->id;
    ctx.desc = &state->desc;
    ctx.endpoint = &state->endpoint;
    ctx.cancellation = state->cancellation;

    return state->step(ctx);
  }

}  // namespace zs
