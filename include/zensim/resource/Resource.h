#pragma once

#include <atomic>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "zensim/Reflection.h"
#include "zensim/ZpcResource.hpp"
#include "zensim/ZpcFunction.hpp"
#include "zensim/memory/MemOps.hpp"
#include "zensim/memory/MemoryResource.h"
#include "zensim/memory/MemoryBackend.h"
#include "zensim/types/SmallVector.hpp"
#include "zensim/types/Tuple.h"
#include "zensim/types/Property.h"

namespace zs {

  template <bool is_virtual_ = false, typename T = byte> struct ZPC_API ZSPmrAllocator {
    using value_type = T;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    /// this is different from std::polymorphic_allocator
    using propagate_on_container_move_assignment = true_type;
    using propagate_on_container_copy_assignment = true_type;
    using propagate_on_container_swap = true_type;
    using is_virtual = wrapv<is_virtual_>;
    using resource_type = conditional_t<is_virtual::value, vmr_t, mr_t>;

    ZSPmrAllocator() = default;
    ZSPmrAllocator(ZSPmrAllocator &&) = default;
    ZSPmrAllocator &operator=(ZSPmrAllocator &&) = default;
    ZSPmrAllocator(const ZSPmrAllocator &o) { *this = o.select_on_container_copy_construction(); }
    ZSPmrAllocator &operator=(const ZSPmrAllocator &o) {
      *this = o.select_on_container_copy_construction();
      return *this;
    }

    friend void swap(ZSPmrAllocator &a, ZSPmrAllocator &b) noexcept {
      zs_swap(a.res, b.res);
      zs_swap(a.location, b.location);
    }

    constexpr resource_type *resource() noexcept { return res.get(); }
    [[nodiscard]] void *allocate(size_t bytes, size_t alignment = alignof(std::max_align_t)) {
      return res->allocate(bytes, alignment);
    }
    void deallocate(void *p, size_t bytes, size_t alignment = alignof(std::max_align_t)) {
      res->deallocate(p, bytes, alignment);
    }
    bool is_equal(const ZSPmrAllocator &other) const noexcept {
      return res.get() == other.res.get() && location == other.location;
    }
    template <bool V = is_virtual::value>
    enable_if_type<V, bool> commit(size_t offset,
                                   size_t bytes = resource_type::s_chunk_granularity) {
      return res->commit(offset, bytes);
    }
    template <bool V = is_virtual::value>
    enable_if_type<V, bool> evict(size_t offset,
                                  size_t bytes = resource_type::s_chunk_granularity) {
      return res->evict(offset, bytes);
    }
    template <bool V = is_virtual::value> enable_if_type<V, bool> check_residency(
        size_t offset, size_t bytes = resource_type::s_chunk_granularity) const {
      return res->check_residency(offset, bytes);
    }
    template <bool V = is_virtual::value>
    enable_if_type<V, void *> address(size_t offset = 0) const {
      return res->address(offset);
    }

    ZSPmrAllocator select_on_container_copy_construction() const {
      ZSPmrAllocator ret{};
      ret.cloner = this->cloner;
      ret.res = this->cloner();
      ret.location = this->location;
      return ret;
    }

    /// @brief Initialise the allocator with a pre-built resource, its location
    /// and a cloner callable that can recreate an equivalent resource.
    void init(UniquePtr<resource_type> resource, MemoryLocation loc,
              function<UniquePtr<resource_type>()> clonerFn) {
      res = zs::move(resource);
      location = loc;
      cloner = zs::move(clonerFn);
    }

    /// @deprecated Legacy template-based setup.  Prefer init() or get_memory_source().
    template <template <typename Tag> class ResourceT, typename... Args, size_t... Is>
    void setOwningUpstream(mem_tags tag, ProcID devid, zs::tuple<Args...> args,
                           index_sequence<Is...>) {
      match([&](auto t) {
        if constexpr (is_memory_source_available(t)) {
          using MemT = RM_CVREF_T(t);
          res = zs::make_unique<ResourceT<MemT>>(devid, zs::get<Is>(args)...);
          location = MemoryLocation{t.value, devid};
          cloner = [devid, args]() -> UniquePtr<resource_type> {
            UniquePtr<resource_type> ret{};
            zs::apply(
                [&ret](auto &&...ctorArgs) {
                  ret = zs::make_unique<ResourceT<decltype(t)>>(FWD(ctorArgs)...);
                },
                zs::tuple_cat(zs::make_tuple(devid), args));
            return ret;
          };
        } else
          std::cerr << "memory resource \"" << get_var_type_str(t) << "\" not available"
                    << std::endl;
      })(tag);
    }
    /// @deprecated Legacy template-based setup.  Prefer init() or get_memory_source().
    template <template <typename Tag> class ResourceT, typename MemTag, typename... Args>
    void setOwningUpstream(MemTag tag, ProcID devid, Args &&...args) {
      if constexpr (is_same_v<MemTag, mem_tags>)
        setOwningUpstream<ResourceT>(tag, devid, zs::forward_as_tuple(FWD(args)...),
                                     index_sequence_for<Args...>{});
      else {
        if constexpr (is_memory_source_available(tag)) {
          res = zs::make_unique<ResourceT<MemTag>>(devid, args...);
          location = MemoryLocation{MemTag::value, devid};
          cloner
              = [devid, args = zs::make_tuple(FWD(args)...)]() -> UniquePtr<resource_type> {
            UniquePtr<resource_type> ret{};
            zs::apply(
                [&ret](auto &&...ctorArgs) {
                  ret = zs::make_unique<ResourceT<MemTag>>(FWD(ctorArgs)...);
                },
                zs::tuple_cat(zs::make_tuple(devid), args));
            return ret;
          };
        } else
          std::cerr << "memory resource \"" << get_var_type_str(tag) << "\" not available"
                    << std::endl;
      }
    }

    function<UniquePtr<resource_type>()> cloner{};
    UniquePtr<resource_type> res{};
    MemoryLocation location{memsrc_e::host, -1};
  };

