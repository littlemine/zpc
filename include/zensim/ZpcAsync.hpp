#pragma once
<<<<<<< HEAD

#include "zensim/TypeAlias.hpp"
#include "zensim/execution/Atomics.hpp"
#include "zensim/ZpcImplPattern.hpp"

namespace zs {

  template <typename T, size_t Capacity> struct StaticVector {
    StaticVector() noexcept = default;
    ~StaticVector() noexcept { clear(); }

    StaticVector(const StaticVector &o) : _size{0} {
      for (size_t i = 0; i != o._size; ++i) emplace_back(o[i]);
    }
    StaticVector &operator=(const StaticVector &o) {
      if (this == &o) return *this;
      clear();
      for (size_t i = 0; i != o._size; ++i) emplace_back(o[i]);
      return *this;
    }

    template <typename... Args> T &emplace_back(Args &&...args) {
      if (_size >= Capacity) throw StaticException();
      auto *ptr = reinterpret_cast<T *>(_storage[_size].data<void>());
      zs::construct_at(ptr, FWD(args)...);
      ++_size;
      return *ptr;
    }

    void push_back(const T &value) { (void)emplace_back(value); }
    void push_back(T &&value) { (void)emplace_back(zs::move(value)); }

    void pop_back() noexcept {
      if (_size == 0) return;
      --_size;
      zs::destroy_at(reinterpret_cast<T *>(_storage[_size].data<void>()));
    }

    void clear() noexcept {
      while (_size) {
        --_size;
        zs::destroy_at(reinterpret_cast<T *>(_storage[_size].data<void>()));
      }
    }

    size_t size() const noexcept { return _size; }
    bool empty() const noexcept { return _size == 0; }
    bool full() const noexcept { return _size == Capacity; }

    T *data() noexcept { return reinterpret_cast<T *>(_storage[0].data<void>()); }
    const T *data() const noexcept {
      return reinterpret_cast<const T *>(_storage[0].data<const void>());
    }

    T &operator[](size_t i) noexcept { return *reinterpret_cast<T *>(_storage[i].data<void>()); }
    const T &operator[](size_t i) const noexcept {
      return *reinterpret_cast<const T *>(_storage[i].data<const void>());
    }
    T &front() noexcept { return (*this)[0]; }
    const T &front() const noexcept { return (*this)[0]; }
    T &back() noexcept { return (*this)[_size - 1]; }
    const T &back() const noexcept { return (*this)[_size - 1]; }

    T *begin() noexcept { return data(); }
    T *end() noexcept { return data() + _size; }
    const T *begin() const noexcept { return data(); }
    const T *end() const noexcept { return data() + _size; }

  private:
    InplaceStorage<sizeof(T), alignof(T)> _storage[Capacity]{};
    size_t _size{0};
  };

  struct AsyncStopState {
    Atomic<u32> refs{1};
    Atomic<u32> flags{0};
  };

  class AsyncStopToken {
  public:
    AsyncStopToken() noexcept = default;
    explicit AsyncStopToken(AsyncStopState *state) noexcept : _state{state} { retain(); }
    AsyncStopToken(const AsyncStopToken &o) noexcept : _state{o._state} { retain(); }
    AsyncStopToken &operator=(const AsyncStopToken &o) noexcept {
      if (this == &o) return *this;
      release();
      _state = o._state;
      retain();
      return *this;
    }
    AsyncStopToken(AsyncStopToken &&o) noexcept : _state{o._state} { o._state = nullptr; }
    AsyncStopToken &operator=(AsyncStopToken &&o) noexcept {
      if (this == &o) return *this;
      release();
      _state = o._state;
      o._state = nullptr;
      return *this;
    }
    ~AsyncStopToken() noexcept { release(); }

    bool stop_requested() const noexcept {
      return _state && ((_state->flags.load(memory_order_acquire) & 1u) != 0u);
    }
    bool interrupt_requested() const noexcept {
      return _state && ((_state->flags.load(memory_order_acquire) & 2u) != 0u);
    }

