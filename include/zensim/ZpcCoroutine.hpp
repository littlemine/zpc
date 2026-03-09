#pragma once

<<<<<<< HEAD
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
=======
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
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)

  protected:
    std::coroutine_handle<> _continuation{};
  };

<<<<<<< HEAD
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
=======
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
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)

    protected:
      CoroHandle _handle;
    };

<<<<<<< HEAD
    Future(CoroHandle handle = nullptr) noexcept : _handle{handle} {}
    Future(Future &&other) noexcept : _handle{zs::exchange(other._handle, CoroHandle{})} {}
    Future &operator=(Future &&other) noexcept {
      if (this != &other) {
        if (_handle) _handle.destroy();
        _handle = zs::exchange(other._handle, CoroHandle{});
      }
      return *this;
    }

=======
    Future(CoroHandle h = nullptr) noexcept : _handle{h} {}
    Future(Future&& o) noexcept : _handle{zs::exchange(o._handle, CoroHandle{})} {}
    Future& operator=(Future&& o) noexcept {
      if (this != &o) {
        if (_handle) _handle.destroy();
        _handle = zs::exchange(o._handle, CoroHandle{});
      }
      return *this;
    }
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    ~Future() {
      if (_handle) _handle.destroy();
    }

<<<<<<< HEAD
    Future(const Future &) = delete;
    Future &operator=(const Future &) = delete;
=======
    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)

    auto operator co_await() & noexcept {
      struct Awaiter : AwaiterBase {
        using AwaiterBase::AwaiterBase;
        decltype(auto) await_resume() { return this->_handle.promise().get(); }
      };
      return Awaiter{_handle};
    }
<<<<<<< HEAD

=======
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    auto operator co_await() && noexcept {
      struct Awaiter : AwaiterBase {
        using AwaiterBase::AwaiterBase;
        decltype(auto) await_resume() { return zs::move(this->_handle.promise()).get(); }
      };
      return Awaiter{_handle};
    }

<<<<<<< HEAD
    CoroHandle getHandle() const noexcept { return _handle; }

    void resume() const {
      _handle.promise()._inSuspension.store(false, memory_order_release);
=======
    auto getHandle() const noexcept { return _handle; }

    void resume() const {
      _handle.promise()._inSuspension = false;
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
      _handle.resume();
    }

    bool isReady() const noexcept {
<<<<<<< HEAD
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
=======
      return !_handle || (_handle.promise()._inSuspension && _handle.done());
    }
    bool isDone() const noexcept {
      return _handle && _handle.promise()._inSuspension && _handle.done();
    }
    bool isInProgress() const noexcept { return _handle && !_handle.promise()._inSuspension; }
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)

    decltype(auto) get() & { return _handle.promise().get(); }
    decltype(auto) get() && { return zs::move(_handle.promise()).get(); }

  private:
    CoroHandle _handle;
  };

