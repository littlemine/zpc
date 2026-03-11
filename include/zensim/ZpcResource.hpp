#pragma once

#include <atomic>

#include "zensim/ZpcImplPattern.hpp"
#include "zensim/ZpcTuple.hpp"

namespace zs {

  /// strongly-typed handle
  template <typename Id, typename TypeName> struct Handle {
    static_assert(is_integral_v<Id>, "Id should be an integral type for the moment.");
    using value_type = Id;

    static constexpr auto type_name() { return get_type_str<TypeName>(); }

    constexpr value_type get() const noexcept { return id; }
    explicit constexpr operator value_type() const noexcept { return id; }

    constexpr bool operator==(const Handle& o) const noexcept { return id == o.get(); }

    Id id;
  };

  /// unique ptr
  /// @ref: microsoft/STL
  template <typename T> struct DefaultDelete {
    constexpr DefaultDelete() noexcept = default;
    template <typename TT, enable_if_t<is_convertible_v<TT*, T*>> = 0>
    constexpr DefaultDelete(const DefaultDelete<TT>&) noexcept {}
    constexpr void operator()(T* p) { delete p; };
  };
  template <typename T> struct DefaultDelete<T[]> {
    constexpr DefaultDelete() noexcept = default;
    template <typename TT, enable_if_t<is_convertible_v<TT (*)[], T (*)[]>> = 0>
    constexpr DefaultDelete(const DefaultDelete<TT[]>&) noexcept {}
    constexpr void operator()(T* p) { delete[] p; };
  };
  template <typename T, typename D = DefaultDelete<T>> struct UniquePtr {
  public:
    using pointer = T*;
    using element_type = T;
    using deleter_type = D;

    template <typename DD = D, enable_if_all<!is_pointer_v<DD>, is_default_constructible_v<DD>> = 0>
    constexpr UniquePtr() noexcept : _storage{} {}

    /// nullptr
    template <typename DD = D, enable_if_all<!is_pointer_v<DD>, is_default_constructible_v<DD>> = 0>
    constexpr UniquePtr(decltype(nullptr)) noexcept : _storage{} {}

    constexpr UniquePtr& operator=(decltype(nullptr)) noexcept {
      reset();
      return *this;
    }

    ///
    template <typename DD = D,
              enable_if_all<!is_pointer_v<DD>, !is_reference_v<DD>, is_default_constructible_v<DD>>
              = 0>
    constexpr explicit UniquePtr(pointer p) noexcept {
      _storage.template get<1>() = p;
    }

    template <typename DD = D, enable_if_t<is_constructible_v<DD, const DD&>> = 0>
    constexpr UniquePtr(pointer p, const D& d) noexcept : _storage{d, p} {}

    template <typename DD = D, enable_if_all<!is_reference_v<DD>, is_constructible_v<DD, DD>> = 0>
    constexpr UniquePtr(pointer p, D&& d) noexcept : _storage{zs::move(d), p} {}

    template <typename DD = D,
              enable_if_all<is_reference_v<DD>, is_constructible_v<DD, remove_reference_t<DD>>> = 0>
    UniquePtr(pointer, remove_reference_t<D>&&) = delete;

    ///
    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    ///
    template <typename DD = D, enable_if_t<is_move_constructible_v<DD>> = 0>
    constexpr UniquePtr(UniquePtr&& o) noexcept
        : _storage(zs::forward<D>(o.get_deleter()), o.release()) {}
    template <typename DD = D, enable_if_t<is_move_assignable_v<DD>> = 0>
    constexpr UniquePtr& operator=(UniquePtr&& o) noexcept {
      reset(o.release());
      _storage.template get<0>() = zs::forward<D>(o._storage.template get<0>());
      return *this;
    }

    ///
    template <typename TT, typename DD,
              enable_if_all<!is_array_v<TT>,
                            is_convertible_v<typename UniquePtr<TT, DD>::pointer, pointer>,
                            (is_reference_v<D> ? is_same_v<DD, D> : is_convertible_v<DD, D>)>
              = 0>
    constexpr UniquePtr(UniquePtr<TT, DD>&& o) noexcept
        : _storage{zs::forward<DD>(o.get_deleter()), o.release()} {}
    template <typename TT, typename DD,
              enable_if_all<!is_array_v<TT>,
                            is_convertible_v<typename UniquePtr<TT, DD>::pointer, pointer>,
                            is_assignable_v<D&, DD>>
              = 0>
    constexpr UniquePtr& operator=(UniquePtr<TT, DD>&& o) noexcept {
      reset(o.release());
      _storage.template get<0>() = zs::forward<DD>(o._storage.template get<0>());
      return *this;
    }

    constexpr void swap(UniquePtr& o) noexcept {
      zs_swap(_storage.template get<0>(), o._storage.template get<0>());
      zs_swap(_storage.template get<1>(), o._storage.template get<1>());
    }

    ~UniquePtr() noexcept {
      if (auto p = _storage.template get<1>(); p) {
        _storage.template get<0>()(p);
        _storage.template get<1>() = nullptr;
      }
    }

    [[nodiscard]] constexpr D& get_deleter() noexcept { return _storage.template get<0>(); }

    [[nodiscard]] constexpr const D& get_deleter() const noexcept {
      return _storage.template get<0>();
    }

    [[nodiscard]] constexpr add_lvalue_reference_t<T> operator*() const
        noexcept(noexcept(*zs::declval<pointer>())) {
      return *_storage.template get<1>();
    }

    [[nodiscard]] constexpr pointer operator->() const noexcept {
      return _storage.template get<1>();
    }

    [[nodiscard]] constexpr pointer get() const noexcept { return _storage.template get<1>(); }

    constexpr explicit operator bool() const noexcept {
      return static_cast<bool>(_storage.template get<1>());
    }

    constexpr pointer release() noexcept {
      return zs::exchange(_storage.template get<1>(), nullptr);
    }

    constexpr void reset(pointer p = nullptr) noexcept {
      pointer old = zs::exchange(_storage.template get<1>(), p);
      if (old) _storage.template get<0>()(old);
    }

    tuple<D, pointer> _storage;
  };

