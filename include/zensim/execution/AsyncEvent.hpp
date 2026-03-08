#pragma once
#include <atomic>

#include "zensim/ZpcAsync.hpp"
#include "zensim/ZpcFunction.hpp"
#include "zensim/execution/ConcurrencyPrimitive.hpp"

namespace zs {

  /// =========================================================================
  /// AsyncTaskStatus — lifecycle states of an async task
  /// =========================================================================
  enum class AsyncTaskStatus : u8 {
    pending = 0,
    running,
    suspended,
    completed,
    cancelled,
    failed
  };

  constexpr bool is_terminal(AsyncTaskStatus s) noexcept {
    return s == AsyncTaskStatus::completed || s == AsyncTaskStatus::cancelled
           || s == AsyncTaskStatus::failed;
  }

  /// =========================================================================
  /// AsyncEvent — waitable, ref-counted completion event with callback chain
  /// =========================================================================
  /// Manual ref counting (no std::shared_ptr).
  /// Uses existing zs::Mutex + zs::ConditionVariable (futex-based).

  struct AsyncEventState {
    std::atomic<u32> refCount{1};
    std::atomic<AsyncTaskStatus> status{AsyncTaskStatus::pending};
    Mutex mutex{};
    ConditionVariable cv{};
    StaticVector<function<void()>, 8> callbacks{};

    void addRef() noexcept { refCount.fetch_add(1, std::memory_order_relaxed); }
    void release() noexcept {
      if (refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) ::delete this;
    }

    void complete(AsyncTaskStatus final_status) {
      auto prev = status.load(std::memory_order_acquire);
      if (is_terminal(prev)) return;  // already terminal

      status.store(final_status, std::memory_order_release);

      // Fire callbacks under lock, then notify waiters
      StaticVector<function<void()>, 8> cbs;
      {
        mutex.lock();
        cbs = zs::move(callbacks);
        callbacks.clear();
        mutex.unlock();
      }
      for (size_t i = 0; i < cbs.size(); ++i) {
        if (cbs[i]) cbs[i]();
      }
      cv.notify_all();
    }

    void wait() noexcept {
      if (is_terminal(status.load(std::memory_order_acquire))) return;
      mutex.lock();
      while (!is_terminal(status.load(std::memory_order_acquire))) cv.wait(mutex);
      mutex.unlock();
    }

    /// @return true if completed before timeout, false if timed out
    /// @param timeout_ms timeout in milliseconds
    bool wait_for(i64 timeout_ms) noexcept {
      if (is_terminal(status.load(std::memory_order_acquire))) return true;
      mutex.lock();
      if (!is_terminal(status.load(std::memory_order_acquire))) {
        cv.wait_for(mutex, timeout_ms);
      }
      bool result = is_terminal(status.load(std::memory_order_acquire));
      mutex.unlock();
      return result;
    }
  };

  struct AsyncEvent {
    AsyncEvent() noexcept : _state{nullptr} {}

    /// Create a live event (pending state)
    static AsyncEvent create() {
      AsyncEvent e;
      e._state = ::new AsyncEventState{};
      return e;
    }

    // Copy (shares state)
    AsyncEvent(const AsyncEvent& o) noexcept : _state{o._state} {
      if (_state) _state->addRef();
    }
    AsyncEvent& operator=(const AsyncEvent& o) noexcept {
      if (this != &o) {
        if (_state) _state->release();
        _state = o._state;
        if (_state) _state->addRef();
      }
      return *this;
    }
    // Move
    AsyncEvent(AsyncEvent&& o) noexcept : _state{zs::exchange(o._state, nullptr)} {}
    AsyncEvent& operator=(AsyncEvent&& o) noexcept {
      if (this != &o) {
        if (_state) _state->release();
        _state = zs::exchange(o._state, nullptr);
      }
      return *this;
    }
    ~AsyncEvent() {
      if (_state) _state->release();
    }

    /// Block until terminal state
    void wait() const {
      if (_state) _state->wait();
    }

    /// Block with timeout (milliseconds). Returns true if completed.
    bool wait_for(i64 timeout_ms) const {
      return _state ? _state->wait_for(timeout_ms) : true;
    }

    /// Current status
    AsyncTaskStatus status() const noexcept {
      return _state ? _state->status.load(std::memory_order_acquire) : AsyncTaskStatus::completed;
    }

    /// Is in terminal state?
    bool ready() const noexcept { return is_terminal(status()); }

    /// Register a callback to fire on completion. If already complete, fires immediately.
    void on_complete(function<void()> cb) const {
      if (!_state || !cb) return;
      if (is_terminal(_state->status.load(std::memory_order_acquire))) {
        cb();
        return;
      }
      _state->mutex.lock();
      if (is_terminal(_state->status.load(std::memory_order_acquire))) {
        _state->mutex.unlock();
        cb();
      } else {
        if (!_state->callbacks.full()) _state->callbacks.push_back(zs::move(cb));
        _state->mutex.unlock();
      }
    }

    /// Signal completion (used by executors)
    void complete(AsyncTaskStatus s = AsyncTaskStatus::completed) const {
      if (_state) _state->complete(s);
    }

    explicit operator bool() const noexcept { return _state != nullptr; }

  private:
    AsyncEventState* _state;
  };

}  // namespace zs
