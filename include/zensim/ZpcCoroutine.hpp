#pragma once

#if defined(__has_include) && !__has_include(<coroutine>)
#  error "ZpcCoroutine.hpp requires C++20 coroutine support."
#endif

#include <coroutine>
#include <exception>

#include "zensim/ZpcImplPattern.hpp"
#include "zensim/ZpcMeta.hpp"
#include "zensim/execution/Atomics.hpp"

namespace zs {

  struct PromiseBase {
    friend struct FinalAwaiter;

    struct FinalAwaiter {
      constexpr bool await_ready() const noexcept { return false; }

      template <typename P>
      std::coroutine_handle<> await_suspend(std::coroutine_handle<P> handle) noexcept {
        handle.promise()._inSuspension.store(true, memory_order_release);
        if (auto continuation = handle.promise()._continuation) return continuation;
        return std::noop_coroutine();
      }

      constexpr void await_resume() const noexcept {}
    };

    std::suspend_always initial_suspend() noexcept { return {}; }
    FinalAwaiter final_suspend() noexcept { return {}; }
    void set_continuation(std::coroutine_handle<> handle) noexcept { _continuation = handle; }

    Atomic<bool> _inSuspension{true};

  protected:
    std::coroutine_handle<> _continuation{};
  };

  template <typename R>
  struct PromiseType;

  template <typename R = void>
  struct Future {
    using promise_type = PromiseType<R>;
    using CoroHandle = std::coroutine_handle<promise_type>;

    struct AwaiterBase {
      explicit AwaiterBase(CoroHandle handle) : _handle{handle} {}

      bool await_ready() noexcept {
        const bool skip = !_handle || _handle.done();
        if (skip && _handle) _handle.promise()._inSuspension.store(false, memory_order_release);
        return skip;
      }

      std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        _handle.promise().set_continuation(awaiting);
        _handle.promise()._inSuspension.store(false, memory_order_release);
        return _handle;
      }

    protected:
      CoroHandle _handle;
    };

    Future(CoroHandle handle = nullptr) noexcept : _handle{handle} {}
    Future(Future &&other) noexcept : _handle{zs::exchange(other._handle, CoroHandle{})} {}
    Future &operator=(Future &&other) noexcept {
      if (this != &other) {
        if (_handle) _handle.destroy();
        _handle = zs::exchange(other._handle, CoroHandle{});
      }
      return *this;
    }

    ~Future() {
      if (_handle) _handle.destroy();
    }

    Future(const Future &) = delete;
    Future &operator=(const Future &) = delete;

    auto operator co_await() & noexcept {
      struct Awaiter : AwaiterBase {
        using AwaiterBase::AwaiterBase;
        decltype(auto) await_resume() { return this->_handle.promise().get(); }
      };
      return Awaiter{_handle};
    }

    auto operator co_await() && noexcept {
      struct Awaiter : AwaiterBase {
        using AwaiterBase::AwaiterBase;
        decltype(auto) await_resume() { return zs::move(this->_handle.promise()).get(); }
      };
      return Awaiter{_handle};
    }

    CoroHandle getHandle() const noexcept { return _handle; }

    void resume() const {
      _handle.promise()._inSuspension.store(false, memory_order_release);
      _handle.resume();
    }

    bool isReady() const noexcept {
      return !_handle
             || (_handle.promise()._inSuspension.load(memory_order_acquire) && _handle.done());
    }
    bool isDone() const noexcept {
      return _handle && _handle.promise()._inSuspension.load(memory_order_acquire)
             && _handle.done();
    }
    bool isInProgress() const noexcept {
      return _handle && !_handle.promise()._inSuspension.load(memory_order_acquire);
    }

    decltype(auto) get() & { return _handle.promise().get(); }
    decltype(auto) get() && { return zs::move(_handle.promise()).get(); }

