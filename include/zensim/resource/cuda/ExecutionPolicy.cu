#include "zensim/cuda/execution/ExecutionPolicy.cuh"
#include "zensim/cuda/memory/Allocator.h"

namespace zs {

  ZPC_API ZSPmrAllocator<> get_temporary_memory_source(const CudaExecutionPolicy &pol) {
    ZSPmrAllocator<> ret{};
    ret.res = zs::make_unique<temporary_memory_resource<device_mem_tag>>(&pol.context(),
                                                                         pol.getStream());
    ret.location = MemoryLocation{memsrc_e::device, pol.getProcid()};
    ret.cloner = [stream = pol.getStream(), context = &pol.context()]() -> UniquePtr<mr_t> {
      UniquePtr<mr_t> ret{};
      ret = zs::make_unique<temporary_memory_resource<device_mem_tag>>(context, stream);
      return ret;
    };
    return ret;
  }

}  // namespace zs