  template <typename T, typename D> struct UniquePtr<T[], D> {
  public:
    using pointer = T*;
    using element_type = T;
    using deleter_type = D;

    template <typename DD = D, enable_if_all<!is_pointer_v<DD>, is_default_constructible_v<DD>> = 0>
    constexpr UniquePtr() noexcept : _storage{} {}

    template <typename DD = D, enable_if_all<!is_pointer_v<DD>, is_default_constructible_v<DD>> = 0>
    constexpr UniquePtr(decltype(nullptr)) noexcept : _storage{} {}

    constexpr UniquePtr& operator=(decltype(nullptr)) noexcept {
      reset();
      return *this;
    }

    template <typename DD = D,
              enable_if_all<!is_pointer_v<DD>, !is_reference_v<DD>, is_default_constructible_v<DD>>
              = 0>
    constexpr explicit UniquePtr(pointer p) noexcept {
      _storage.template get<1>() = p;
    }

    template <typename DD = D, enable_if_t<is_constructible_v<DD, const DD&>> = 0>
    constexpr UniquePtr(pointer p, const D& d) noexcept : _storage{d, p} {}

    template <typename DD = D, enable_if_all<!is_reference_v<DD>, is_constructible_v<DD, DD>> = 0>
    constexpr UniquePtr(pointer p, D&& d) noexcept : _storage{zs::move(d), p} {}

    template <typename DD = D,
              enable_if_all<is_reference_v<DD>, is_constructible_v<DD, remove_reference_t<DD>>> = 0>
    UniquePtr(pointer, remove_reference_t<D>&&) = delete;

    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    template <typename DD = D, enable_if_t<is_move_constructible_v<DD>> = 0>
    constexpr UniquePtr(UniquePtr&& o) noexcept
        : _storage(zs::forward<D>(o.get_deleter()), o.release()) {}

    template <typename DD = D, enable_if_t<is_move_assignable_v<DD>> = 0>
    constexpr UniquePtr& operator=(UniquePtr&& o) noexcept {
      reset(o.release());
      _storage.template get<0>() = zs::forward<D>(o._storage.template get<0>());
      return *this;
    }

    template <typename TT, typename DD,
              enable_if_all<is_array_v<TT>, is_convertible_v<typename UniquePtr<TT, DD>::pointer, pointer>,
                            (is_reference_v<D> ? is_same_v<DD, D> : is_convertible_v<DD, D>)>
              = 0>
    constexpr UniquePtr(UniquePtr<TT, DD>&& o) noexcept
        : _storage{zs::forward<DD>(o.get_deleter()), o.release()} {}

    template <typename TT, typename DD,
              enable_if_all<is_array_v<TT>, is_convertible_v<typename UniquePtr<TT, DD>::pointer, pointer>,
                            is_assignable_v<D&, DD>>
              = 0>
    constexpr UniquePtr& operator=(UniquePtr<TT, DD>&& o) noexcept {
      reset(o.release());
      _storage.template get<0>() = zs::forward<DD>(o._storage.template get<0>());
      return *this;
    }

    constexpr void swap(UniquePtr& o) noexcept {
      zs_swap(_storage.template get<0>(), o._storage.template get<0>());
      zs_swap(_storage.template get<1>(), o._storage.template get<1>());
    }

    ~UniquePtr() noexcept {
      if (auto p = _storage.template get<1>(); p) {
        _storage.template get<0>()(p);
        _storage.template get<1>() = nullptr;
      }
    }

    [[nodiscard]] constexpr D& get_deleter() noexcept { return _storage.template get<0>(); }
    [[nodiscard]] constexpr const D& get_deleter() const noexcept {
      return _storage.template get<0>();
    }

    [[nodiscard]] constexpr T& operator[](size_t i) const noexcept { return _storage.template get<1>()[i]; }
    [[nodiscard]] constexpr pointer get() const noexcept { return _storage.template get<1>(); }
    constexpr explicit operator bool() const noexcept {
      return static_cast<bool>(_storage.template get<1>());
    }

    constexpr pointer release() noexcept {
      return zs::exchange(_storage.template get<1>(), nullptr);
    }

    constexpr void reset(pointer p = nullptr) noexcept {
      pointer old = zs::exchange(_storage.template get<1>(), p);
      if (old) _storage.template get<0>()(old);
    }

    tuple<D, pointer> _storage;
  };

