// Copyright (c) zpc contributors. Licensed under the MIT License.
// ZpcReflectObject.hpp — Base class for reflected types with runtime member
// access and method invocation.
//
// Reflected types that inherit from zs::reflect::Object gain:
//   - obj.getField("name")              → type-erased Any
//   - obj.setField("name", Any(val))    → set from Any
//   - obj.invoke("method", args, nargs) → call by name, returns Any
//   - obj.typeInfo()                    → const TypeInfo&
//
// Everything in this header is std-free — it reuses type traits from
// ZpcMeta.hpp and only requires <new> for placement new.
//
// The code generator emits a per-type Accessor using static function
// pointers and flat arrays — no heap allocation at runtime.

#pragma once

#include "ZpcReflectRuntime.hpp"

#include <new>  // ::operator new / delete, placement new

namespace zs {
namespace reflect {

  // -----------------------------------------------------------------------
  // Any — lightweight, type-erased value wrapper (std-free, SBO)
  // -----------------------------------------------------------------------

  class Any {
  public:
    /// Inline buffer capacity (bytes).  Types up to this size are stored
    /// without heap allocation.
    static constexpr zs::size_t kInlineCap = 48;
    static constexpr zs::size_t kAlign     = 16;

  private:
    // --- VTable (type-erased operations) ---------------------------------

    struct Ops {
      void (*destroy)(void*);
      void (*copy)(void* dst, const void* src);
      void (*move)(void* dst, void* src);
    };

    template <typename T> static void opDestroy(void* p) {
      static_cast<T*>(p)->~T();
    }
    template <typename T> static void opCopy(void* dst, const void* src) {
      ::new (dst) T(*static_cast<const T*>(src));
    }
    template <typename T> static void opMove(void* dst, void* src) {
      ::new (dst) T(zs::move(*static_cast<T*>(src)));
    }

    template <typename T>
    static const Ops* opsFor() {
      static const Ops ops = {&opDestroy<T>, &opCopy<T>, &opMove<T>};
      return &ops;
    }

    // --- Type tag (per-type identity via static-variable address) --------

    using Tag = const void*;
    template <typename T>
    static Tag tagFor() noexcept {
      static const char tag = 0;
      return &tag;
    }

    // --- Storage ---------------------------------------------------------

    alignas(kAlign) char buf_[kInlineCap]{};
    void*      ptr_{nullptr};
    const Ops* ops_{nullptr};
    Tag        tag_{nullptr};
    zs::size_t size_{0};

    bool isInline() const noexcept {
      return ptr_ == static_cast<const void*>(buf_);
    }

    void releaseStorage() {
      if (!ptr_) return;
      if (ops_ && ops_->destroy) ops_->destroy(ptr_);
      if (!isInline()) ::operator delete(ptr_);
      ptr_  = nullptr;
      ops_  = nullptr;
      tag_  = nullptr;
      size_ = 0;
    }

  public:
    Any() noexcept = default;
    ~Any() { releaseStorage(); }

    /// Construct from an arbitrary value (perfect forwarding, std-free).
    /// Disabled when T decays to Any (use copy / move ctor instead).
    template <typename T,
              zs::enable_if_t<!zs::is_same_v<zs::decay_t<T>, Any>> = 0>
    explicit Any(T&& val) {
      using D = zs::decay_t<T>;
      size_ = sizeof(D);
      tag_  = tagFor<D>();
      ops_  = opsFor<D>();
      if constexpr (sizeof(D) <= kInlineCap && alignof(D) <= kAlign) {
        ptr_ = buf_;
      } else {
        ptr_ = ::operator new(sizeof(D));
      }
      ::new (ptr_) D(zs::forward<T>(val));
    }

    /// Copy constructor.
    Any(const Any& o) : ops_(o.ops_), tag_(o.tag_), size_(o.size_) {
      if (!o.ptr_) return;
      if (o.isInline()) {
        ptr_ = buf_;
      } else {
        ptr_ = ::operator new(o.size_);
      }
      if (ops_ && ops_->copy) ops_->copy(ptr_, o.ptr_);
    }

    /// Move constructor.
    Any(Any&& o) noexcept : ops_(o.ops_), tag_(o.tag_), size_(o.size_) {
      if (!o.ptr_) return;
      if (o.isInline()) {
        ptr_ = buf_;
        if (ops_ && ops_->move) ops_->move(ptr_, o.ptr_);
        if (o.ops_ && o.ops_->destroy) o.ops_->destroy(o.ptr_);
      } else {
        ptr_ = o.ptr_;  // steal heap pointer
      }
      o.ptr_  = nullptr;
      o.ops_  = nullptr;
      o.tag_  = nullptr;
      o.size_ = 0;
    }

    Any& operator=(const Any& o) {
      if (this != &o) {
        releaseStorage();
        ops_  = o.ops_;
        tag_  = o.tag_;
        size_ = o.size_;
        if (o.ptr_) {
          ptr_ = o.isInline() ? static_cast<void*>(buf_)
                              : ::operator new(o.size_);
          if (ops_ && ops_->copy) ops_->copy(ptr_, o.ptr_);
        }
      }
      return *this;
    }

