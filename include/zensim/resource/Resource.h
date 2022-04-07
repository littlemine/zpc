#pragma once

#include <atomic>
#include <stdexcept>
#include <vector>

#include "zensim/Reflection.h"
#include "zensim/Singleton.h"
#include "zensim/execution/ExecutionPolicy.hpp"
#include "zensim/memory/MemOps.hpp"
#include "zensim/memory/MemoryResource.h"
// #include "zensim/types/Pointers.hpp"
#include "zensim/memory/Allocator.h"
#include "zensim/types/SmallVector.hpp"
#include "zensim/types/Tuple.h"
#if ZS_ENABLE_CUDA
#  include "zensim/cuda/memory/Allocator.h"
#endif
#if ZS_ENABLE_OPENMP
#endif

namespace zs {

  template <bool is_virtual_ = false, typename T = std::byte> struct ZPC_API ZSPmrAllocator {
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    /// this is different from std::polymorphic_allocator
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    using is_virtual = wrapv<is_virtual_>;
    using resource_type = conditional_t<is_virtual::value, vmr_t, mr_t>;

    struct ResourceCloner {
      struct Interface {
        virtual ~Interface() = default;
        virtual std::unique_ptr<Interface> clone() const = 0;
        virtual std::unique_ptr<resource_type> invoke() const = 0;
      };
      template <typename F> struct Cloner : Interface {
        template <typename Fn> Cloner(Fn &&f) : _f{FWD(f)} {}
        std::unique_ptr<Interface> clone() const override {
          return std::make_unique<Cloner<F>>(_f);
        }
        std::unique_ptr<resource_type> invoke() const override { return std::invoke(_f); }

        F _f;
      };

      constexpr ResourceCloner() = default;
      ~ResourceCloner() = default;
      ResourceCloner(const ResourceCloner &o) : _cloner{o._cloner->clone()} {}
      ResourceCloner(ResourceCloner &&) = default;
      ResourceCloner &operator=(const ResourceCloner &o) {
        ResourceCloner tmp{o};
        std::swap(*this, tmp);
        return *this;
      }
      ResourceCloner &operator=(ResourceCloner &&) = default;
      ResourceCloner &swap(ResourceCloner &o) noexcept {
        std::swap(_cloner, o._cloner);
        return *this;
      }

      template <typename F> constexpr ResourceCloner(F &&f)
          : _cloner{std::make_unique<Cloner<F>>(FWD(f))} {}

      std::unique_ptr<resource_type> operator()() const { return _cloner->invoke(); }

      std::unique_ptr<Interface> _cloner{};
    };

    ZSPmrAllocator() = default;
    ZSPmrAllocator(ZSPmrAllocator &&) = default;
    ZSPmrAllocator &operator=(ZSPmrAllocator &&) = default;
    ZSPmrAllocator(const ZSPmrAllocator &o) { *this = o.select_on_container_copy_construction(); }
    ZSPmrAllocator &operator=(const ZSPmrAllocator &o) {
      *this = o.select_on_container_copy_construction();
      return *this;
    }

    friend void swap(ZSPmrAllocator &a, ZSPmrAllocator &b) {
      std::swap(a.res, b.res);
      std::swap(a.location, b.location);
    }

    constexpr resource_type *resource() noexcept { return res.get(); }
    [[nodiscard]] void *allocate(std::size_t bytes,
                                 std::size_t alignment = alignof(std::max_align_t)) {
      return res->allocate(bytes, alignment);
    }
    void deallocate(void *p, std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) {
      res->deallocate(p, bytes, alignment);
    }
    bool is_equal(const ZSPmrAllocator &other) const noexcept {
      return res.get() == other.res.get() && location == other.location;
    }
    template <bool V = is_virtual::value>
    std::enable_if_t<V, bool> commit(std::size_t offset,
                                     std::size_t bytes = resource_type::s_chunk_granularity) {
      return res->commit(offset, bytes);
    }
    template <bool V = is_virtual::value>
    std::enable_if_t<V, bool> evict(std::size_t offset,
                                    std::size_t bytes = resource_type::s_chunk_granularity) {
      return res->evict(offset, bytes);
    }
    template <bool V = is_virtual::value> std::enable_if_t<V, bool> check_residency(
        std::size_t offset, std::size_t bytes = resource_type::s_chunk_granularity) const {
      return res->check_residency(offset, bytes);
    }
    template <bool V = is_virtual::value>
    std::enable_if_t<V, void *> address(std::size_t offset = 0) const {
      return res->address(offset);
    }

    ZSPmrAllocator select_on_container_copy_construction() const {
      ZSPmrAllocator ret{};
      ret.cloner = this->cloner;
      ret.res = this->cloner();
      ret.location = this->location;
      return ret;
    }

    /// owning upstream should specify deleter
    template <template <typename Tag> class ResourceT, typename... Args, std::size_t... Is>
    void setOwningUpstream(mem_tags tag, ProcID devid, std::tuple<Args &&...> args,
                           index_seq<Is...>) {
      match(
          [&](auto t) {
            if constexpr (is_memory_source_available(t)) {
              using MemT = RM_CVREF_T(t);
              res = std::make_unique<ResourceT<MemT>>(devid, std::get<Is>(args)...);
              location = MemoryLocation{t.value, devid};
              cloner = [devid, args]() -> std::unique_ptr<resource_type> {
                std::unique_ptr<resource_type> ret{};
                std::apply(
                    [&ret](auto &&...ctorArgs) {
                      ret = std::make_unique<ResourceT<decltype(t)>>(FWD(ctorArgs)...);
                    },
                    std::tuple_cat(std::make_tuple(devid), args));
                return ret;
              };
            }
          },
          [](...) {})(tag);
    }
    template <template <typename Tag> class ResourceT, typename MemTag, typename... Args>
    void setOwningUpstream(MemTag tag, ProcID devid, Args &&...args) {
      if constexpr (is_same_v<MemTag, mem_tags>)
        setOwningUpstream<ResourceT>(tag, devid, std::forward_as_tuple(FWD(args)...),
                                     std::index_sequence_for<Args...>{});
      else {
        if constexpr (is_memory_source_available(tag)) {
          res = std::make_unique<ResourceT<MemTag>>(devid, FWD(args)...);
          location = MemoryLocation{MemTag::value, devid};
          cloner = [devid, args = std::make_tuple(args...)]() -> std::unique_ptr<resource_type> {
            std::unique_ptr<resource_type> ret{};
            std::apply(
                [&ret](auto &&...ctorArgs) {
                  ret = std::make_unique<ResourceT<MemTag>>(FWD(ctorArgs)...);
                },
                std::tuple_cat(std::make_tuple(devid), args));
            return ret;
          };
        }
      }
    }

    ResourceCloner cloner{};
    std::unique_ptr<resource_type> res{};
    MemoryLocation location{memsrc_e::host, -1};
  };