<<<<<<< HEAD
  using Task = Future<>;

  template <typename R>
  struct PromiseType final : PromiseBase {
=======
  /// Alias
  using Task = Future<>;

  /// =========================================================================
  /// PromiseType<R> — typed promise for value-returning coroutines
  /// =========================================================================

  template <typename R> struct PromiseType final : PromiseBase {
    PromiseType() noexcept = default;

>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    Future<R> get_return_object() noexcept {
      return std::coroutine_handle<PromiseType>::from_promise(*this);
    }

    void unhandled_exception() { _exception = std::current_exception(); }

<<<<<<< HEAD
    template <typename V, enable_if_t<is_convertible_v<V &&, R>> = 0>
    void return_value(V &&value) noexcept(is_nothrow_constructible_v<R, V &&>) {
=======
    template <typename V, enable_if_t<is_convertible_v<V&&, R>> = 0>
    void return_value(V&& value) noexcept(is_nothrow_constructible_v<R, V&&>) {
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
      construct_at(&_value, zs::forward<V>(value));
      _hasValue = true;
    }

<<<<<<< HEAD
    R &get() & {
      if (_exception) std::rethrow_exception(_exception);
      return _value;
    }

    using rvalue_type = conditional_t<is_arithmetic_v<R> || is_pointer_v<R>, R, R &&>;

=======
    R& get() & {
      if (_exception) std::rethrow_exception(_exception);
      return _value;
    }
    using rvalue_type = conditional_t<is_arithmetic_v<R> || is_pointer_v<R>, R, R&&>;
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
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

<<<<<<< HEAD
  template <>
  struct PromiseType<void> : PromiseBase {
    Future<void> get_return_object() noexcept {
      return std::coroutine_handle<PromiseType>::from_promise(*this);
    }

=======
  /// void specialization
  template <> struct PromiseType<void> : PromiseBase {
    PromiseType() noexcept = default;

    Future<void> get_return_object() noexcept {
      return std::coroutine_handle<PromiseType>::from_promise(*this);
    }
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    void unhandled_exception() noexcept { _exception = std::current_exception(); }
    void return_void() noexcept {}

    void get() {
      if (_exception) std::rethrow_exception(_exception);
    }

  private:
    std::exception_ptr _exception{};
  };

<<<<<<< HEAD
  template <typename R>
  struct PromiseType<R &> final : PromiseBase {
    Future<R &> get_return_object() noexcept {
      return std::coroutine_handle<PromiseType>::from_promise(*this);
    }

    void unhandled_exception() noexcept { _exception = std::current_exception(); }
    void return_value(R &value) noexcept { _result = addressof(value); }

    R &get() {
=======
  /// reference specialization
  template <typename R> struct PromiseType<R&> final : PromiseBase {
    PromiseType() noexcept = default;

    Future<R&> get_return_object() noexcept {
      return std::coroutine_handle<PromiseType>::from_promise(*this);
    }
    void unhandled_exception() noexcept { _exception = std::current_exception(); }
    void return_value(R& value) noexcept { _result = addressof(value); }

    R& get() {
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
      if (_exception) std::rethrow_exception(_exception);
      return *_result;
    }

  private:
<<<<<<< HEAD
    R *_result{nullptr};
    std::exception_ptr _exception{};
  };

  template <typename T>
  struct Generator {
    struct promise_type {
      T *_current{nullptr};
      std::exception_ptr _exception{};
=======
    R* _result{nullptr};
    std::exception_ptr _exception{};
  };

  /// =========================================================================
  /// Generator<T> — lazy pull-based coroutine sequence
  /// =========================================================================

  template <typename T> struct Generator {
    struct promise_type {
      T* _current{nullptr};
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)

      Generator get_return_object() {
        return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
      }
      std::suspend_always initial_suspend() noexcept { return {}; }
      std::suspend_always final_suspend() noexcept { return {}; }
      void unhandled_exception() { _exception = std::current_exception(); }
      void return_void() noexcept {}

<<<<<<< HEAD
      std::suspend_always yield_value(T &value) noexcept {
=======
      std::suspend_always yield_value(T& value) noexcept {
        _current = addressof(value);
        return {};
      }
      std::suspend_always yield_value(T&& value) noexcept {
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
        _current = addressof(value);
        return {};
      }

<<<<<<< HEAD
      std::suspend_always yield_value(T &&value) noexcept {
        _current = addressof(value);
        return {};
      }
=======
      std::exception_ptr _exception{};
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    };

    using CoroHandle = std::coroutine_handle<promise_type>;

<<<<<<< HEAD
    explicit Generator(CoroHandle handle) : _handle{handle} {}
    Generator(Generator &&other) noexcept : _handle{zs::exchange(other._handle, CoroHandle{})} {}

=======
    Generator(CoroHandle h) : _handle{h} {}
    Generator(Generator&& o) noexcept : _handle{zs::exchange(o._handle, CoroHandle{})} {}
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    ~Generator() {
      if (_handle) _handle.destroy();
    }

<<<<<<< HEAD
    Generator(const Generator &) = delete;
    Generator &operator=(const Generator &) = delete;

=======
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    /// Advance to next value. Returns false when done.
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    bool next() {
      if (!_handle || _handle.done()) return false;
      _handle.resume();
      if (_handle.promise()._exception) std::rethrow_exception(_handle.promise()._exception);
      return !_handle.done();
    }

<<<<<<< HEAD
    T &value() { return *_handle.promise()._current; }
    const T &value() const { return *_handle.promise()._current; }
    bool done() const noexcept { return !_handle || _handle.done(); }

    struct Sentinel {};

    struct Iterator {
      CoroHandle _handle;
      bool operator!=(Sentinel) const { return _handle && !_handle.done(); }
      Iterator &operator++() {
=======
    T& value() { return *_handle.promise()._current; }
    const T& value() const { return *_handle.promise()._current; }

    bool done() const noexcept { return !_handle || _handle.done(); }

    /// Range-based for loop support
    struct Sentinel {};
    struct Iterator {
      CoroHandle _handle;
      bool operator!=(Sentinel) const { return _handle && !_handle.done(); }
      Iterator& operator++() {
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
        _handle.resume();
        if (_handle.promise()._exception) std::rethrow_exception(_handle.promise()._exception);
        return *this;
      }
<<<<<<< HEAD
      T &operator*() { return *_handle.promise()._current; }
    };

=======
      T& operator*() { return *_handle.promise()._current; }
    };
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    Iterator begin() {
      if (_handle) _handle.resume();
      return Iterator{_handle};
    }
<<<<<<< HEAD

=======
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    Sentinel end() { return {}; }

  private:
    CoroHandle _handle;
  };

<<<<<<< HEAD
  namespace detail {
    template <typename T, typename = void>
    struct has_co_await_member : false_type {};

=======
  /// =========================================================================
  /// Awaitable traits
  /// =========================================================================

  namespace detail {
    template <typename T, typename = void> struct has_co_await_member : false_type {};
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    template <typename T>
    struct has_co_await_member<T, void_t<decltype(declval<T>().operator co_await())>> : true_type {
    };

<<<<<<< HEAD
    template <typename T, typename = void>
    struct has_await_ready : false_type {};

=======
    template <typename T, typename = void> struct has_await_ready : false_type {};
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    template <typename T>
    struct has_await_ready<T, void_t<decltype(declval<T>().await_ready())>> : true_type {};
  }  // namespace detail

  template <typename T>
  constexpr bool is_awaitable_v = detail::has_co_await_member<T>::value
                                  || detail::has_await_ready<T>::value;

<<<<<<< HEAD
}  // namespace zs
=======
}  // namespace zs
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