  // UniquePtr comparison operators
  template <typename T, typename D>
  constexpr bool operator==(const UniquePtr<T, D>& a, decltype(nullptr)) noexcept {
    return !a;
  }
  template <typename T, typename D>
  constexpr bool operator==(decltype(nullptr), const UniquePtr<T, D>& a) noexcept {
    return !a;
  }
  template <typename T, typename D>
  constexpr bool operator!=(const UniquePtr<T, D>& a, decltype(nullptr)) noexcept {
    return static_cast<bool>(a);
  }
  template <typename T, typename D>
  constexpr bool operator!=(decltype(nullptr), const UniquePtr<T, D>& a) noexcept {
    return static_cast<bool>(a);
  }
  template <typename T1, typename D1, typename T2, typename D2>
  constexpr bool operator==(const UniquePtr<T1, D1>& a, const UniquePtr<T2, D2>& b) noexcept {
    return a.get() == b.get();
  }
  template <typename T1, typename D1, typename T2, typename D2>
  constexpr bool operator!=(const UniquePtr<T1, D1>& a, const UniquePtr<T2, D2>& b) noexcept {
    return a.get() != b.get();
  }

  namespace detail {
    template <typename From, typename To>
    struct shared_ptr_compatible : bool_constant<is_convertible_v<From*, To*>> {};

    template <typename From, typename To>
    struct shared_ptr_compatible<From[], To[]> : bool_constant<is_convertible_v<From (*)[], To (*)[]>> {};

    struct SharedControlBlockBase {
      std::atomic_size_t strong{1};
      std::atomic_size_t weak{1};

      void add_strong() noexcept { strong.fetch_add(1, std::memory_order_relaxed); }

      bool try_add_strong() noexcept {
        auto count = strong.load(std::memory_order_acquire);
        while (count != 0) {
          if (strong.compare_exchange_weak(count, count + 1, std::memory_order_acq_rel,
                                           std::memory_order_acquire))
            return true;
        }
        return false;
      }

      void release_strong() noexcept {
        if (strong.fetch_sub(1, std::memory_order_acq_rel) == 1) {
          destroy_object();
          release_weak();
        }
      }

      void add_weak() noexcept { weak.fetch_add(1, std::memory_order_relaxed); }

      void release_weak() noexcept {
        if (weak.fetch_sub(1, std::memory_order_acq_rel) == 1) delete_self();
      }

      size_t strong_count() const noexcept { return strong.load(std::memory_order_acquire); }

