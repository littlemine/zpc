#pragma once
#include <atomic>

#include "zensim/TypeAlias.hpp"
#include "zensim/ZpcMeta.hpp"

namespace zs {

  /// =========================================================================
  /// StaticVector<T, Capacity> — fixed-capacity inline vector, no heap alloc
  /// =========================================================================
  template <typename T, size_t Capacity> struct StaticVector {
    static_assert(Capacity > 0, "StaticVector capacity must be > 0");
    using value_type = T;
    using size_type = size_t;
    static constexpr size_type capacity = Capacity;

    constexpr StaticVector() noexcept : _size{0} {}
    ~StaticVector() { clear(); }

    StaticVector(const StaticVector& o) noexcept(is_nothrow_copy_constructible_v<T>) : _size{0} {
      for (size_type i = 0; i < o._size; ++i) emplace_back(o[i]);
    }
    StaticVector(StaticVector&& o) noexcept(is_nothrow_move_constructible_v<T>) : _size{0} {
      for (size_type i = 0; i < o._size; ++i) emplace_back(zs::move(o[i]));
      o.clear();
    }
    StaticVector& operator=(const StaticVector& o) noexcept(is_nothrow_copy_constructible_v<T>) {
      if (this != &o) {
        clear();
        for (size_type i = 0; i < o._size; ++i) emplace_back(o[i]);
      }
      return *this;
    }
    StaticVector& operator=(StaticVector&& o) noexcept(is_nothrow_move_constructible_v<T>) {
      if (this != &o) {
        clear();
        for (size_type i = 0; i < o._size; ++i) emplace_back(zs::move(o[i]));
        o.clear();
      }
      return *this;
    }

    template <typename... Args> T& emplace_back(Args&&... args) {
      T* p = reinterpret_cast<T*>(_storage) + _size;
      ::new (static_cast<void*>(p)) T(zs::forward<Args>(args)...);
      return *(reinterpret_cast<T*>(_storage) + _size++);
    }
    void push_back(const T& v) { emplace_back(v); }
    void push_back(T&& v) { emplace_back(zs::move(v)); }

    void pop_back() {
      if (_size > 0) {
        --_size;
        reinterpret_cast<T*>(_storage)[_size].~T();
      }
    }

    void clear() {
      for (size_type i = 0; i < _size; ++i) reinterpret_cast<T*>(_storage)[i].~T();
      _size = 0;
    }

    T& operator[](size_type i) { return reinterpret_cast<T*>(_storage)[i]; }
    const T& operator[](size_type i) const { return reinterpret_cast<const T*>(_storage)[i]; }
    T* data() { return reinterpret_cast<T*>(_storage); }
    const T* data() const { return reinterpret_cast<const T*>(_storage); }
    T& back() { return (*this)[_size - 1]; }
    const T& back() const { return (*this)[_size - 1]; }
    T& front() { return (*this)[0]; }
    const T& front() const { return (*this)[0]; }

    T* begin() { return data(); }
    T* end() { return data() + _size; }
    const T* begin() const { return data(); }
    const T* end() const { return data() + _size; }

    size_type size() const noexcept { return _size; }
    bool empty() const noexcept { return _size == 0; }
    bool full() const noexcept { return _size == Capacity; }

  private:
    alignas(alignof(T)) byte _storage[sizeof(T) * Capacity] = {};
    size_type _size{0};
  };

  /// =========================================================================
  /// AsyncStopSource / AsyncStopToken — cooperative cancellation + interrupt
  /// =========================================================================
  /// Ref-counted shared state for stop/interrupt signaling.
  /// No std::shared_ptr — manual atomic ref counting.

  struct AsyncStopState {
    std::atomic<u32> refCount{1};
    std::atomic<u32> flags{0};  // bit 0 = stop, bit 1 = interrupt

    static constexpr u32 kStopBit = 1u;
    static constexpr u32 kInterruptBit = 2u;

    void addRef() noexcept { refCount.fetch_add(1, std::memory_order_relaxed); }
    void release() noexcept {
      if (refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) ::delete this;
    }

    bool stopRequested() const noexcept {
      return (flags.load(std::memory_order_acquire) & kStopBit) != 0;
    }
    bool interruptRequested() const noexcept {
      return (flags.load(std::memory_order_acquire) & kInterruptBit) != 0;
    }
    void requestStop() noexcept { flags.fetch_or(kStopBit, std::memory_order_release); }
    void requestInterrupt() noexcept { flags.fetch_or(kInterruptBit, std::memory_order_release); }
  };

