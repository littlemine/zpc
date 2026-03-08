#pragma once

// C++20 coroutine support guard
#if defined(__has_include) && !__has_include(<coroutine>)
#  error "ZpcCoroutine.hpp requires C++20 coroutine support. Compile with /std:c++20 or -std=c++20."
#endif

#include <coroutine>

#include "zensim/ZpcImplPattern.hpp"
#include "zensim/ZpcMeta.hpp"

namespace zs {

  /// =========================================================================
  /// Promise base — continuation chain support
  /// =========================================================================

  struct PromiseBase {
    friend struct FinalAwaiter;
    struct FinalAwaiter {
      constexpr bool await_ready() const noexcept { return false; }
      template <typename P>
      std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) noexcept {
        h.promise()._inSuspension = true;
        if (auto c = h.promise()._continuation) return c;
        return std::noop_coroutine();
      }
      constexpr void await_resume() const noexcept {}
    };

    PromiseBase() noexcept = default;
    std::suspend_always initial_suspend() noexcept { return {}; }
    FinalAwaiter final_suspend() noexcept { return {}; }

    void set_continuation(std::coroutine_handle<> h) noexcept { _continuation = h; }

    bool _inSuspension{true};

  protected:
    std::coroutine_handle<> _continuation{};
  };

  /// =========================================================================
  /// Future<R> — RAII coroutine type, co_await-able with value return
  /// =========================================================================

  template <typename R> struct PromiseType;

  template <typename R = void> struct Future {
    using promise_type = PromiseType<R>;
    using CoroHandle = std::coroutine_handle<promise_type>;

    friend struct AwaiterBase;
    struct AwaiterBase {
      bool await_ready() noexcept {
        bool skip = !_handle || _handle.done();
        if (skip) _handle.promise()._inSuspension = false;
        return skip;
      }
      std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        _handle.promise().set_continuation(awaiting);
        _handle.promise()._inSuspension = false;
        return _handle;
      }
      explicit AwaiterBase(CoroHandle h) : _handle{h} {}

    protected:
      CoroHandle _handle;
    };

    Future(CoroHandle h = nullptr) noexcept : _handle{h} {}
    Future(Future&& o) noexcept : _handle{zs::exchange(o._handle, CoroHandle{})} {}
    Future& operator=(Future&& o) noexcept {
      if (this != &o) {
        if (_handle) _handle.destroy();
        _handle = zs::exchange(o._handle, CoroHandle{});
      }
      return *this;
    }
    ~Future() {
      if (_handle) _handle.destroy();
    }

    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;

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

    auto getHandle() const noexcept { return _handle; }

    void resume() const {
      _handle.promise()._inSuspension = false;
      _handle.resume();
    }

    bool isReady() const noexcept {
      return !_handle || (_handle.promise()._inSuspension && _handle.done());
    }
    bool isDone() const noexcept {
      return _handle && _handle.promise()._inSuspension && _handle.done();
    }
    bool isInProgress() const noexcept { return _handle && !_handle.promise()._inSuspension; }

    decltype(auto) get() & { return _handle.promise().get(); }
    decltype(auto) get() && { return zs::move(_handle.promise()).get(); }

  private:
    CoroHandle _handle;
  };

  /// Alias
  using Task = Future<>;

  /// =========================================================================
  /// PromiseType<R> — typed promise for value-returning coroutines
  /// =========================================================================

  template <typename R> struct PromiseType final : PromiseBase {
    PromiseType() noexcept = default;

    Future<R> get_return_object() noexcept {
      return std::coroutine_handle<PromiseType>::from_promise(*this);
    }

    void unhandled_exception() { _exception = std::current_exception(); }

    template <typename V, enable_if_t<is_convertible_v<V&&, R>> = 0>
    void return_value(V&& value) noexcept(is_nothrow_constructible_v<R, V&&>) {
      construct_at(&_value, zs::forward<V>(value));
      _hasValue = true;
    }

    R& get() & {
      if (_exception) std::rethrow_exception(_exception);
      return _value;
    }
    using rvalue_type = conditional_t<is_arithmetic_v<R> || is_pointer_v<R>, R, R&&>;
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

  /// void specialization
  template <> struct PromiseType<void> : PromiseBase {
    PromiseType() noexcept = default;

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

  /// reference specialization
  template <typename R> struct PromiseType<R&> final : PromiseBase {
    PromiseType() noexcept = default;

    Future<R&> get_return_object() noexcept {
      return std::coroutine_handle<PromiseType>::from_promise(*this);
    }
    void unhandled_exception() noexcept { _exception = std::current_exception(); }
    void return_value(R& value) noexcept { _result = addressof(value); }

    R& get() {
      if (_exception) std::rethrow_exception(_exception);
      return *_result;
    }

  private:
    R* _result{nullptr};
    std::exception_ptr _exception{};
  };

  /// =========================================================================
  /// Generator<T> — lazy pull-based coroutine sequence
  /// =========================================================================

  template <typename T> struct Generator {
    struct promise_type {
      T* _current{nullptr};

      Generator get_return_object() {
        return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
      }
      std::suspend_always initial_suspend() noexcept { return {}; }
      std::suspend_always final_suspend() noexcept { return {}; }
      void unhandled_exception() { _exception = std::current_exception(); }
      void return_void() noexcept {}

      std::suspend_always yield_value(T& value) noexcept {
        _current = addressof(value);
        return {};
      }
      std::suspend_always yield_value(T&& value) noexcept {
        _current = addressof(value);
        return {};
      }

      std::exception_ptr _exception{};
    };

    using CoroHandle = std::coroutine_handle<promise_type>;

    Generator(CoroHandle h) : _handle{h} {}
    Generator(Generator&& o) noexcept : _handle{zs::exchange(o._handle, CoroHandle{})} {}
    ~Generator() {
      if (_handle) _handle.destroy();
    }

    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    /// Advance to next value. Returns false when done.
    bool next() {
      if (!_handle || _handle.done()) return false;
      _handle.resume();
      if (_handle.promise()._exception) std::rethrow_exception(_handle.promise()._exception);
      return !_handle.done();
    }

    T& value() { return *_handle.promise()._current; }
    const T& value() const { return *_handle.promise()._current; }

    bool done() const noexcept { return !_handle || _handle.done(); }

    /// Range-based for loop support
    struct Sentinel {};
    struct Iterator {
      CoroHandle _handle;
      bool operator!=(Sentinel) const { return _handle && !_handle.done(); }
      Iterator& operator++() {
        _handle.resume();
        if (_handle.promise()._exception) std::rethrow_exception(_handle.promise()._exception);
        return *this;
      }
      T& operator*() { return *_handle.promise()._current; }
    };
    Iterator begin() {
      if (_handle) _handle.resume();
      return Iterator{_handle};
    }
    Sentinel end() { return {}; }

  private:
    CoroHandle _handle;
  };

  /// =========================================================================
  /// Awaitable traits
  /// =========================================================================

  namespace detail {
    template <typename T, typename = void> struct has_co_await_member : false_type {};
    template <typename T>
    struct has_co_await_member<T, void_t<decltype(declval<T>().operator co_await())>> : true_type {
    };

    template <typename T, typename = void> struct has_await_ready : false_type {};
    template <typename T>
    struct has_await_ready<T, void_t<decltype(declval<T>().await_ready())>> : true_type {};
  }  // namespace detail

  template <typename T>
  constexpr bool is_awaitable_v = detail::has_co_await_member<T>::value
                                  || detail::has_await_ready<T>::value;

}  // namespace zs