  template <typename Allocator> struct is_zs_allocator : std::false_type {};
  template <bool is_virtual, typename T> struct is_zs_allocator<ZSPmrAllocator<is_virtual, T>>
      : std::true_type {};
  template <typename Allocator> using is_virtual_zs_allocator
      = conditional_t<is_zs_allocator<Allocator>::value, typename Allocator::is_virtual,
                      std::false_type>;

  template <typename MemTag> constexpr bool is_memory_source_available(MemTag) noexcept {
    if constexpr (is_same_v<MemTag, device_mem_tag>)
      return ZS_ENABLE_CUDA;
    else if constexpr (is_same_v<MemTag, um_mem_tag>)
      return ZS_ENABLE_CUDA;
    else if constexpr (is_same_v<MemTag, host_mem_tag>)
      return true;
    return false;
  }

  inline ZPC_API ZSPmrAllocator<> get_memory_source(memsrc_e mre, ProcID devid,
                                                    std::string_view advice = std::string_view{}) {
    const mem_tags tag = to_memory_source_tag(mre);
    ZSPmrAllocator<> ret{};
    if (advice.empty()) {
      if (mre == memsrc_e::um) {
        if (devid < -1)
          match(
              [&ret, devid](auto tag) {
                if constexpr (is_memory_source_available(tag)
                              || is_same_v<RM_CVREF_T(tag), mem_tags>)
                  ret.setOwningUpstream<advisor_memory_resource>(tag, devid, "READ_MOSTLY");
              },
              [](...) {})(tag);
        else
          match(
              [&ret, devid](auto tag) {
                if constexpr (is_memory_source_available(tag)
                              || is_same_v<RM_CVREF_T(tag), mem_tags>)
                  ret.setOwningUpstream<advisor_memory_resource>(tag, devid, "PREFERRED_LOCATION");
              },
              [](...) {})(tag);
      } else {
        // match([&ret](auto &tag) { ret.setNonOwningUpstream<raw_memory_resource>(tag); })(tag);
        match(
            [&ret, devid](auto tag) {
              if constexpr (is_memory_source_available(tag) || is_same_v<RM_CVREF_T(tag), mem_tags>)
                ret.setOwningUpstream<default_memory_resource>(tag, devid);
            },
            [](...) {})(tag);
        // ret.setNonOwningUpstream<raw_memory_resource>(tag);
      }
    } else
      match(
          [&ret, &advice, devid](auto tag) {
            if constexpr (is_memory_source_available(tag) || is_same_v<RM_CVREF_T(tag), mem_tags>)
              ret.setOwningUpstream<advisor_memory_resource>(tag, devid, advice);
          },
          [](...) {})(tag);
    return ret;
  }