  struct AsyncStopToken {
    AsyncStopToken() noexcept : _state{nullptr} {}
    explicit AsyncStopToken(AsyncStopState* s) noexcept : _state{s} {
      if (_state) _state->addRef();
    }
    AsyncStopToken(const AsyncStopToken& o) noexcept : _state{o._state} {
      if (_state) _state->addRef();
    }
    AsyncStopToken(AsyncStopToken&& o) noexcept : _state{zs::exchange(o._state, nullptr)} {}
    AsyncStopToken& operator=(const AsyncStopToken& o) noexcept {
      if (this != &o) {
        if (_state) _state->release();
        _state = o._state;
        if (_state) _state->addRef();
      }
      return *this;
    }
    AsyncStopToken& operator=(AsyncStopToken&& o) noexcept {
      if (this != &o) {
        if (_state) _state->release();
        _state = zs::exchange(o._state, nullptr);
      }
      return *this;
    }
    ~AsyncStopToken() {
      if (_state) _state->release();
    }

    bool stop_requested() const noexcept { return _state && _state->stopRequested(); }
    bool interrupt_requested() const noexcept { return _state && _state->interruptRequested(); }
    explicit operator bool() const noexcept { return _state != nullptr; }

  private:
    AsyncStopState* _state;
  };

  struct AsyncStopSource {
    AsyncStopSource() : _state{::new AsyncStopState{}} {}
    ~AsyncStopSource() {
      if (_state) _state->release();
    }

    AsyncStopSource(const AsyncStopSource&) = delete;
    AsyncStopSource& operator=(const AsyncStopSource&) = delete;
    AsyncStopSource(AsyncStopSource&& o) noexcept : _state{zs::exchange(o._state, nullptr)} {}
    AsyncStopSource& operator=(AsyncStopSource&& o) noexcept {
      if (this != &o) {
        if (_state) _state->release();
        _state = zs::exchange(o._state, nullptr);
      }
      return *this;
    }

    AsyncStopToken token() const noexcept { return AsyncStopToken{_state}; }
    void request_stop() noexcept {
      if (_state) _state->requestStop();
    }
    void request_interrupt() noexcept {
      if (_state) _state->requestInterrupt();
    }
    bool stop_requested() const noexcept { return _state && _state->stopRequested(); }

  private:
    AsyncStopState* _state;
  };

  /// =========================================================================
  /// AsyncPollStatus — result of a single step of an async routine
  /// =========================================================================
  enum class AsyncPollStatus : u8 { suspend = 0, completed, cancelled, failed };

  constexpr bool is_terminal(AsyncPollStatus s) noexcept {
    return s == AsyncPollStatus::completed || s == AsyncPollStatus::cancelled
           || s == AsyncPollStatus::failed;
  }

  /// =========================================================================
  /// AsyncRoutineBase<Derived> — stackless coroutine via macros (JIT-friendly)
  /// =========================================================================
  /// Usage:
  ///   struct MyRoutine : AsyncRoutineBase<MyRoutine> {
  ///     AsyncPollStatus operator()(AsyncExecutionContext& ctx) {
  ///       ZS_ASYNC_ROUTINE_BEGIN(this);
  ///       // first step
  ///       ZS_ASYNC_ROUTINE_SUSPEND(this);
  ///       // second step
  ///       ZS_ASYNC_ROUTINE_END(this);
  ///     }
  ///   };

  template <typename Derived> struct AsyncRoutineBase {
    i32 _routineState{0};

    void reset() noexcept { _routineState = 0; }
    bool done() const noexcept { return _routineState < 0; }
  };

// Switch-based stackless coroutine macros
// These produce a state machine from sequential code without C++20 coroutine support.
// Compatible with JIT compilation (no special ABI required).
#define ZS_ASYNC_ROUTINE_BEGIN(self)   \
  switch ((self)->_routineState) {     \
    case 0:

#define ZS_ASYNC_ROUTINE_SUSPEND(self)                     \
  do {                                                     \
    (self)->_routineState = __LINE__;                       \
    return ::zs::AsyncPollStatus::suspend;                 \
    case __LINE__:;                                        \
  } while (0)

#define ZS_ASYNC_ROUTINE_AWAIT(self, expr)                 \
  do {                                                     \
    (self)->_routineState = __LINE__;                       \
    case __LINE__:                                         \
      if (!(expr)) return ::zs::AsyncPollStatus::suspend;  \
  } while (0)

#define ZS_ASYNC_ROUTINE_CANCEL_IF_STOPPED(self, token)    \
  do {                                                     \
    if ((token).stop_requested()) {                        \
      (self)->_routineState = -1;                           \
      return ::zs::AsyncPollStatus::cancelled;             \
    }                                                      \
  } while (0)

#define ZS_ASYNC_ROUTINE_RETURN(self, status)              \
  do {                                                     \
    (self)->_routineState = -1;                             \
    return (status);                                       \
  } while (0)

#define ZS_ASYNC_ROUTINE_END(self)                         \
  }                                                        \
  (self)->_routineState = -1;                               \
  return ::zs::AsyncPollStatus::completed

}  // namespace zs