    Any& operator=(Any&& o) noexcept {
      if (this != &o) {
        releaseStorage();
        ops_  = o.ops_;
        tag_  = o.tag_;
        size_ = o.size_;
        if (o.ptr_) {
          if (o.isInline()) {
            ptr_ = buf_;
            if (ops_ && ops_->move) ops_->move(ptr_, o.ptr_);
            if (o.ops_ && o.ops_->destroy) o.ops_->destroy(o.ptr_);
          } else {
            ptr_ = o.ptr_;
          }
        }
        o.ptr_  = nullptr;
        o.ops_  = nullptr;
        o.tag_  = nullptr;
        o.size_ = 0;
      }
      return *this;
    }

    // --- Observers -------------------------------------------------------

    void clear() { releaseStorage(); }

    bool hasValue() const noexcept { return ptr_ != nullptr; }

    /// Unchecked access — undefined behaviour on type mismatch.
    template <typename T> T&       as()       { return *static_cast<T*>(ptr_); }
    template <typename T> const T& as() const { return *static_cast<const T*>(ptr_); }

    /// Safe access — returns nullptr on type mismatch.
    template <typename T>
    T* tryAs() noexcept {
      return tag_ == tagFor<zs::decay_t<T>>()
                 ? static_cast<T*>(ptr_) : nullptr;
    }
    template <typename T>
    const T* tryAs() const noexcept {
      return tag_ == tagFor<zs::decay_t<T>>()
                 ? static_cast<const T*>(ptr_) : nullptr;
    }

    /// Check if the stored value is of type T.
    template <typename T>
    bool holdsType() const noexcept {
      return tag_ == tagFor<zs::decay_t<T>>();
    }

    void*       rawPtr()       noexcept { return ptr_; }
    const void* rawPtr() const noexcept { return ptr_; }
  };

  // -----------------------------------------------------------------------
  // Function-pointer typedefs for field / method access
  // -----------------------------------------------------------------------

  /// Signature: read a field from `obj` into `*out`.
  using FieldGetterFn   = void (*)(const void* obj, Any* out);
  /// Signature: write `*val` into a field of `obj`.
  using FieldSetterFn   = void (*)(void* obj, const Any* val);
  /// Signature: invoke a method on `obj` with `args[0..nargs-1]`,
  ///            write return value into `*ret` (may be nullptr for void).
  using MethodInvokerFn = void (*)(void* obj, const Any* args, int nargs, Any* ret);

  // -----------------------------------------------------------------------
  // FieldAccessor / MethodAccessor — flat descriptors in static tables
  // -----------------------------------------------------------------------

  struct FieldAccessor {
    const char*   name;   ///< Field name (string literal).
    FieldGetterFn get;    ///< Getter (never null).
    FieldSetterFn set;    ///< Setter (nullptr for const / read-only fields).
  };

  struct MethodAccessor {
    const char*     name;    ///< Method name (string literal).
    MethodInvokerFn invoke;  ///< Invoker (never null).
  };

  // -----------------------------------------------------------------------
  // Accessor — per-type dispatch table (static arrays, no heap)
  // -----------------------------------------------------------------------

  struct Accessor {
    const TypeInfo*       info;
    const FieldAccessor*  fields;
    int                   fieldCount;
    const MethodAccessor* methods;
    int                   methodCount;

    // --- Lookup (linear search; O(N) — fast for typical field counts) ----

    const FieldAccessor* findField(const char* name) const {
      for (int i = 0; i < fieldCount; ++i)
        if (detail::str_eq(fields[i].name, name)) return &fields[i];
      return nullptr;
    }

    const MethodAccessor* findMethod(const char* name) const {
      for (int i = 0; i < methodCount; ++i)
        if (detail::str_eq(methods[i].name, name)) return &methods[i];
      return nullptr;
    }

    // --- Convenience wrappers --------------------------------------------

    bool getField(const void* obj, const char* name, Any* out) const {
      const FieldAccessor* fa = findField(name);
      if (!fa || !fa->get) return false;
      fa->get(obj, out);
      return true;
    }

    bool setField(void* obj, const char* name, const Any* val) const {
      const FieldAccessor* fa = findField(name);
      if (!fa || !fa->set) return false;
      fa->set(obj, val);
      return true;
    }

    bool invoke(void* obj, const char* name,
                const Any* args, int nargs, Any* ret) const {
      const MethodAccessor* ma = findMethod(name);
      if (!ma || !ma->invoke) return false;
      ma->invoke(obj, args, nargs, ret);
      return true;
    }

    bool hasField(const char* name)  const { return findField(name)  != nullptr; }
    bool hasMethod(const char* name) const { return findMethod(name) != nullptr; }
  };

  // -----------------------------------------------------------------------
  // AccessorRegistry — intrusive linked list (no std containers, no mutex)
  // -----------------------------------------------------------------------