      virtual void destroy_object() noexcept = 0;
      virtual void delete_self() noexcept = 0;

    protected:
      ~SharedControlBlockBase() = default;
    };

    template <typename Pointer, typename Deleter>
    struct SharedControlBlock final : SharedControlBlockBase {
      Pointer ptr;
      Deleter deleter;

      SharedControlBlock(Pointer p, Deleter d) : ptr{p}, deleter{zs::move(d)} {}

      void destroy_object() noexcept override {
        if (ptr) {
          deleter(ptr);
          ptr = nullptr;
        }
      }

      void delete_self() noexcept override { delete this; }
    };
  }  // namespace detail

  template <typename T>
  class WeakPtr;

  template <typename T>
  class SharedPtr {
  public:
    using pointer = T*;
    using element_type = T;

    constexpr SharedPtr() noexcept = default;
    constexpr SharedPtr(decltype(nullptr)) noexcept {}

    template <typename Deleter = DefaultDelete<T>, enable_if_t<is_invocable_v<Deleter&, pointer>> = 0>
    explicit SharedPtr(pointer p) : SharedPtr{p, Deleter{}} {}

    template <typename Deleter, enable_if_t<is_invocable_v<Deleter&, pointer>> = 0>
    SharedPtr(pointer p, Deleter deleter) {
      reset_(p, zs::move(deleter));
    }

    SharedPtr(const SharedPtr& o) noexcept : _ptr{o._ptr}, _control{o._control} {
      if (_control) _control->add_strong();
    }

    template <typename U, enable_if_t<is_convertible_v<U*, pointer>> = 0>
    SharedPtr(const SharedPtr<U>& o) noexcept : _ptr{o._ptr}, _control{o._control} {
      if (_control) _control->add_strong();
    }

    SharedPtr(SharedPtr&& o) noexcept : _ptr{zs::exchange(o._ptr, nullptr)},
                                        _control{zs::exchange(o._control, nullptr)} {}

    template <typename U, enable_if_t<is_convertible_v<U*, pointer>> = 0>
    SharedPtr(SharedPtr<U>&& o) noexcept : _ptr{zs::exchange(o._ptr, nullptr)},
                                           _control{zs::exchange(o._control, nullptr)} {}

    SharedPtr(const WeakPtr<T>& weak) noexcept : SharedPtr{} {
      if (weak._control && weak._control->try_add_strong()) {
        _ptr = weak._ptr;
        _control = weak._control;
      }
    }

    template <typename U, enable_if_t<is_convertible_v<U*, pointer>> = 0>
    SharedPtr(const WeakPtr<U>& weak) noexcept : SharedPtr{} {
      if (weak._control && weak._control->try_add_strong()) {
        _ptr = weak._ptr;
        _control = weak._control;
      }
    }

    ~SharedPtr() noexcept { release_(); }

    SharedPtr& operator=(const SharedPtr& o) noexcept {
      if (this != &o) {
        SharedPtr tmp{o};
        swap(tmp);
      }
      return *this;
    }

    template <typename U, enable_if_t<is_convertible_v<U*, pointer>> = 0>
    SharedPtr& operator=(const SharedPtr<U>& o) noexcept {
      SharedPtr tmp{o};
      swap(tmp);
      return *this;
    }

    SharedPtr& operator=(SharedPtr&& o) noexcept {
      if (this != &o) {
        release_();
        _ptr = zs::exchange(o._ptr, nullptr);
        _control = zs::exchange(o._control, nullptr);
      }
      return *this;
    }

    template <typename U, enable_if_t<is_convertible_v<U*, pointer>> = 0>
    SharedPtr& operator=(SharedPtr<U>&& o) noexcept {
      release_();
      _ptr = zs::exchange(o._ptr, nullptr);
      _control = zs::exchange(o._control, nullptr);
      return *this;
    }

    void reset() noexcept { release_(); }

    template <typename Deleter = DefaultDelete<T>, enable_if_t<is_invocable_v<Deleter&, pointer>> = 0>
    void reset(pointer p) {
      SharedPtr tmp{p, Deleter{}};
      swap(tmp);
    }

    template <typename Deleter, enable_if_t<is_invocable_v<Deleter&, pointer>> = 0>
    void reset(pointer p, Deleter deleter) {
      SharedPtr tmp{p, zs::move(deleter)};
      swap(tmp);
    }

