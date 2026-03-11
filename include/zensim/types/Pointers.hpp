#pragma once

#include "zensim/ZpcResource.hpp"

namespace zs {

  /// this impl only accounts for single object
  template <class T, typename Deleter = DefaultDelete<T>> class copyable_ptr {
  public:
    static_assert(!is_array_v<T>, "copyable_ptr does not support array");
    static_assert(is_copy_constructible_v<T>, "T should be copy-constructible");

    using pointer = add_pointer_t<T>;
    using element_type = T;
    using deleter_type = Deleter;

    template <typename U, typename E, template <typename, typename> class PTR>
    static constexpr bool __safe_conversion_up() noexcept {
      return is_convertible_v<typename PTR<U, E>::pointer, pointer> && !is_array_v<U>;
    }

    /// ctor
    copyable_ptr() = default;
    explicit copyable_ptr(T *p) noexcept : _ptr{p} {}

    /// deleter ctor mimic behavior of UniquePtr
    template <typename D = Deleter, enable_if_type<is_copy_constructible_v<D>, char> = 0>
    copyable_ptr(T *p, const Deleter &d) noexcept : _ptr{p, d} {}
    template <typename D = Deleter, enable_if_type<is_move_constructible_v<D>, char> = 0>
    copyable_ptr(T *p, enable_if_type<!is_lvalue_reference_v<D>, D &&> d) noexcept
        : _ptr{p, zs::move(d)} {}
    template <typename D = Deleter, typename DUnref = remove_reference_t<D>>
    copyable_ptr(T *p, enable_if_type<is_lvalue_reference_v<D>, DUnref &&> d) = delete;

    constexpr copyable_ptr(zs::nullptr_t) noexcept : _ptr{nullptr} {}

    /// override copy (assignment) ctor
    constexpr copyable_ptr(const copyable_ptr &o) noexcept(is_nothrow_copy_constructible_v<T>)
        : _ptr{UniquePtr<T, Deleter>(new T(*o), o.get_deleter())} {}
    copyable_ptr &operator=(const copyable_ptr &o) noexcept(
        is_nothrow_copy_constructible_v<T>) {
      _ptr = UniquePtr<T, Deleter>(new T(*o), o.get_deleter());
      return *this;
    }
    constexpr copyable_ptr(const UniquePtr<T, Deleter> &o) noexcept(
        is_nothrow_copy_constructible_v<T>)
        : _ptr{UniquePtr<T, Deleter>(new T(*o), o.get_deleter())} {}
    copyable_ptr &operator=(const UniquePtr<T, Deleter> &o) noexcept(
        is_nothrow_copy_constructible_v<T>) {
      _ptr = UniquePtr<T, Deleter>(new T(*o), o.get_deleter());
      return *this;
    }
    /// delegate move conversion (assignment) ctor
    template <typename U, typename E,
              enable_if_type<
                  __safe_conversion_up<U, E, copyable_ptr>()
                      && conditional_t<is_reference_v<Deleter>, is_same<E, Deleter>,
                                       is_convertible<E, Deleter>>::value,
                  int> = 0>
    copyable_ptr(copyable_ptr<U, E> &&o) noexcept : _ptr{zs::move(o._ptr)} {}
    template <typename U, typename E,
              enable_if_type<
            __safe_conversion_up<U, E, UniquePtr>()
                      && conditional_t<is_reference_v<Deleter>, is_same<E, Deleter>,
                                       is_convertible<E, Deleter>>::value,
                  int> = 0>
    copyable_ptr(UniquePtr<U, E> &&o) noexcept : _ptr{zs::move(o)} {}

    template <typename U, typename E,
              enable_if_type<
                  __safe_conversion_up<U, E, copyable_ptr>()
                      && conditional_t<is_reference_v<Deleter>, is_same<E, Deleter>,
                                       is_convertible<E, Deleter>>::value,
                  int> = 0>
    copyable_ptr &operator=(copyable_ptr<U, E> &&o) noexcept {
      _ptr = zs::move(o._ptr);
      return *this;
    }
    template <typename U, typename E,
              enable_if_type<
                  __safe_conversion_up<U, E, UniquePtr>()
                      && conditional_t<is_reference_v<Deleter>, is_same<E, Deleter>,
                                       is_convertible<E, Deleter>>::value,
                  int> = 0>
    copyable_ptr &operator=(UniquePtr<U, E> &&o) noexcept {
      _ptr = zs::move(o);
      return *this;
    }

    ~copyable_ptr() = default;

    copyable_ptr(copyable_ptr &&o) = default;
    copyable_ptr &operator=(copyable_ptr &&o) = default;

    /// observer delegation
    add_lvalue_reference_t<element_type> operator*() const { return *_ptr; }
    pointer operator->() const noexcept { return _ptr.operator->(); }
    pointer get() const noexcept { return _ptr.get(); }
    deleter_type &get_deleter() noexcept { return _ptr.get_deleter(); }
    const deleter_type &get_deleter() const noexcept { return _ptr.get_deleter(); }
    explicit operator bool() const noexcept { return static_cast<bool>(_ptr); }

    /// modifier delegation
    pointer release() noexcept { return _ptr.release(); }
    void reset(pointer p = pointer()) noexcept { _ptr.reset(zs::move(p)); }
    void swap(UniquePtr<T, Deleter> &o) noexcept { _ptr.swap(o); }
    void swap(copyable_ptr &o) noexcept { _ptr.swap(o._ptr); }

    operator UniquePtr<T, Deleter> &() noexcept { return _ptr; }
    operator const UniquePtr<T, Deleter> &() const noexcept { return _ptr; }

    UniquePtr<T, Deleter> _ptr;
  };
}  // namespace zs