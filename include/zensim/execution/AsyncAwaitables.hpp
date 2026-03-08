#pragma once

#if defined(__has_include) && !__has_include(<coroutine>)
#  error "AsyncAwaitables.hpp requires C++20 coroutine support."
#endif

#include <coroutine>

#include "zensim/ZpcAsync.hpp"
#include "zensim/ZpcCoroutine.hpp"
#include "zensim/execution/AsyncEvent.hpp"
#include "zensim/execution/AsyncScheduler.hpp"
#include "zensim/execution/ConcurrencyPrimitive.hpp"

namespace zs {

  /// =========================================================================
  /// schedule_on — resume a coroutine on a specific scheduler/worker
  /// =========================================================================
  /// co_await schedule_on(scheduler)       — resume on any worker
  /// co_await schedule_on(scheduler, 2)    — resume on worker 2

  inline auto schedule_on(AsyncScheduler& sched, i32 workerId = -1) {
    return sched.schedule(workerId);
  }

  /// =========================================================================
  /// AsyncFlag — single-shot flag, co_await-able
  /// =========================================================================
  /// Used to park a coroutine until signaled from another context.

  struct AsyncFlag {
    struct Awaiter {
      AsyncFlag& flag;
      std::coroutine_handle<> _continuation{};

      bool await_ready() const noexcept {
        return flag._signaled.load(std::memory_order_acquire);
      }
      void await_suspend(std::coroutine_handle<> h) noexcept {
        _continuation = h;
        // Store the continuation so signal() can resume it
        flag._waiter = h;
        // Double-check: if signaled between await_ready and here, resume immediately
        if (flag._signaled.load(std::memory_order_acquire)) {
          h.resume();
        }
      }
      void await_resume() const noexcept {}
    };

    auto operator co_await() noexcept { return Awaiter{*this}; }

    void signal() noexcept {
      _signaled.store(true, std::memory_order_release);
      auto w = _waiter;
      if (w) w.resume();
    }

    bool is_signaled() const noexcept { return _signaled.load(std::memory_order_acquire); }
    void reset() noexcept {
      _signaled.store(false, std::memory_order_relaxed);
      _waiter = {};
    }

  private:
    std::atomic<bool> _signaled{false};
    std::coroutine_handle<> _waiter{};
  };

  /// =========================================================================
  /// sync_wait — block the current thread until an awaitable completes
  /// =========================================================================
  /// This is the bridge between sync and async worlds.
  ///
  /// Usage:
  ///   int result = sync_wait(my_async_function());

  namespace detail {
    struct SyncWaitState {
      Mutex mutex{};
      ConditionVariable cv{};
      bool done{false};
    };

    template <typename R> struct SyncWaitPromise : PromiseBase {
      SyncWaitState* state{nullptr};

      Future<R> get_return_object() noexcept {
        return std::coroutine_handle<SyncWaitPromise>::from_promise(*this);
      }
      void unhandled_exception() { _exception = std::current_exception(); }

      template <typename V> void return_value(V&& v) {
        _result = zs::forward<V>(v);
        signal_done_();
      }

      R get() {
        if (_exception) std::rethrow_exception(_exception);
        return zs::move(_result);
      }

    private:
      void signal_done_() {
        if (state) {
          state->mutex.lock();
          state->done = true;
          state->mutex.unlock();
          state->cv.notify_one();
        }
      }
      R _result{};
      std::exception_ptr _exception{};
    };
  }  // namespace detail

  template <typename Awaitable> auto sync_wait(Awaitable&& awaitable) {
    // Simple approach: run the coroutine in a blocking manner
    auto task = zs::forward<Awaitable>(awaitable);

    // If it's a Future, just drive it to completion
    while (!task.isDone() && !task.isReady()) {
      if (!task.isInProgress()) task.resume();
    }
    return task.get();
  }

  /// void specialization
  inline void sync_wait(Future<void>&& task) {
    while (!task.isDone() && !task.isReady()) {
      if (!task.isInProgress()) task.resume();
    }
    task.get();  // rethrows if exception
  }

  /// =========================================================================
  /// when_all_ready — wait for multiple futures to complete
  /// =========================================================================
  /// Returns when all futures are done. Does NOT aggregate results (use
  /// individual .get() after).
  ///
  /// Usage:
  ///   auto [a, b, c] = sync_wait(when_all_ready(fa, fb, fc));
  ///   // or just:
  ///   when_all_ready_sync(fa, fb, fc);

  template <typename... Futures> void when_all_ready_sync(Futures&... futures) {
    // Simple polling: drive all to completion
    bool allDone = false;
    while (!allDone) {
      allDone = true;
      auto check = [&](auto& f) {
        if (!f.isDone() && !f.isReady()) {
          allDone = false;
          if (!f.isInProgress()) f.resume();
        }
      };
      (check(futures), ...);
    }
  }

  /// =========================================================================
  /// AsyncEventAwaiter — co_await an AsyncEvent
  /// =========================================================================
  /// Bridges the callback-based AsyncEvent into coroutine world.

  struct AsyncEventAwaiter {
    AsyncEvent event;

    bool await_ready() const noexcept { return event.ready(); }

    void await_suspend(std::coroutine_handle<> h) {
      event.on_complete([h]() mutable { h.resume(); });
    }

    AsyncTaskStatus await_resume() const noexcept { return event.status(); }
  };

  /// co_await an AsyncEvent
  inline AsyncEventAwaiter co_await_event(const AsyncEvent& evt) {
    return AsyncEventAwaiter{evt};
  }

}  // namespace zs
