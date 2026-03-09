#pragma once

#if defined(__has_include) && !__has_include(<coroutine>)
#  error "AsyncAwaitables.hpp requires C++20 coroutine support."
#endif

#include <coroutine>

#include "zensim/ZpcAsync.hpp"
#include "zensim/ZpcCoroutine.hpp"
#include "zensim/execution/AsyncRuntime.hpp"
#include "zensim/execution/AsyncScheduler.hpp"
#include "zensim/execution/ConcurrencyPrimitive.hpp"

namespace zs {

  inline auto schedule_on(AsyncScheduler &scheduler, i32 workerId = -1) {
    return scheduler.schedule(workerId);
  }

  struct AsyncFlag {
    struct Awaiter {
      AsyncFlag &flag;
      std::coroutine_handle<> _continuation{};

      bool await_ready() const noexcept { return flag._signaled.load(memory_order_acquire); }

      void await_suspend(std::coroutine_handle<> handle) noexcept {
        _continuation = handle;
        flag._waiter = handle;
        if (flag._signaled.load(memory_order_acquire)) handle.resume();
      }

      void await_resume() const noexcept {}
    };

    auto operator co_await() noexcept { return Awaiter{*this}; }

    void signal() noexcept {
      _signaled.store(true, memory_order_release);
      const auto waiter = _waiter;
      if (waiter) waiter.resume();
    }

    bool is_signaled() const noexcept { return _signaled.load(memory_order_acquire); }

    void reset() noexcept {
      _signaled.store(false, memory_order_relaxed);
      _waiter = {};
    }

  private:
    Atomic<bool> _signaled{false};
    std::coroutine_handle<> _waiter{};
  };

  template <typename Awaitable>
  auto sync_wait(Awaitable &&awaitable) {
    auto task = zs::forward<Awaitable>(awaitable);
    while (!task.isDone() && !task.isReady()) {
      if (!task.isInProgress()) task.resume();
    }
    return task.get();
  }

  inline void sync_wait(Future<void> &&task) {
    while (!task.isDone() && !task.isReady()) {
      if (!task.isInProgress()) task.resume();
    }
    task.get();
  }

  template <typename... Futures>
  void when_all_ready_sync(Futures &...futures) {
    bool allDone = false;
    while (!allDone) {
      allDone = true;
      auto drive = [&](auto &future) {
        if (!future.isDone() && !future.isReady()) {
          allDone = false;
          if (!future.isInProgress()) future.resume();
        }
      };
      (drive(futures), ...);
    }
  }

  struct AsyncEventAwaiter {
    AsyncEvent event;

    bool await_ready() const noexcept { return event.ready(); }

    void await_suspend(std::coroutine_handle<> handle) {
      event.on_complete([handle]() mutable { handle.resume(); });
    }

    AsyncTaskStatus await_resume() const noexcept { return event.status(); }
  };

  inline AsyncEventAwaiter co_await_event(const AsyncEvent &event) {
    return AsyncEventAwaiter{event};
  }

  struct AsyncEventSchedulerAwaiter {
    AsyncEvent event;
    AsyncScheduler *scheduler{nullptr};
    i32 workerId{-1};

    bool await_ready() const noexcept { return event.ready(); }

    void await_suspend(std::coroutine_handle<> handle) {
      event.on_complete([continuation = handle, scheduler = scheduler, worker = workerId]() mutable {
        if (scheduler)
          scheduler->enqueue(continuation, worker);
        else
          continuation.resume();
      });
    }

    AsyncTaskStatus await_resume() const noexcept { return event.status(); }
  };

  inline AsyncEventSchedulerAwaiter co_await_event_on(const AsyncEvent &event,
                                                      AsyncScheduler &scheduler,
                                                      i32 workerId = -1) {
    return AsyncEventSchedulerAwaiter{event, &scheduler, scheduler.resolve_worker_id(workerId)};
  }

  struct AsyncSubmissionAwaiter {
    AsyncSubmissionHandle handle;

    bool await_ready() const noexcept { return !handle.valid() || handle.event().ready(); }

    void await_suspend(std::coroutine_handle<> continuation) {
      handle.event().on_complete([continuation]() mutable { continuation.resume(); });
    }

    AsyncTaskStatus await_resume() const noexcept { return handle.status(); }
  };

  inline AsyncSubmissionAwaiter co_await_submission(const AsyncSubmissionHandle &handle) {
    return AsyncSubmissionAwaiter{handle};
  }

  struct AsyncSubmissionSchedulerAwaiter {
    AsyncSubmissionHandle handle;
    AsyncScheduler *scheduler{nullptr};
    i32 workerId{-1};

    bool await_ready() const noexcept { return !handle.valid() || handle.event().ready(); }

    void await_suspend(std::coroutine_handle<> continuation) {
      handle.event().on_complete(
          [continuation, scheduler = scheduler, worker = workerId]() mutable {
            if (scheduler)
              scheduler->enqueue(continuation, worker);
            else
              continuation.resume();
          });
    }

    AsyncTaskStatus await_resume() const noexcept { return handle.status(); }
  };

  inline AsyncSubmissionSchedulerAwaiter co_await_submission_on(const AsyncSubmissionHandle &handle,
                                                                AsyncScheduler &scheduler,
                                                                i32 workerId = -1) {
    return AsyncSubmissionSchedulerAwaiter{handle, &scheduler,
                                           scheduler.resolve_worker_id(workerId)};
  }

}  // namespace zs