  struct AccessorEntry {
    zs::u64         typeHash;
    const Accessor* accessor;
    AccessorEntry*  next;
  };

  class AccessorRegistry {
  public:
    /// Register an entry (called during static initialisation).
    static void add(AccessorEntry* entry) {
      entry->next = head_;
      head_       = entry;
    }

    /// Look up by type hash.
    static const Accessor* find(zs::u64 typeHash) {
      for (auto* e = head_; e; e = e->next)
        if (e->typeHash == typeHash) return e->accessor;
      return nullptr;
    }

    /// Look up by type name (linear scan through linked list).
    static const Accessor* findByName(const char* name) {
      for (auto* e = head_; e; e = e->next)
        if (e->accessor && e->accessor->info
            && detail::str_eq(e->accessor->info->name, name))
          return e->accessor;
      return nullptr;
    }

  private:
    static inline AccessorEntry* head_ = nullptr;  // C++17 inline variable
  };

  /// RAII helper used by generated code to register an Accessor.
  struct AutoRegisterAccessor {
    AccessorEntry entry;

    AutoRegisterAccessor(zs::u64 typeHash, const Accessor* acc) {
      entry.typeHash = typeHash;
      entry.accessor = acc;
      entry.next     = nullptr;
      AccessorRegistry::add(&entry);
    }
  };

  // -----------------------------------------------------------------------
  // Object — polymorphic base class for reflected types (std-free API)
  // -----------------------------------------------------------------------

  class Object {
  public:
    virtual ~Object() = default;

    /// Returns the TypeInfo for this concrete type.
    virtual const TypeInfo& typeInfo() const = 0;

    /// Returns the Accessor for this concrete type.
    virtual const Accessor& accessor() const = 0;

    // --- Field access by name ---

    Any getField(const char* name) const {
      Any out;
      accessor().getField(static_cast<const void*>(this), name, &out);
      return out;
    }

    bool setField(const char* name, const Any& val) {
      return accessor().setField(static_cast<void*>(this), name, &val);
    }

    bool hasField(const char* name) const {
      return accessor().hasField(name);
    }

    // --- Method invocation by name ---

    Any invoke(const char* name, const Any* args = nullptr, int nargs = 0) {
      Any ret;
      accessor().invoke(static_cast<void*>(this), name, args, nargs, &ret);
      return ret;
    }

    bool hasMethod(const char* name) const {
      return accessor().hasMethod(name);
    }

    // --- Introspection (no std containers) ---

    const char* typeName()  const { return typeInfo().name; }
    zs::u64     typeHash()  const { return typeInfo().typeHash; }
    int fieldCount()  const { return accessor().fieldCount; }
    int methodCount() const { return accessor().methodCount; }

    const FieldAccessor* fieldAt(int i) const {
      const Accessor& a = accessor();
      return (i >= 0 && i < a.fieldCount) ? &a.fields[i] : nullptr;
    }
    const MethodAccessor* methodAt(int i) const {
      const Accessor& a = accessor();
      return (i >= 0 && i < a.methodCount) ? &a.methods[i] : nullptr;
    }
  };

  // -----------------------------------------------------------------------
  // AccessorOf<T> — compile-time accessor lookup (specialized by codegen)
  // -----------------------------------------------------------------------

  /// Primary template — no accessor available.
  template <typename T, typename = void>
  struct AccessorOf {
    static constexpr bool available = false;
  };

  /// Helper to test whether a type has an Accessor.
  template <typename T>
  inline constexpr bool has_accessor_v = AccessorOf<zs::decay_t<T>>::available;

  /// Convenience: get the Accessor for a type.
  template <typename T>
  inline const Accessor* accessor_of() {
    if constexpr (has_accessor_v<T>)
      return &AccessorOf<zs::decay_t<T>>::get();
    else
      return nullptr;
  }

  // -----------------------------------------------------------------------
  // Free-function field / method access (no Object inheritance required)
  // -----------------------------------------------------------------------

  /// Get a field from any reflected object.
  template <typename T>
  Any getField(const T& obj, const char* name) {
    Any out;
    if constexpr (has_accessor_v<T>)
      AccessorOf<zs::decay_t<T>>::get().getField(&obj, name, &out);
    return out;
  }

  /// Set a field on any reflected object.
  template <typename T>
  bool setField(T& obj, const char* name, const Any& value) {
    if constexpr (has_accessor_v<T>)
      return AccessorOf<zs::decay_t<T>>::get().setField(&obj, name, &value);
    return false;
  }

  /// Invoke a method on any reflected object.
  template <typename T>
  Any invoke(T& obj, const char* name,
             const Any* args = nullptr, int nargs = 0) {
    Any ret;
    if constexpr (has_accessor_v<T>)
      AccessorOf<zs::decay_t<T>>::get().invoke(&obj, name, args, nargs, &ret);
    return ret;
  }

}  // namespace reflect
}  // namespace zs
