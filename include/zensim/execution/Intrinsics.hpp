#pragma once
/// be cautious to include this header
/// to enable cuda compiler, include cuda header before this one

/// use these functions within other templated function (1) or in a source file (2)
/// (1)
/// REMEMBER! Make Sure Their Specializations Done In the Correct Compiler Context!
/// which is given a certain execution policy tag, necessary headers are to be included
/// (2)
/// inside a certain source file

#include "zensim/execution/ExecutionPolicy.hpp"
#include "zensim/math/bit/Bits.h"
#if defined(_WIN32)
#  include <intrin.h>
#  include <stdlib.h>
// #  include <windows.h>
// #  include <synchapi.h>

#elif defined(__linux__)
#  include <immintrin.h>
#  include <linux/futex.h>
#  include <sys/syscall.h> /* Definition of SYS_* constants */
#  include <unistd.h>
#endif

namespace zs {

  // __threadfence
#if defined(__CUDACC__)
  __forceinline__ __device__ void thread_fence(cuda_exec_tag) { __threadfence(); }
#endif

#if defined(_OPENMP)
  inline void thread_fence(omp_exec_tag) noexcept {
    /// a thread is guaranteed to see a consistent view of memory with respect to the variables in “
    /// list ”
#  pragma omp flush
  }
#endif

  inline void thread_fence(host_exec_tag) noexcept {}

  // __syncthreads
#if defined(__CUDACC__)
  __forceinline__ __device__ void sync_threads(cuda_exec_tag) { __syncthreads(); }
#endif

#if defined(_OPENMP)
  inline void sync_threads(omp_exec_tag) noexcept {
#  pragma omp barrier
  }
#endif

  inline void sync_threads(host_exec_tag) noexcept {}

  // pause
  template <typename ExecTag = host_exec_tag,
            enable_if_t<is_same_v<ExecTag, omp_exec_tag> || is_same_v<ExecTag, host_exec_tag>> = 0>
  inline void pause_cpu(ExecTag = {}) {
#if defined(_MSC_VER) || (defined(_WIN32) && defined(__INTEL_COMPILER))
    YieldProcessor();
#elif defined(__clang__) || defined(__GNUC__)
    _mm_pause();
#endif
  }

  // __activemask
#if defined(__CUDACC__)
  __forceinline__ __device__ unsigned active_mask(cuda_exec_tag) { return __activemask(); }
#endif

  // __ballot_sync
#if defined(__CUDACC__)
  __forceinline__ __device__ unsigned ballot_sync(cuda_exec_tag, unsigned mask, int predicate) {
    return __ballot_sync(mask, predicate);
  }
#endif

  // ref: https://graphics.stanford.edu/~seander/bithacks.html

  /// count leading zeros
#if defined(__CUDACC__)
  template <typename T> __forceinline__ __device__ int count_lz(cuda_exec_tag, T x) {
    constexpr auto nbytes = sizeof(T);
    if constexpr (sizeof(int) == nbytes)
      return __clz((int)x);
    else if constexpr (sizeof(long long int) == nbytes)
      return __clzll((long long int)x);
    else {
      static_assert(sizeof(long long int) != nbytes || sizeof(int) != nbytes,
                    "count_lz(tag CUDA, [?] bytes) not viable\n");
    }
    return -1;
  }
#endif

  template <typename ExecTag, typename T,
            enable_if_t<is_same_v<ExecTag, omp_exec_tag> || is_same_v<ExecTag, host_exec_tag>> = 0>
  inline int count_lz(ExecTag, T x) {
    constexpr auto nbytes = sizeof(T);
    if (x == (T)0) return nbytes * 8;
#if defined(_MSC_VER) || (defined(_WIN32) && defined(__INTEL_COMPILER))
    if constexpr (sizeof(unsigned short) == nbytes)
      return __lzcnt16((unsigned short)x);
    else if constexpr (sizeof(unsigned int) == nbytes)
      return __lzcnt((unsigned int)x);
    else if constexpr (sizeof(unsigned __int64) == nbytes)
      return __lzcnt64((unsigned __int64)x);
#elif defined(__clang__) || defined(__GNUC__)
    if constexpr (sizeof(unsigned int) == nbytes)
      return __builtin_clz((unsigned int)x);
    else if constexpr (sizeof(unsigned long) == nbytes)
      return __builtin_clzl((unsigned long)x);
    else if constexpr (sizeof(unsigned long long) == nbytes)
      return __builtin_clzll((unsigned long long)x);
#endif
    throw std::runtime_error(fmt::format("count_lz(tag {}, {} bytes) not viable\n",
                                         get_execution_tag_name(ExecTag{}), sizeof(T)));
  }

  /// reverse bits
#if defined(__CUDACC__)
  template <typename T> __forceinline__ __device__ T reverse_bits(cuda_exec_tag, T x) {
    constexpr auto nbytes = sizeof(T);
    if constexpr (sizeof(unsigned int) == nbytes)
      return __brev((unsigned int)x);
    else if constexpr (sizeof(unsigned long long int) == nbytes)
      return __brevll((unsigned long long int)x);
    else
      static_assert(sizeof(unsigned long long int) != nbytes || sizeof(unsigned int) != nbytes,
                    "reverse_bits(tag [?], [?] bytes) not viable\n");
    return x;
  }
#endif

  template <typename ExecTag, typename T,
            enable_if_t<is_same_v<ExecTag, omp_exec_tag> || is_same_v<ExecTag, host_exec_tag>> = 0>
  inline T reverse_bits(ExecTag, T x) {
    constexpr auto nbytes = sizeof(T);
    if (x == (T)0) return 0;
    using Val = std::make_unsigned_t<T>;
    Val tmp{}, ret{0};
#if defined(_MSC_VER) || (defined(_WIN32) && defined(__INTEL_COMPILER))
    if constexpr (sizeof(unsigned short) == nbytes)
      tmp = (Val)_byteswap_ushort((unsigned short)x);
    else if constexpr (sizeof(unsigned long) == nbytes)
      tmp = (Val)_byteswap_ulong((unsigned long)x);
    else if constexpr (sizeof(unsigned __int64) == nbytes)
      tmp = (Val)_byteswap_uint64((unsigned __int64)x);
#elif defined(__clang__) || defined(__GNUC__)
    if constexpr (sizeof(unsigned short) == nbytes)
      tmp = (Val)__builtin_bswap16((unsigned short)x);
    else if constexpr (sizeof(unsigned int) == nbytes)
      tmp = (Val)__builtin_bswap32((unsigned int)x);
    else if constexpr (sizeof(unsigned long long) == nbytes)
      tmp = (Val)__builtin_bswap64((unsigned long long)x);
#endif
    else
      throw std::runtime_error(fmt::format("reverse_bits(tag {}, {} bytes) not viable\n",
                                           get_execution_tag_name(ExecTag{}), sizeof(T)));
    // reverse within each byte
    for (int bitoffset = 0; tmp; bitoffset += 8) {
      unsigned char b = tmp & 0xff;
      b = ((u64)b * 0x0202020202ULL & 0x010884422010ULL) % 1023;
      ret |= ((Val)b << bitoffset);
      tmp >>= 8;
    }
    return (T)ret;
  }

}  // namespace zs