  inline ZPC_API ZSPmrAllocator<true> get_virtual_memory_source(memsrc_e mre, ProcID devid,
                                                                std::size_t bytes,
                                                                std::string_view option = "STACK") {
    const mem_tags tag = to_memory_source_tag(mre);
    ZSPmrAllocator<true> ret{};
    if (mre == memsrc_e::um)
      throw std::runtime_error("no corresponding virtual memory resource for [um]");
    match(
        [&ret, devid, bytes, option](auto tag) {
          if constexpr (!is_same_v<decltype(tag), um_mem_tag>)
            if constexpr (is_memory_source_available(tag)) {
              if (option == "ARENA")
                ret.setOwningUpstream<arena_virtual_memory_resource>(tag, devid, bytes);
              else if (option == "STACK" || option.empty())
                ret.setOwningUpstream<stack_virtual_memory_resource>(tag, devid, bytes);
              else
                throw std::runtime_error(fmt::format("unkonwn vmr option [{}]\n", option));
            }
        },
        [](...) {})(tag);
    return ret;
  }

  template <execspace_e space> constexpr bool initialize_backend(wrapv<space>) noexcept {
    return false;
  }

  struct ZPC_API Resource {
    static std::atomic_ullong &counter() noexcept;
    static Resource &instance() noexcept;
    static void copy(MemoryEntity dst, MemoryEntity src, std::size_t numBytes) {
      if (dst.location.onHost() && src.location.onHost())
        zs::copy(mem_host, dst.ptr, src.ptr, numBytes);
      else {
        if constexpr (is_memory_source_available(mem_device))
          zs::copy(mem_device, dst.ptr, src.ptr, numBytes);
        else
          throw std::runtime_error("There is no corresponding device backend for Resource::copy");
      }
    }
    static void memset(MemoryEntity dst, char ch, std::size_t numBytes) {
      if (dst.location.onHost())
        zs::memset(mem_host, dst.ptr, ch, numBytes);
      else {
        if constexpr (is_memory_source_available(mem_device))
          zs::memset(mem_device, dst.ptr, ch, numBytes);
        else
          throw std::runtime_error("There is no corresponding device backend for Resource::memset");
      }
    }

    struct AllocationRecord {
      mem_tags tag{};
      std::size_t size{0}, alignment{0};
      std::string allocatorType{};
    };
    Resource();
    ~Resource();

    void record(mem_tags tag, void *ptr, std::string_view name, std::size_t size,
                std::size_t alignment);
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