  extern template struct ZPC_TEMPLATE_IMPORT ZSPmrAllocator<false, byte>;
  extern template struct ZPC_TEMPLATE_IMPORT ZSPmrAllocator<true, byte>;

  /// @note Queries the runtime registry for execution-space ? memory-space compatibility.
  template <typename Policy, bool is_virtual, typename T>
  bool valid_memspace_for_execution(const Policy &,
                                    const ZSPmrAllocator<is_virtual, T> &allocator) {
    constexpr execspace_e space = Policy::exec_tag::value;
    return MemoryBackendRegistry::instance().valid_memspace_for_execution(
        space, allocator.location.memspace());
  }

  template <typename Allocator> struct is_zs_allocator : false_type {};
  template <bool is_virtual, typename T> struct is_zs_allocator<ZSPmrAllocator<is_virtual, T>>
      : true_type {};
  template <typename Allocator> using is_virtual_zs_allocator
      = conditional_t<is_zs_allocator<Allocator>::value, typename Allocator::is_virtual,
                      false_type>;

  /// @brief Compile-time query kept for backward compatibility in template code
  /// that still uses tag-dispatch.  Prefers the runtime registry when possible.
  template <typename MemTag> constexpr bool is_memory_source_available(MemTag) noexcept {
    if constexpr (is_same_v<MemTag, host_mem_tag>)
      return true;
    // For device/um the compile-time flag is still the conservative answer;
    // at runtime the registry may have additional backends registered.
    else if constexpr (is_same_v<MemTag, device_mem_tag> || is_same_v<MemTag, um_mem_tag>)
      return ZS_ENABLE_DEVICE;
    return false;
  }

  /// @brief Runtime query: is a particular memory source available?
  inline bool is_memory_source_available(memsrc_e mre) noexcept {
    return MemoryBackendRegistry::instance().is_available(mre);
  }

  ZPC_API ZSPmrAllocator<> get_memory_source(memsrc_e mre, ProcID devid,
                                             std::string_view advice = std::string_view{});

  ZPC_API ZSPmrAllocator<true> get_virtual_memory_source(memsrc_e mre, ProcID devid,
                                                         size_t bytes,
                                                         std::string_view option = "STACK");

  template <execspace_e space> constexpr bool initialize_backend(wrapv<space>) noexcept {
    return false;
  }

  struct ZPC_API Resource {
    static std::atomic_ullong &counter() noexcept;
    static Resource &instance() noexcept;
    static void copy(MemoryEntity dst, MemoryEntity src, size_t numBytes);
    static void memset(MemoryEntity dst, char ch, size_t numBytes);

    struct AllocationRecord {
      mem_tags tag{};
      size_t size{0}, alignment{0};
      std::string allocatorType{};
    };
    Resource();
    ~Resource();

    void record(mem_tags tag, void *ptr, std::string_view name, size_t size, size_t alignment);
    void erase(void *ptr);

    void deallocate(void *ptr);

  private:
    mutable std::atomic_ullong _counter{0};
  };

  inline auto select_properties(const std::vector<PropertyTag> &props,
                                const std::vector<SmallString> &names) {
    std::vector<PropertyTag> ret(0);
    for (auto &&name : names)
      for (auto &&prop : props)
        if (prop.name == name) {
          ret.push_back(prop);
          break;
        }
    return ret;
  }

}  // namespace zs
