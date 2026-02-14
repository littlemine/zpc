#include "MemOps.hpp"

#include <cuda.h>

#include <iostream>
#include <sstream>

#include "zensim/Logger.hpp"
#include "zensim/cuda/Cuda.h"

namespace zs {

  bool prepare_context(device_mem_tag, ProcID did, const source_location &loc) {
    CUcontext ctx = nullptr;
    auto ec = cuCtxGetCurrent(&ctx);
    if (ec != CUDA_SUCCESS) {
      const char *errString = nullptr;
      cuGetErrorString(ec, &errString);
      checkCuApiError((u32)ec, loc, "[cuCtxGetCurrent]", errString);
      return false;
    } else {
      if (did < 0) did = Cuda::get_default_device();
      int devid = did;
      if (ctx != NULL) {
        auto ec = cuCtxGetDevice(&devid);
        if (ec != CUDA_SUCCESS) {
          const char *errString = nullptr;
          cuGetErrorString(ec, &errString);
          checkCuApiError((u32)ec, loc, "[cuCtxGetDevice]", errString);
          return false;
        }
      }  // otherwise, no context has been initialized yet.

      if (ctx == NULL || devid != did) {
        {
          std::ostringstream oss;
          oss << "context switching during (de)allocation of [tag ["
              << get_memory_tag_name(device_mem_tag{}) << "] @ device [" << (int)did << "]]";
          ZS_WARN(oss.str());
        }
        if (did < Cuda::device_count() && did >= 0)
          Cuda::context(did).setContext();
        else {
          std::ostringstream oss;
          oss << "current binding device [" << devid
              << "] does not match the expected [" << (int)did
              << "] and failed to switch context.";
          throw std::runtime_error(oss.str());
        }
      }
    }
    return true;
  }
  void *allocate(device_mem_tag, size_t size, size_t alignment, const source_location &loc) {
    void *ret{nullptr};
    [[maybe_unused]] auto ec = cuMemAlloc((CUdeviceptr *)&ret, size);
#if ZS_ENABLE_OFB_ACCESS_CHECK
    cudaDeviceSynchronize();
    // checkCuApiError((u32)ec, loc, "[cuMemAlloc]", errString);
    if (ec != CUDA_SUCCESS) {
      const char *errString = nullptr;
      cuGetErrorString(ec, &errString);
      int devid;
      cuCtxGetDevice(&devid);
      std::cerr << "\nCuda Error on Device " << devid
                << ": cuMemAlloc failed: " << errString
                << " (size: " << size << " bytes, alignment: " << alignment << " bytes)"
                << "\n============================================================\n"
                << "# File: \"" << loc.file_name() << "\"\n"
                << "# Ln " << loc.line() << ", Col " << loc.column() << "\n"
                << "# Func: \"" << loc.function_name() << "\"\n"
                << "============================================================\n\n";
    }
#endif
    return ret;
  }

  void deallocate(device_mem_tag, void *ptr, size_t size, size_t alignment,
                  const source_location &loc) {
    cuMemFree((CUdeviceptr)ptr);
  }

  void memset(device_mem_tag, void *addr, int chval, size_t size, const source_location &loc) {
    /// @note this is asynchronous with respect to host, cuz of using 'per-thread' default stream!!!
    cuMemsetD8((CUdeviceptr)addr, chval, size);
    // cudaDeviceSynchronize();
  }
  void copy(device_mem_tag, void *dst, void *src, size_t size, const source_location &loc) {
    /// @note
    /// https://docs.nvidia.com/cuda/cuda-driver-api/api-sync-behavior.html#api-sync-behavior__memcpy-sync
    cuMemcpy((CUdeviceptr)dst, (CUdeviceptr)src, size);
    // cudaDeviceSynchronize();
  }
  void copyHtoD(device_mem_tag, void *dst, void *src, size_t size, const source_location &loc) {
    cuMemcpyHtoD((CUdeviceptr)dst, (void *)src, size);
    // cudaDeviceSynchronize();
  }
  void copyDtoH(device_mem_tag, void *dst, void *src, size_t size, const source_location &loc) {
    cuMemcpyDtoH((void *)dst, (CUdeviceptr)src, size);
    // cudaDeviceSynchronize();
  }
  void copyDtoD(device_mem_tag, void *dst, void *src, size_t size, const source_location &loc) {
    cuMemcpyDtoD((CUdeviceptr)dst, (CUdeviceptr)src, size);
    // cudaDeviceSynchronize();
  }

