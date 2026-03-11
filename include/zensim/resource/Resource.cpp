#include "Resource.h"

#include "zensim/execution/Concurrency.h"
#include "zensim/memory/MemoryBackend.h"
#include "zensim/memory/MemoryResource.h"

namespace zs {

  template struct ZPC_TEMPLATE_EXPORT ZSPmrAllocator<false, byte>;
  template struct ZPC_TEMPLATE_EXPORT ZSPmrAllocator<true, byte>;

  // -----------------------------------------------------------------------
  // get_memory_source / get_virtual_memory_source  (runtime-dispatched)
  // -----------------------------------------------------------------------

  ZPC_API ZSPmrAllocator<> get_memory_source(memsrc_e mre, ProcID devid,
                                             std::string_view advice) {
    auto &reg = MemoryBackendRegistry::instance();
    ZSPmrAllocator<> ret{};

    if (advice.empty()) {
      if (mre == memsrc_e::um) {
        std::string_view effectiveAdvice
            = (devid < -1) ? std::string_view{"READ_MOSTLY"}
                           : std::string_view{"PREFERRED_LOCATION"};
        auto resource = reg.create_advisor_resource(mre, devid, effectiveAdvice);
        MemoryLocation loc{mre, devid};
        auto cloner = [mre, devid,
                       adv = std::string(effectiveAdvice)]() -> UniquePtr<mr_t> {
          return MemoryBackendRegistry::instance().create_advisor_resource(mre, devid, adv);
        };
        ret.init(zs::move(resource), loc, zs::move(cloner));
      } else {
        auto resource = reg.create_default_resource(mre, devid);
        MemoryLocation loc{mre, devid};
        auto cloner = [mre, devid]() -> UniquePtr<mr_t> {
          return MemoryBackendRegistry::instance().create_default_resource(mre, devid);
        };
        ret.init(zs::move(resource), loc, zs::move(cloner));
      }
    } else {
      auto resource = reg.create_advisor_resource(mre, devid, advice);
      MemoryLocation loc{mre, devid};
      auto cloner = [mre, devid, adv = std::string(advice)]() -> UniquePtr<mr_t> {
        return MemoryBackendRegistry::instance().create_advisor_resource(mre, devid, adv);
      };
      ret.init(zs::move(resource), loc, zs::move(cloner));
    }
    return ret;
  }

  ZPC_API ZSPmrAllocator<true> get_virtual_memory_source(memsrc_e mre, ProcID devid,
                                                         size_t bytes,
                                                         std::string_view option) {
    auto &reg = MemoryBackendRegistry::instance();
    if (mre == memsrc_e::um)
      throw std::runtime_error("no corresponding virtual memory resource for [um]");

    ZSPmrAllocator<true> ret{};

    if (option == "ARENA") {
      auto resource = reg.create_arena_virtual_resource(mre, devid, bytes);
      MemoryLocation loc{mre, devid};
      auto cloner = [mre, devid, bytes]() -> UniquePtr<vmr_t> {
        return MemoryBackendRegistry::instance().create_arena_virtual_resource(mre, devid, bytes);
      };
      ret.init(zs::move(resource), loc, zs::move(cloner));
    } else if (option == "STACK" || option.empty()) {
      auto resource = reg.create_stack_virtual_resource(mre, devid, bytes);
      MemoryLocation loc{mre, devid};
      auto cloner = [mre, devid, bytes]() -> UniquePtr<vmr_t> {
        return MemoryBackendRegistry::instance().create_stack_virtual_resource(mre, devid, bytes);
      };
      ret.init(zs::move(resource), loc, zs::move(cloner));
    } else {
      std::ostringstream oss;
      oss << "unknown vmr option [" << option << "]\n";
      throw std::runtime_error(oss.str());
    }

    return ret;
  }

  // -----------------------------------------------------------------------
  // Resource::copy / Resource::memset  (runtime-dispatched)
  // -----------------------------------------------------------------------