  private:
    void retain() noexcept {
      if (_state) _state->refs.fetch_add(1, memory_order_relaxed);
    }
    void release() noexcept {
      if (_state && _state->refs.fetch_sub(1, memory_order_acq_rel) == 1) delete _state;
      _state = nullptr;
    }

    AsyncStopState *_state{nullptr};

    friend class AsyncStopSource;
  };

  class AsyncStopSource {
  public:
    AsyncStopSource() : _state{new AsyncStopState{}} {}
    AsyncStopSource(const AsyncStopSource &) = delete;
    AsyncStopSource &operator=(const AsyncStopSource &) = delete;
    AsyncStopSource(AsyncStopSource &&o) noexcept : _state{o._state} { o._state = nullptr; }
    AsyncStopSource &operator=(AsyncStopSource &&o) noexcept {
      if (this == &o) return *this;
      reset();
      _state = o._state;
      o._state = nullptr;
      return *this;
    }
    ~AsyncStopSource() noexcept { reset(); }

    AsyncStopToken token() const noexcept { return AsyncStopToken{_state}; }
    void request_stop() noexcept {
      if (_state) {
        u32 expected = _state->flags.load(memory_order_relaxed);
        while (!_state->flags.compare_exchange_weak(expected, expected | 1u, memory_order_release,
                                                    memory_order_relaxed)) {
        }
      }
    }
    void request_interrupt() noexcept {
      if (_state) {
        u32 expected = _state->flags.load(memory_order_relaxed);
        while (!_state->flags.compare_exchange_weak(expected, expected | 2u, memory_order_release,
                                                    memory_order_relaxed)) {
        }
      }
    }
    bool stop_requested() const noexcept {
      return _state && ((_state->flags.load(memory_order_acquire) & 1u) != 0u);
    }

  private:
    void reset() noexcept {
      if (_state && _state->refs.fetch_sub(1, memory_order_acq_rel) == 1) delete _state;
      _state = nullptr;
    }

    AsyncStopState *_state{nullptr};
  };

  enum class AsyncPollStatus : u8 { suspend, completed, cancelled, failed };

  template <typename Derived> struct AsyncRoutineBase {
    void reset_routine() noexcept { _zsAsyncState = 0; }
    bool finished_routine() const noexcept { return _zsAsyncState < 0; }
    void reset() noexcept { reset_routine(); }
    bool done() const noexcept { return finished_routine(); }

  protected:
    int _zsAsyncState{0};
  };

#define ZS_ASYNC_ROUTINE_BEGIN(self) switch ((self)->_zsAsyncState) { case 0:
#define ZS_ASYNC_ROUTINE_SUSPEND(self)                                                   \
  do {                                                                                   \
    (self)->_zsAsyncState = __LINE__;                                                    \
    return ::zs::AsyncPollStatus::suspend;                                               \
    case __LINE__:;                                                                      \
  } while (false)
#define ZS_ASYNC_ROUTINE_AWAIT(self, expr)                                               \
  do {                                                                                   \
    (self)->_zsAsyncState = __LINE__;                                                    \
    case __LINE__:                                                                       \
    if (!(expr)) return ::zs::AsyncPollStatus::suspend;                                  \
  } while (false)
#define ZS_ASYNC_ROUTINE_CANCEL_IF_STOPPED(self, token)                                  \
  do {                                                                                   \
    if ((token).stop_requested() || (token).interrupt_requested()) {                     \
      (self)->_zsAsyncState = -1;                                                        \
      return ::zs::AsyncPollStatus::cancelled;                                           \
    }                                                                                    \
  } while (false)
#define ZS_ASYNC_ROUTINE_RETURN(self, status)                                            \
  do {                                                                                   \
    (self)->_zsAsyncState = -1;                                                          \
    return (status);                                                                     \
  } while (false)
#define ZS_ASYNC_ROUTINE_END(self)                                                       \
  }                                                                                      \
  (self)->_zsAsyncState = -1;                                                            \
  return ::zs::AsyncPollStatus::completed

}  // namespace zs
=======
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
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