  bool prepare_context(um_mem_tag, ProcID did, const source_location &loc) {
    CUcontext ctx = nullptr;
    auto ec = cuCtxGetCurrent(&ctx);
    if (ec != CUDA_SUCCESS) {
      const char *errString = nullptr;
      cuGetErrorString(ec, &errString);
      checkCuApiError((u32)ec, loc, "[cuCtxGetCurrent]", errString);
      return false;
    } else {
      if (did < 0) did = Cuda::get_default_device();
      int devid = did;
      if (ctx != NULL) {
        auto ec = cuCtxGetDevice(&devid);
        if (ec != CUDA_SUCCESS) {
          const char *errString = nullptr;
          cuGetErrorString(ec, &errString);
          checkCuApiError((u32)ec, loc, "[cuCtxGetDevice]", errString);
          return false;
        }
      }  // otherwise, no context has been initialized yet.

      if (ctx == NULL || devid != did) {
        {
          std::ostringstream oss;
          oss << "context switching during (de)allocation of [tag ["
              << get_memory_tag_name(um_mem_tag{}) << "] @ device [" << (int)did << "]]";
          ZS_WARN(oss.str());
        }
        if (did < Cuda::device_count() && did >= 0)
          Cuda::context(did).setContext();
        else {
          std::ostringstream oss;
          oss << "current binding device [" << devid
              << "] does not match the expected [" << (int)did
              << "] and failed to switch context.";
          throw std::runtime_error(oss.str());
        }
      }
    }
    return true;
  }
  void *allocate(um_mem_tag, size_t size, size_t alignment, const source_location &loc) {
    void *ret{nullptr};
    // cudri::umalloc(&ret, size, 0x1, loc);  //(unsigned int)CU_MEM_ATTACH_GLOBAL);
    [[maybe_unused]] auto ec = cuMemAllocManaged((CUdeviceptr *)&ret, size, CU_MEM_ATTACH_GLOBAL);
#if ZS_ENABLE_OFB_ACCESS_CHECK
    cudaDeviceSynchronize();
    if (ec != CUDA_SUCCESS) {
      const char *errString = nullptr;
      cuGetErrorString(ec, &errString);
      int devid;
      cuCtxGetDevice(&devid);
      std::cerr << "\nCuda Error on Device " << devid
                << ": cuMemAllocManaged failed: " << errString
                << " (size: " << size << " bytes, alignment: " << alignment << " bytes)"
                << "\n============================================================\n"
                << "# File: \"" << loc.file_name() << "\"\n"
                << "# Ln " << loc.line() << ", Col " << loc.column() << "\n"
                << "# Func: \"" << loc.function_name() << "\"\n"
                << "============================================================\n\n";
    }
#endif
    return ret;
  }
  void deallocate(um_mem_tag, void *ptr, size_t size, size_t alignment,
                  const source_location &loc) {
    cuMemFree((CUdeviceptr)ptr);
  }
  void memset(um_mem_tag, void *addr, int chval, size_t size, const source_location &loc) {
    cuMemsetD8((CUdeviceptr)addr, chval, size);
  }
  void copy(um_mem_tag, void *dst, void *src, size_t size, const source_location &loc) {
    cuMemcpy((CUdeviceptr)dst, (CUdeviceptr)src, size);
  }
  void copyHtoD(um_mem_tag, void *dst, void *src, size_t size, const source_location &loc) {
    cuMemcpyHtoD((CUdeviceptr)dst, (void *)src, size);
  }
  void copyDtoH(um_mem_tag, void *dst, void *src, size_t size, const source_location &loc) {
    cuMemcpyDtoH((void *)dst, (CUdeviceptr)src, size);
  }
  void copyDtoD(um_mem_tag, void *dst, void *src, size_t size, const source_location &loc) {
    cuMemcpyDtoD((CUdeviceptr)dst, (CUdeviceptr)src, size);
  }
  void advise(um_mem_tag, std::string advice, void *addr, size_t bytes, ProcID did,
              const source_location &loc) {
    unsigned int option{};  // CUmem_advise
    if (advice == "ACCESSED_BY")
      option = 5;  // CU_MEM_ADVISE_SET_ACCESSED_BY;
    else if (advice == "PREFERRED_LOCATION")
      option = 3;  // CU_MEM_ADVISE_SET_PREFERRED_LOCATION;
    else if (advice == "READ_MOSTLY")
      option = 1;  // CU_MEM_ADVISE_SET_READ_MOSTLY;
    else {
      std::ostringstream oss;
      oss << "advise(tag um_mem_tag, advice " << advice << ", addr " << addr << ", bytes " << bytes << ", devid " << (int)did << ")\n";
      throw std::runtime_error(oss.str());
    }
    if (Cuda::context(did).supportConcurrentUmAccess)
      if (bytes > 0) {
#if CUDA_VERSION >= 12020
        // CUDA 12.2+ uses cuMemAdvise_v2 with CUmemLocation
        CUmemLocation location = {};
        location.type = CU_MEM_LOCATION_TYPE_DEVICE;
        location.id = (int)did;
        cuMemAdvise_v2((CUdeviceptr)addr, bytes, (CUmem_advise)option, location);
#else
        cuMemAdvise((CUdeviceptr)addr, bytes, (CUmem_advise)option, (int)did);
#endif
      }
  }

}  // namespace zs