  void Resource::copy(MemoryEntity dst, MemoryEntity src, size_t numBytes) {
    if (dst.location.onHost() && src.location.onHost()) {
      zs::copy(mem_host, dst.ptr, src.ptr, numBytes);
      return;
    }
    // Try device backend for the non-host side
    memsrc_e deviceMre = !dst.location.onHost() ? dst.location.memspace()
                                                : src.location.memspace();
    auto *ops = MemoryBackendRegistry::instance().get_memory_ops(deviceMre);
    if (!ops)
      throw std::runtime_error("There is no corresponding device backend for Resource::copy");

    if (!dst.location.onHost() && !src.location.onHost()) {
      if (!ops->copyDtoD)
        throw std::runtime_error("copyDtoD not registered for backend");
      ops->copyDtoD(dst.ptr, src.ptr, numBytes);
    } else if (dst.location.onHost() && !src.location.onHost()) {
      if (!ops->copyDtoH)
        throw std::runtime_error("copyDtoH not registered for backend");
      ops->copyDtoH(dst.ptr, src.ptr, numBytes);
    } else if (!dst.location.onHost() && src.location.onHost()) {
      if (!ops->copyHtoD)
        throw std::runtime_error("copyHtoD not registered for backend");
      ops->copyHtoD(dst.ptr, src.ptr, numBytes);
    }
  }

  void Resource::memset(MemoryEntity dst, char ch, size_t numBytes) {
    if (dst.location.onHost()) {
      zs::memset(mem_host, dst.ptr, ch, numBytes);
      return;
    }
    auto *ops = MemoryBackendRegistry::instance().get_memory_ops(dst.location.memspace());
    if (!ops || !ops->memset)
      throw std::runtime_error("There is no corresponding device backend for Resource::memset");
    ops->memset(dst.ptr, ch, numBytes);
  }

  // -----------------------------------------------------------------------
  // Resource singleton
  // -----------------------------------------------------------------------

  static concurrent_map<void *, Resource::AllocationRecord> g_resource_records;

#if 0
  static Resource g_resource;
  Resource &Resource::instance() noexcept { return g_resource; }
#else
  Resource &Resource::instance() noexcept {
    static Resource *ptr = new Resource();
    return *ptr;
  }
#endif
  std::atomic_ullong &Resource::counter() noexcept { return instance()._counter; }

  Resource::Resource() {
#if 0
    initialize_backend(seq_c);
#  if ZS_ENABLE_OPENMP
    puts("openmp initialized");
    initialize_backend(omp_c);
#  endif
#  if ZS_ENABLE_CUDA
    puts("cuda initialized");
    initialize_backend(cuda_c);
#  elif ZS_ENABLE_MUSA
    puts("musa initialized");
    initialize_backend(musa_c);
#  elif ZS_ENABLE_ROCM
    puts("rocm initialized");
    initialize_backend(rocm_c);
#  elif ZS_ENABLE_SYCL
    puts("sycl initialized");
    initialize_backend(sycl_c);
#  endif
#endif
  }
  Resource::~Resource() {
    for (auto &&record : g_resource_records) {
      const auto &[ptr, info] = record;
      std::cout << "recycling allocation [" << (std::uintptr_t)ptr << "], tag ["
                << match([](auto &tag) { return get_memory_tag_name(tag); })(info.tag)
                << "], size [" << info.size << "], alignment [" << info.alignment
                << "], allocator [" << info.allocatorType << "]\n";
    }
#if 0
#  if ZS_ENABLE_CUDA
    deinitialize_backend(cuda_c);
#  elif ZS_ENABLE_MUSA
    deinitialize_backend(musa_c);
#  elif ZS_ENABLE_ROCM
    deinitialize_backend(rocm_c);
#  elif ZS_ENABLE_SYCL
    deinitialize_backend(sycl_c);
#  endif
#  if ZS_ENABLE_OPENMP
    deinitialize_backend(omp_c);
#  endif
    deinitialize_backend(seq_c);
#endif
  }
  void Resource::record(mem_tags tag, void *ptr, std::string_view name, size_t size,
                        size_t alignment) {
    g_resource_records.set(ptr, AllocationRecord{tag, size, alignment, std::string(name)});
  }
  void Resource::erase(void *ptr) { g_resource_records.erase(ptr); }

  void Resource::deallocate(void *ptr) {
    // Copy the record data before erasing to avoid TOCTOU race.
    // concurrent_map releases its internal lock after each operation, so we must
    // copy the record value before the reference becomes invalid.
    AllocationRecord record;
    try {
      record = g_resource_records.get(ptr);
    } catch (...) {
      std::ostringstream oss;
      oss << "allocation record " << (std::uintptr_t)ptr << " not found in records!";
      throw std::runtime_error(oss.str());
    }
    g_resource_records.erase(ptr);
    match([&record, ptr](auto &tag) { zs::deallocate(tag, ptr, record.size, record.alignment); })(record.tag);
  }

}  // namespace zs
