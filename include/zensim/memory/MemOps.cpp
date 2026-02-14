#include "MemOps.hpp"
#include <iostream>

namespace zs {

  void *allocate(host_mem_tag, size_t size, size_t alignment, const source_location &loc) {
    void *ret{nullptr};
#ifdef _MSC_VER
    ret = _aligned_malloc(size, alignment);
#else
    // ret = std::aligned_alloc(alignment, size);
    ret = std::malloc(size);
#endif
#if ZS_ENABLE_OFB_ACCESS_CHECK
    if (ret == nullptr) {
      std::cerr << "\nHost Side Error: allocattion failed (size: " << size
                << " bytes, alignment: " << alignment << " bytes)"
                << "\n============================================================\n"
                << "# File: \"" << loc.file_name() << "\"\n"
                << "# Ln " << loc.line() << ", Col " << loc.column() << "\n"
                << "# Func: \"" << loc.function_name() << "\"\n"
                << "============================================================\n\n";
    }
#endif
    return ret;
  }
  void deallocate(host_mem_tag, void *ptr, size_t size, size_t alignment,
                  const source_location &loc) {
#ifdef _MSC_VER
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
  }
  void memset(host_mem_tag, void *addr, int chval, size_t size, const source_location &loc) {
    std::memset(addr, chval, size);
  }
  void copy(host_mem_tag, void *dst, void *src, size_t size, const source_location &loc) {
    std::memcpy(dst, src, size);
  }

}  // namespace zs