    void swap(SharedPtr& o) noexcept {
      zs_swap(_ptr, o._ptr);
      zs_swap(_control, o._control);
    }

    [[nodiscard]] pointer get() const noexcept { return _ptr; }
    [[nodiscard]] size_t use_count() const noexcept {
      return _control ? _control->strong_count() : 0;
    }
    [[nodiscard]] bool unique() const noexcept { return use_count() == 1; }
    [[nodiscard]] explicit operator bool() const noexcept { return _ptr != nullptr; }

    template <typename TT = T, enable_if_t<!is_void_v<TT>> = 0>
    [[nodiscard]] add_lvalue_reference_t<T> operator*() const noexcept(noexcept(*zs::declval<pointer>())) {
      return *_ptr;
    }

    template <typename TT = T, enable_if_t<!is_void_v<TT>> = 0>
    [[nodiscard]] pointer operator->() const noexcept {
      return _ptr;
    }

  private:
    template <typename U>
    friend class SharedPtr;
    template <typename U>
    friend class WeakPtr;

    template <typename To, typename From>
    friend SharedPtr<To> static_pointer_cast(const SharedPtr<From>&) noexcept;
    template <typename To, typename From>
    friend SharedPtr<To> static_pointer_cast(SharedPtr<From>&&) noexcept;
    template <typename To, typename From>
    friend SharedPtr<To> dynamic_pointer_cast(const SharedPtr<From>&) noexcept;
    template <typename To, typename From>
    friend SharedPtr<To> dynamic_pointer_cast(SharedPtr<From>&&) noexcept;
    template <typename To, typename From>
    friend SharedPtr<To> const_pointer_cast(const SharedPtr<From>&) noexcept;

    SharedPtr(pointer p, detail::SharedControlBlockBase* control, int) noexcept
        : _ptr{p}, _control{control} {}

    template <typename Deleter>
    void reset_(pointer p, Deleter deleter) {
      if (!p) return;
      using block_t = detail::SharedControlBlock<pointer, Deleter>;
      try {
        _control = new block_t{p, zs::move(deleter)};
        _ptr = p;
      } catch (...) {
        deleter(p);
        throw;
      }
    }

    void release_() noexcept {
      if (_control) {
        _control->release_strong();
        _control = nullptr;
        _ptr = nullptr;
      }
    }

    pointer _ptr{nullptr};
    detail::SharedControlBlockBase* _control{nullptr};
  };

  template <typename T>
  class SharedPtr<T[]> {
  public:
    using element_type = T;
    using pointer = T*;

    constexpr SharedPtr() noexcept = default;
    constexpr SharedPtr(decltype(nullptr)) noexcept {}

    template <typename Deleter = DefaultDelete<T[]>, enable_if_t<is_invocable_v<Deleter&, pointer>> = 0>
    explicit SharedPtr(pointer p) : SharedPtr{p, Deleter{}} {}

    template <typename Deleter, enable_if_t<is_invocable_v<Deleter&, pointer>> = 0>
    SharedPtr(pointer p, Deleter deleter) {
      reset_(p, zs::move(deleter));
    }

    SharedPtr(const SharedPtr& o) noexcept : _ptr{o._ptr}, _control{o._control} {
      if (_control) _control->add_strong();
    }

    template <typename U, enable_if_t<is_convertible_v<U (*)[], T (*)[]>> = 0>
    SharedPtr(const SharedPtr<U[]>& o) noexcept : _ptr{o._ptr}, _control{o._control} {
      if (_control) _control->add_strong();
    }

    SharedPtr(SharedPtr&& o) noexcept : _ptr{zs::exchange(o._ptr, nullptr)},
                                        _control{zs::exchange(o._control, nullptr)} {}

    template <typename U, enable_if_t<is_convertible_v<U (*)[], T (*)[]>> = 0>
    SharedPtr(SharedPtr<U[]>&& o) noexcept : _ptr{zs::exchange(o._ptr, nullptr)},
                                             _control{zs::exchange(o._control, nullptr)} {}

    ~SharedPtr() noexcept { release_(); }

    SharedPtr& operator=(const SharedPtr& o) noexcept {
      if (this != &o) {
        SharedPtr tmp{o};
        swap(tmp);
      }
      return *this;
    }