  private:
    CoroHandle _handle;
  };

  using Task = Future<>;

  template <typename R>
  struct PromiseType final : PromiseBase {
    Future<R> get_return_object() noexcept {
      return std::coroutine_handle<PromiseType>::from_promise(*this);
    }

    void unhandled_exception() { _exception = std::current_exception(); }

    template <typename V, enable_if_t<is_convertible_v<V &&, R>> = 0>
    void return_value(V &&value) noexcept(is_nothrow_constructible_v<R, V &&>) {
      construct_at(&_value, zs::forward<V>(value));
      _hasValue = true;
    }

    R &get() & {
      if (_exception) std::rethrow_exception(_exception);
      return _value;
    }

    using rvalue_type = conditional_t<is_arithmetic_v<R> || is_pointer_v<R>, R, R &&>;

    rvalue_type get() && {
      if (_exception) std::rethrow_exception(zs::move(_exception));
      return static_cast<rvalue_type>(zs::move(_value));
    }

    ~PromiseType() {
      if (_hasValue) _value.~R();
    }

  private:
    union {
      R _value;
    };
    std::exception_ptr _exception{};
    bool _hasValue{false};
  };

  template <>
  struct PromiseType<void> : PromiseBase {
    Future<void> get_return_object() noexcept {
      return std::coroutine_handle<PromiseType>::from_promise(*this);
    }

    void unhandled_exception() noexcept { _exception = std::current_exception(); }
    void return_void() noexcept {}

    void get() {
      if (_exception) std::rethrow_exception(_exception);
    }

  private:
    std::exception_ptr _exception{};
  };

  template <typename R>
  struct PromiseType<R &> final : PromiseBase {
    Future<R &> get_return_object() noexcept {
      return std::coroutine_handle<PromiseType>::from_promise(*this);
    }

    void unhandled_exception() noexcept { _exception = std::current_exception(); }
    void return_value(R &value) noexcept { _result = addressof(value); }

    R &get() {
      if (_exception) std::rethrow_exception(_exception);
      return *_result;
    }

  private:
    R *_result{nullptr};
    std::exception_ptr _exception{};
  };

  template <typename T>
  struct Generator {
    struct promise_type {
      T *_current{nullptr};
      std::exception_ptr _exception{};

      Generator get_return_object() {
        return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
      }
      std::suspend_always initial_suspend() noexcept { return {}; }
      std::suspend_always final_suspend() noexcept { return {}; }
      void unhandled_exception() { _exception = std::current_exception(); }
      void return_void() noexcept {}

      std::suspend_always yield_value(T &value) noexcept {
        _current = addressof(value);
        return {};
      }

      std::suspend_always yield_value(T &&value) noexcept {
        _current = addressof(value);
        return {};
      }
    };

    using CoroHandle = std::coroutine_handle<promise_type>;

    explicit Generator(CoroHandle handle) : _handle{handle} {}
    Generator(Generator &&other) noexcept : _handle{zs::exchange(other._handle, CoroHandle{})} {}

    ~Generator() {
      if (_handle) _handle.destroy();
    }

    Generator(const Generator &) = delete;
    Generator &operator=(const Generator &) = delete;

    bool next() {
      if (!_handle || _handle.done()) return false;
      _handle.resume();
      if (_handle.promise()._exception) std::rethrow_exception(_handle.promise()._exception);
      return !_handle.done();
    }

    T &value() { return *_handle.promise()._current; }
    const T &value() const { return *_handle.promise()._current; }
    bool done() const noexcept { return !_handle || _handle.done(); }

    struct Sentinel {};

    struct Iterator {
      CoroHandle _handle;
      bool operator!=(Sentinel) const { return _handle && !_handle.done(); }
      Iterator &operator++() {
        _handle.resume();
        if (_handle.promise()._exception) std::rethrow_exception(_handle.promise()._exception);
        return *this;
      }
      T &operator*() { return *_handle.promise()._current; }
    };

    Iterator begin() {
      if (_handle) _handle.resume();
      return Iterator{_handle};
    }

    Sentinel end() { return {}; }

  private:
    CoroHandle _handle;
  };

  namespace detail {
    template <typename T, typename = void>
    struct has_co_await_member : false_type {};

    template <typename T>
    struct has_co_await_member<T, void_t<decltype(declval<T>().operator co_await())>> : true_type {
    };

    template <typename T, typename = void>
    struct has_await_ready : false_type {};

    template <typename T>
    struct has_await_ready<T, void_t<decltype(declval<T>().await_ready())>> : true_type {};
  }  // namespace detail

  template <typename T>
  constexpr bool is_awaitable_v = detail::has_co_await_member<T>::value
                                  || detail::has_await_ready<T>::value;

}  // namespace zs