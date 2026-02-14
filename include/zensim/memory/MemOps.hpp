#pragma once
#include "zensim/memory/MemoryResource.h"
#include "zensim/types/SourceLocation.hpp"
#if ZS_ENABLE_CUDA
#  include "zensim/cuda/memory/MemOps.hpp"
#endif

namespace zs {

  ZPC_CORE_API void *allocate(host_mem_tag, size_t size, size_t alignment,
                              const source_location &loc = source_location::current());
  ZPC_CORE_API void deallocate(host_mem_tag, void *ptr, size_t size, size_t alignment,
                               const source_location &loc = source_location::current());
  ZPC_CORE_API void memset(host_mem_tag, void *addr, int chval, size_t size,
                           const source_location &loc = source_location::current());
  ZPC_CORE_API void copy(host_mem_tag, void *dst, void *src, size_t size,
                         const source_location &loc = source_location::current());

#if 0
  /// dispatch mem op calls
  void *allocate_dispatch(mem_tags tag, size_t size, size_t alignment);
  void deallocate_dispatch(mem_tags tag, void *ptr, size_t size, size_t alignment);
  void memset_dispatch(mem_tags tag, void *addr, int chval, size_t size);
  void copy_dispatch(mem_tags tag, void *dst, void *src, size_t size);
  void advise_dispatch(mem_tags tag, std::string advice, void *addr, size_t bytes, ProcID did);
#endif

  /// default memory operation implementations (fallback)
  template <typename MemTag> bool prepare_context(MemTag, ProcID) { return true; }
  template <typename MemTag> void *allocate(MemTag, size_t size, size_t alignment) {
    std::ostringstream oss;
    oss << "allocate(tag " << get_memory_tag_name(MemTag{}) << ", size " << size
        << ", alignment " << alignment << ") not implemented\n";
    throw std::runtime_error(oss.str());
  }
  template <typename MemTag> void deallocate(MemTag, void *ptr, size_t size, size_t alignment) {
    std::ostringstream oss;
    oss << "deallocate(tag " << get_memory_tag_name(MemTag{}) << ", ptr "
        << reinterpret_cast<std::uintptr_t>(ptr) << ", size " << size << ", alignment "
        << alignment << ") not implemented\n";
    throw std::runtime_error(oss.str());
  }
  template <typename MemTag> void memset(MemTag, void *addr, int chval, size_t size) {
    std::ostringstream oss;
    oss << "memset(tag " << get_memory_tag_name(MemTag{}) << ", ptr "
        << reinterpret_cast<std::uintptr_t>(addr) << ", charval " << chval << ", size " << size
        << ") not implemented\n";
    throw std::runtime_error(oss.str());
  }
  template <typename MemTag> void copy(MemTag, void *dst, void *src, size_t size) {
    std::ostringstream oss;
    oss << "copy(tag " << get_memory_tag_name(MemTag{}) << ", dst "
        << reinterpret_cast<std::uintptr_t>(dst) << ", src "
        << reinterpret_cast<std::uintptr_t>(src) << ", size " << size << ") not implemented\n";
    throw std::runtime_error(oss.str());
  }
  template <typename MemTag> void copyHtoD(MemTag, void *dst, void *src, size_t size) {
    std::ostringstream oss;
    oss << "copyHtoD(tag " << get_memory_tag_name(MemTag{}) << ", dst "
        << reinterpret_cast<std::uintptr_t>(dst) << ", src "
        << reinterpret_cast<std::uintptr_t>(src) << ", size " << size << ") not implemented\n";
    throw std::runtime_error(oss.str());
  }
  template <typename MemTag> void copyDtoH(MemTag, void *dst, void *src, size_t size) {
    std::ostringstream oss;
    oss << "copyDtoH(tag " << get_memory_tag_name(MemTag{}) << ", dst "
        << reinterpret_cast<std::uintptr_t>(dst) << ", src "
        << reinterpret_cast<std::uintptr_t>(src) << ", size " << size << ") not implemented\n";
    throw std::runtime_error(oss.str());
  }
  template <typename MemTag> void copyDtoD(MemTag, void *dst, void *src, size_t size) {
    std::ostringstream oss;
    oss << "copyDtoD(tag " << get_memory_tag_name(MemTag{}) << ", dst "
        << reinterpret_cast<std::uintptr_t>(dst) << ", src "
        << reinterpret_cast<std::uintptr_t>(src) << ", size " << size << ") not implemented\n";
    throw std::runtime_error(oss.str());
  }
  template <typename MemTag, typename... Args>
  void advise(MemTag, std::string advice, void *addr, Args...) {
    std::ostringstream oss;
    oss << "advise(tag " << get_memory_tag_name(MemTag{}) << ", advise " << advice << ", addr "
        << reinterpret_cast<std::uintptr_t>(addr) << ") with " << sizeof...(Args)
        << " args not implemented\n";
    throw std::runtime_error(oss.str());
  }

}  // namespace zs