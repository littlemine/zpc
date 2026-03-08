// Copyright (c) zpc contributors. Licensed under the MIT License.
// ZpcReflectRegistry.hpp — Global runtime type registry for reflected types.
//
// Generated code registers itself here via static initializers so that types
// can be looked up by name or hash at runtime.
//
// This header is std-free — it uses an intrusive linked list for storage,
// consistent with the patterns in ZpcMeta.hpp / ZpcImplPattern.hpp.

#pragma once

#include "ZpcReflectRuntime.hpp"

namespace zs {
namespace reflect {

  // -----------------------------------------------------------------------
  // TypeRegistryEntry — intrusive linked-list node (one per reflected type)
  // -----------------------------------------------------------------------

  struct TypeRegistryEntry {
    const TypeInfo*    info;
    TypeRegistryEntry* next;
  };

  // -----------------------------------------------------------------------
  // TypeRegistry — global registry using intrusive linked list
  // -----------------------------------------------------------------------

  /// @note Registration happens during static initialisation (before main).
  /// Concurrent registration from multiple TUs is safe on platforms that
  /// serialize static-init per-TU (all major compilers).  Runtime lookup
  /// after initialisation is inherently thread-safe (read-only traversal).
  class TypeRegistry {
  public:
    /// Register a type.  Called from static initialisers.
    static void registerType(const TypeInfo* info) {
      if (!info) return;
      // prevent double registration
      if (findByHash(info->typeHash)) return;
      auto* entry = ::new TypeRegistryEntry{info, head_};
      head_ = entry;
    }

    /// Unregister by hash (e.g. for hot-reload).
    /// @note Leaks the unlinked node intentionally — it originated from
    /// ::operator new during static init and there is no safe dealloc order.
    static void unregisterType(zs::u64 hash) {
      TypeRegistryEntry** pp = &head_;
      while (*pp) {
        if ((*pp)->info && (*pp)->info->typeHash == hash) {
          *pp = (*pp)->next;
          return;
        }
        pp = &(*pp)->next;
      }
    }

    /// Look up by qualified name.
    static const TypeInfo* findByName(const char* name) {
      if (!name) return nullptr;
      for (auto* e = head_; e; e = e->next) {
        if (e->info && detail::str_eq(e->info->name, name))
          return e->info;
      }
      return nullptr;
    }

    /// Look up by hash.
    static const TypeInfo* findByHash(zs::u64 hash) {
      for (auto* e = head_; e; e = e->next)
        if (e->info && e->info->typeHash == hash) return e->info;
      return nullptr;
    }

    /// Iterate all registered types, calling `fn(const TypeInfo*)` for each.
    template <typename Fn>
    static void forEach(Fn&& fn) {
      for (auto* e = head_; e; e = e->next)
        if (e->info) fn(e->info);
    }

    /// Count of registered types.
    static int count() {
      int n = 0;
      for (auto* e = head_; e; e = e->next) ++n;
      return n;
    }

  private:
    static inline TypeRegistryEntry* head_ = nullptr;  // C++17 inline variable
  };

  // -----------------------------------------------------------------------
  // AutoRegister — RAII helper used by generated code
  // -----------------------------------------------------------------------

  struct AutoRegister {
    explicit AutoRegister(const TypeInfo* info) {
      TypeRegistry::registerType(info);
    }
  };

}  // namespace reflect
}  // namespace zs