    SharedPtr& operator=(SharedPtr&& o) noexcept {
      if (this != &o) {
        release_();
        _ptr = zs::exchange(o._ptr, nullptr);
        _control = zs::exchange(o._control, nullptr);
      }
      return *this;
    }

    void reset() noexcept { release_(); }

    template <typename Deleter = DefaultDelete<T[]>, enable_if_t<is_invocable_v<Deleter&, pointer>> = 0>
    void reset(pointer p) {
      SharedPtr tmp{p, Deleter{}};
      swap(tmp);
    }

    template <typename Deleter, enable_if_t<is_invocable_v<Deleter&, pointer>> = 0>
    void reset(pointer p, Deleter deleter) {
      SharedPtr tmp{p, zs::move(deleter)};
      swap(tmp);
    }

    void swap(SharedPtr& o) noexcept {
      zs_swap(_ptr, o._ptr);
      zs_swap(_control, o._control);
    }

    [[nodiscard]] pointer get() const noexcept { return _ptr; }
    [[nodiscard]] size_t use_count() const noexcept {
      return _control ? _control->strong_count() : 0;
    }
    [[nodiscard]] bool unique() const noexcept { return use_count() == 1; }
    [[nodiscard]] explicit operator bool() const noexcept { return _ptr != nullptr; }
    [[nodiscard]] T& operator[](size_t i) const noexcept { return _ptr[i]; }

  private:
    template <typename U>
    friend class SharedPtr;
    template <typename U>
    friend class WeakPtr;

    template <typename Deleter>
    void reset_(pointer p, Deleter deleter) {
      if (!p) return;
      using block_t = detail::SharedControlBlock<pointer, Deleter>;
      try {
        _control = new block_t{p, zs::move(deleter)};
        _ptr = p;
      } catch (...) {
        deleter(p);
        throw;
      }
    }

    void release_() noexcept {
      if (_control) {
        _control->release_strong();
        _control = nullptr;
        _ptr = nullptr;
      }
    }

    pointer _ptr{nullptr};
    detail::SharedControlBlockBase* _control{nullptr};
  };

  template <typename T>
  class WeakPtr {
  public:
    using pointer = conditional_t<is_array_v<T>, remove_extent_t<T>*, T*>;

    constexpr WeakPtr() noexcept = default;
    constexpr WeakPtr(decltype(nullptr)) noexcept {}

    WeakPtr(const WeakPtr& o) noexcept : _ptr{o._ptr}, _control{o._control} {
      if (_control) _control->add_weak();
    }

    template <typename U, enable_if_t<detail::shared_ptr_compatible<U, T>::value> = 0>
    WeakPtr(const WeakPtr<U>& o) noexcept : _ptr{o._ptr}, _control{o._control} {
      if (_control) _control->add_weak();
    }

    WeakPtr(WeakPtr&& o) noexcept : _ptr{zs::exchange(o._ptr, nullptr)},
                                    _control{zs::exchange(o._control, nullptr)} {}

    template <typename U, enable_if_t<detail::shared_ptr_compatible<U, T>::value> = 0>
    WeakPtr(const SharedPtr<U>& o) noexcept : _ptr{o._ptr}, _control{o._control} {
      if (_control) _control->add_weak();
    }

    WeakPtr(const SharedPtr<T>& o) noexcept : _ptr{o._ptr}, _control{o._control} {
      if (_control) _control->add_weak();
    }

    ~WeakPtr() noexcept { release_(); }

    WeakPtr& operator=(const WeakPtr& o) noexcept {
      if (this != &o) {
        WeakPtr tmp{o};
        swap(tmp);
      }
      return *this;
    }

    WeakPtr& operator=(WeakPtr&& o) noexcept {
      if (this != &o) {
        release_();
        _ptr = zs::exchange(o._ptr, nullptr);
        _control = zs::exchange(o._control, nullptr);
      }
      return *this;
    }

    void reset() noexcept { release_(); }

    void swap(WeakPtr& o) noexcept {
      zs_swap(_ptr, o._ptr);
      zs_swap(_control, o._control);
    }

    [[nodiscard]] size_t use_count() const noexcept {
      return _control ? _control->strong_count() : 0;
    }
    [[nodiscard]] bool expired() const noexcept { return use_count() == 0; }
    [[nodiscard]] SharedPtr<T> lock() const noexcept { return SharedPtr<T>{*this}; }

