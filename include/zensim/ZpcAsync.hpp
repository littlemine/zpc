#pragma once

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