  private:
    template <typename U>
    friend class SharedPtr;
    template <typename U>
    friend class WeakPtr;

    void release_() noexcept {
      if (_control) {
        _control->release_weak();
        _control = nullptr;
        _ptr = nullptr;
      }
    }

    pointer _ptr{nullptr};
    detail::SharedControlBlockBase* _control{nullptr};
  };

  template <typename T, typename... Args, enable_if_t<!is_array_v<T>> = 0>
  auto make_unique(Args&&... args) -> decltype(UniquePtr<T>(new T(FWD(args)...))) {
    return UniquePtr<T>(new T(FWD(args)...));
  }

  template <typename T, enable_if_t<is_array_v<T> && (extent<T>::value == 0)> = 0>
  auto make_unique(size_t count) -> UniquePtr<remove_extent_t<T>[]> {
    using element_type = remove_extent_t<T>;
    return UniquePtr<element_type[]>(new element_type[count]());
  }

  // --- SharedPtr cast functions ---

  template <typename To, typename From>
  SharedPtr<To> static_pointer_cast(const SharedPtr<From>& r) noexcept {
    auto p = static_cast<To*>(r._ptr);
    if (r._control) r._control->add_strong();
    return SharedPtr<To>(p, r._control, 0);
  }

  template <typename To, typename From>
  SharedPtr<To> static_pointer_cast(SharedPtr<From>&& r) noexcept {
    auto p = static_cast<To*>(r._ptr);
    r._ptr = nullptr;
    auto ctrl = zs::exchange(r._control, nullptr);
    return SharedPtr<To>(p, ctrl, 0);
  }

  template <typename To, typename From>
  SharedPtr<To> dynamic_pointer_cast(const SharedPtr<From>& r) noexcept {
    if (auto p = dynamic_cast<To*>(r._ptr)) {
      if (r._control) r._control->add_strong();
      return SharedPtr<To>(p, r._control, 0);
    }
    return SharedPtr<To>{};
  }

  template <typename To, typename From>
  SharedPtr<To> dynamic_pointer_cast(SharedPtr<From>&& r) noexcept {
    if (auto p = dynamic_cast<To*>(r._ptr)) {
      r._ptr = nullptr;
      auto ctrl = zs::exchange(r._control, nullptr);
      return SharedPtr<To>(p, ctrl, 0);
    }
    return SharedPtr<To>{};
  }

  template <typename To, typename From>
  SharedPtr<To> const_pointer_cast(const SharedPtr<From>& r) noexcept {
    auto p = const_cast<To*>(r._ptr);
    if (r._control) r._control->add_strong();
    return SharedPtr<To>(p, r._control, 0);
  }

  // --- SharedPtr comparison operators ---

  template <typename T1, typename T2>
  constexpr bool operator==(const SharedPtr<T1>& a, const SharedPtr<T2>& b) noexcept {
    return a.get() == b.get();
  }

  template <typename T1, typename T2>
  constexpr bool operator!=(const SharedPtr<T1>& a, const SharedPtr<T2>& b) noexcept {
    return a.get() != b.get();
  }

  template <typename T>
  constexpr bool operator==(const SharedPtr<T>& a, decltype(nullptr)) noexcept {
    return !a;
  }

  template <typename T>
  constexpr bool operator==(decltype(nullptr), const SharedPtr<T>& a) noexcept {
    return !a;
  }

  template <typename T>
  constexpr bool operator!=(const SharedPtr<T>& a, decltype(nullptr)) noexcept {
    return static_cast<bool>(a);
  }

  template <typename T>
  constexpr bool operator!=(decltype(nullptr), const SharedPtr<T>& a) noexcept {
    return static_cast<bool>(a);
  }

  // --- make functions ---

  template <typename T, typename... Args, enable_if_t<!is_array_v<T>> = 0>
  auto make_shared(Args&&... args) -> SharedPtr<T> {
    return SharedPtr<T>(new T(FWD(args)...));
  }

  template <typename T, enable_if_t<is_array_v<T> && (extent<T>::value == 0)> = 0>
  auto make_shared(size_t count) -> SharedPtr<remove_extent_t<T>[]> {
    using element_type = remove_extent_t<T>;
    return SharedPtr<element_type[]>(new element_type[count]());
  }

}  // namespace zs