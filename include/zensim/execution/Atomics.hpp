#pragma once

#if 0
#  include <atomic>

#  include "zensim/execution/ExecutionPolicy.hpp"
#  include "zensim/math/bit/Bits.h"
#  if defined(_WIN32)
#    include <windows.h>
// # include <winnt.h>
#  endif

#else
#  if defined(_MSC_VER) || (defined(_WIN32) && defined(__INTEL_COMPILER))
#    include <windows.h>
// # include <winnt.h>
#  endif
#  include "zensim/ZpcIntrinsics.hpp"
#  include "zensim/execution/ConcurrencyPrimitive.hpp"
#  include "zensim/types/Property.h"
#endif

namespace zs {

  enum memory_order {
    memory_order_relaxed,
    memory_order_consume,
    memory_order_acquire,
    memory_order_release,
    memory_order_acq_rel,
    memory_order_seq_cst
  };

  /// reference: raja/include/RAJA/policy/atomic_builtin.hpp: BuiltinAtomicCAS

#if defined(__CUDACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, cuda_exec_tag>, T> atomic_add(
      ExecTag, T *dest, const T val) {
    if constexpr (is_same_v<T, double>) {
#  if __CUDA_ARCH__ >= 600
      /// @note use native implementation if available
      return atomicAdd(dest, val);
#  else
      /// @note fallback to manual implementation
      unsigned long long int *address_as_ull = (unsigned long long int *)dest;
      unsigned long long int old = *address_as_ull, assumed;

      do {
        assumed = old;
        old = atomicCAS(address_as_ull, assumed,
                        __double_as_longlong(val + __longlong_as_double(assumed)));

        // Note: uses integer comparison to avoid hang in case of NaN (since NaN != NaN)
      } while (assumed != old);
      return __longlong_as_double(old);
#  endif
    } else
      return atomicAdd(dest, val);
  }
#endif

#if defined(__MUSACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, musa_exec_tag>, T> atomic_add(
      ExecTag, T *dest, const T val) {
    return atomicAdd(dest, val);
  }
#endif

  template <typename ExecTag, typename T>
  inline enable_if_type<is_host_execution_tag<ExecTag>(), T> atomic_add_impl(ExecTag, T *dest,
                                                                             const T val) {
    using TT = conditional_t<sizeof(T) == 2, u16, conditional_t<sizeof(T) == 4, u32, u64>>;
    static_assert(sizeof(T) == sizeof(TT));
    TT oldVal{reinterpret_bits<TT>(*const_cast<volatile T *>(dest))};
    TT newVal{reinterpret_bits<TT>(reinterpret_bits<T>(oldVal) + val)}, readVal{};
    while ((readVal = atomic_cas(ExecTag{}, (TT *)dest, oldVal, newVal)) != oldVal) {
      oldVal = readVal;
      newVal = reinterpret_bits<TT>(reinterpret_bits<T>(readVal) + val);
    }
    return reinterpret_bits<T>(oldVal);
  }

  template <typename ExecTag, typename T>
  inline enable_if_type<is_host_execution_tag<ExecTag>(), T> atomic_add(ExecTag, T *dest,
                                                                        const T val) {
    if constexpr (is_same_v<ExecTag, seq_exec_tag>) {
      const T old = *dest;
      *dest += val;
      return old;
    } else {
#if 1
#  if defined(_MSC_VER) || (defined(_WIN32) && defined(__INTEL_COMPILER))
      if constexpr (is_integral_v<T>) {
        if constexpr (sizeof(T) == sizeof(char))
          return InterlockedExchangeAdd8(const_cast<char volatile *>((char *)dest), (char)val);
        else if constexpr (sizeof(T) == sizeof(short))
          return InterlockedExchangeAdd16(const_cast<short volatile *>((short *)dest), (short)val);
        else if constexpr (sizeof(T) == sizeof(long))
          return InterlockedExchangeAdd(const_cast<long volatile *>((long *)dest), (long)val);
        else if constexpr (sizeof(T) == sizeof(__int64))
          return InterlockedExchangeAdd64(const_cast<__int64 volatile *>((__int64 *)dest),
                                          (__int64)val);
      } else
        return atomic_add_impl(ExecTag{}, dest, val);
#  else
      if constexpr (is_integral_v<T>)
        return __atomic_fetch_add(dest, val, __ATOMIC_SEQ_CST);
      else
        return atomic_add_impl(ExecTag{}, dest, val);
#  endif

#else
      /// introduced in c++20
      std::atomic_ref<T> target{const_cast<T &>(*dest)};
      return target.fetch_add(val, std::memory_order_seq_cst);
#endif
    }
  }

  // https://developer.nvidia.com/blog/cuda-pro-tip-optimized-filtering-warp-aggregated-atomics/
#if defined(__CUDACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, cuda_exec_tag>, T> atomic_inc(
      ExecTag, T *dest) {
    if constexpr (is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)) {
      unsigned int active = __activemask();
      int leader = __ffs(active) - 1;
      int change = __popc(active);
      // https://stackoverflow.com/questions/44337309/whats-the-most-efficient-way-to-calculate-the-warp-id-lane-id-in-a-1-d-grid
      unsigned int rank = __popc(active & ((1 << (threadIdx.x & 31)) - 1));
      T warp_res;
      if (rank == 0) warp_res = atomicAdd(dest, (T)change);
      warp_res = __shfl_sync(active, warp_res, leader);
      return warp_res + rank;
    } else
      return atomic_add(ExecTag{}, dest, (T)1);
  }
#endif

#if defined(__MUSACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, musa_exec_tag>, T> atomic_inc(
      ExecTag, T *dest) {
    if constexpr (is_integral_v<T> && (sizeof(T) == 4 || sizeof(T) == 8)) {
      unsigned int active = __activemask();
      int leader = __ffs(active) - 1;
      int change = __popc(active);
      // https://stackoverflow.com/questions/44337309/whats-the-most-efficient-way-to-calculate-the-warp-id-lane-id-in-a-1-d-grid
      unsigned int rank = __popc(active & ((1 << (threadIdx.x & 31)) - 1));
      T warp_res;
      if (rank == 0) warp_res = atomicAdd(dest, (T)change);
      warp_res = __shfl_sync(active, warp_res, leader);
      return warp_res + rank;
    } else
      return atomic_add(ExecTag{}, dest, (T)1);
  }
#endif

  template <typename ExecTag, typename T>
  inline enable_if_type<is_host_execution_tag<ExecTag>(), T> atomic_inc(ExecTag, T *dest) {
    return atomic_add(ExecTag{}, dest, (T)1);
  }

  ///
  /// exch, cas
  ///
#if defined(__CUDACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, cuda_exec_tag>, T> atomic_exch(
      ExecTag, T *dest, const T val) {
    return atomicExch(dest, val);
  }
#endif
#if defined(__MUSACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, musa_exec_tag>, T> atomic_exch(
      ExecTag, T *dest, const T val) {
    return atomicExch(dest, val);
  }
#endif
  template <typename ExecTag, typename T>
  inline enable_if_type<is_host_execution_tag<ExecTag>(), T> atomic_exch(ExecTag, T *dest,
                                                                         const T val) {
    if constexpr (is_same_v<ExecTag, seq_exec_tag>) {
      const T old = *dest;
      *dest = val;
      return old;
    } else {
#if 1
#  if defined(_MSC_VER) || (defined(_WIN32) && defined(__INTEL_COMPILER))
      if constexpr (sizeof(T) == sizeof(char))
        return InterlockedExchange8(const_cast<char volatile *>((char *)dest), (char)val);
      else if constexpr (sizeof(T) == sizeof(short))
        return InterlockedExchange16(const_cast<short volatile *>((short *)dest), (short)val);
      else if constexpr (sizeof(T) == sizeof(long))
        return InterlockedExchange(const_cast<long volatile *>((long *)dest), (long)val);
      else if constexpr (sizeof(T) == sizeof(__int64))
        return InterlockedExchange64(const_cast<__int64 volatile *>((__int64 *)dest), (__int64)val);
      else
        static_assert(always_false<ExecTag>, "no corresponding parallel atomic_exch (win) impl!");
#  else
      return __atomic_exchange_n(dest, val, __ATOMIC_SEQ_CST);
#  endif
#else
      std::atomic_ref<T> target{const_cast<T &>(*dest)};
      return target.exchange(val, std::memory_order_seq_cst);
#endif
    }
  }

#if defined(__CUDACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, cuda_exec_tag>, T> atomic_cas(
      ExecTag, T *dest, T expected, T desired) {
    if constexpr (is_same_v<T, float> && sizeof(int) == sizeof(T))
      return reinterpret_bits<float>(atomicCAS((unsigned int *)dest,
                                               reinterpret_bits<unsigned int>(expected),
                                               reinterpret_bits<unsigned int>(desired)));
    else if constexpr (is_same_v<T, double> && sizeof(unsigned long long int) == sizeof(T))
      return reinterpret_bits<double>(atomicCAS((unsigned long long int *)dest,
                                                reinterpret_bits<unsigned long long int>(expected),
                                                reinterpret_bits<unsigned long long int>(desired)));
    else
      return atomicCAS(dest, expected, desired);
  }
#endif
#if defined(__MUSACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, musa_exec_tag>, T> atomic_cas(
      ExecTag, T *dest, T expected, T desired) {
    if constexpr (is_same_v<T, float> && sizeof(int) == sizeof(T))
      return reinterpret_bits<float>(atomicCAS((unsigned int *)dest,
                                               reinterpret_bits<unsigned int>(expected),
                                               reinterpret_bits<unsigned int>(desired)));
    else if constexpr (is_same_v<T, double> && sizeof(unsigned long long int) == sizeof(T))
      return reinterpret_bits<double>(atomicCAS((unsigned long long int *)dest,
                                                reinterpret_bits<unsigned long long int>(expected),
                                                reinterpret_bits<unsigned long long int>(desired)));
    else
      return atomicCAS(dest, expected, desired);
  }
#endif
  template <typename ExecTag, typename T>
  inline enable_if_type<is_host_execution_tag<ExecTag>(), T> atomic_cas(ExecTag, T *dest,
                                                                        T expected, T desired) {
    if constexpr (is_same_v<ExecTag, seq_exec_tag>) {
      const T old = *dest;
      if (old == expected) *dest = desired;
      return old;
    } else {
#if 1
#  if defined(_MSC_VER) || (defined(_WIN32) && defined(__INTEL_COMPILER))
      if constexpr (sizeof(T) == sizeof(char)) {  // 8-bit
        return reinterpret_bits<T>(_InterlockedCompareExchange8(
            const_cast<volatile char *>((char *)dest), reinterpret_bits<char>(desired),
            reinterpret_bits<char>(expected)));
      } else if constexpr (sizeof(T) == sizeof(short)) {  // 16-bit
        return reinterpret_bits<T>(_InterlockedCompareExchange16(
            const_cast<volatile short *>((short *)dest), reinterpret_bits<short>(desired),
            reinterpret_bits<short>(expected)));
      } else if constexpr (sizeof(T) == sizeof(long)) {  // 32-bit
        return reinterpret_bits<T>(_InterlockedCompareExchange(
            const_cast<volatile long *>((long *)dest), reinterpret_bits<long>(desired),
            reinterpret_bits<long>(expected)));
      } else if constexpr (sizeof(T) == sizeof(__int64)) {
        return reinterpret_bits<T>(InterlockedCompareExchange64(
            const_cast<volatile __int64 *>((__int64 *)dest), reinterpret_bits<__int64>(desired),
            reinterpret_bits<__int64>(expected)));
      } else {
        static_assert(always_false<ExecTag>, "no corresponding parallel atomic_cas (win) impl!");
      }
#  else
      if constexpr (is_same_v<T, float> && sizeof(int) == sizeof(T)) {
        int expected_ = reinterpret_bits<int>(expected);
        __atomic_compare_exchange_n(const_cast<int volatile *>((int *)dest), &expected_,
                                    reinterpret_bits<int>(desired), false, __ATOMIC_ACQ_REL,
                                    __ATOMIC_RELAXED);
        return reinterpret_bits<T>(expected_);
      } else if constexpr (is_same_v<T, double> && sizeof(long long) == sizeof(T)) {
        long long expected_ = reinterpret_bits<long long>(expected);
        __atomic_compare_exchange_n(const_cast<long long volatile *>((long long *)dest), &expected_,
                                    reinterpret_bits<long long>(desired), false, __ATOMIC_ACQ_REL,
                                    __ATOMIC_RELAXED);
        return reinterpret_bits<T>(expected_);
      } else {
        __atomic_compare_exchange_n(const_cast<T volatile *>(dest), &expected, desired, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
        return expected;
      }
#  endif

#else
      std::atomic_ref<T> target{const_cast<T &>(*dest)};
      return target.compare_exchange_strong(expected, desired, std::memory_order_seq_cst);
#endif
    }
  }

  ///
  /// min/ max operations
  ///
  // https://github.com/NVIDIA-developer-blog/code-samples/blob/master/posts/cuda-aware-mpi-example/src/Device.cu
  // https://herbsutter.com/2012/08/31/reader-qa-how-to-write-a-cas-loop-using-stdatomics/
#if defined(__CUDACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, cuda_exec_tag>, T> atomic_max(
      ExecTag execTag, T *const dest, const T val) {
    if constexpr (is_integral_v<T>) {
      return atomicMax(dest, val);
    } else {
      T old = *dest;
      for (T assumed = old;
           assumed < val && (old = atomic_cas(execTag, dest, assumed, val)) != assumed;
           assumed = old);
      return old;
    }
  }
#endif
#if defined(__MUSACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, musa_exec_tag>, T> atomic_max(
      ExecTag execTag, T *const dest, const T val) {
    if constexpr (is_integral_v<T>) {
      return atomicMax(dest, val);
    } else {
      T old = *dest;
      for (T assumed = old;
           assumed < val && (old = atomic_cas(execTag, dest, assumed, val)) != assumed;
           assumed = old);
      return old;
    }
  }
#endif
  template <typename ExecTag, typename T>
  inline enable_if_type<is_host_execution_tag<ExecTag>(), T> atomic_max(ExecTag execTag,
                                                                        T *const dest,
                                                                        const T val) {
    if constexpr (is_same_v<ExecTag, seq_exec_tag>) {
      const T old = *dest;
      if (old < val) *dest = val;
      return old;
    } else {
      T old = *dest;
      for (T assumed = old;
           assumed < val && (old = atomic_cas(execTag, dest, assumed, val)) != assumed;
           assumed = old);
      return old;
    }
  }

#if defined(__CUDACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, cuda_exec_tag>, T> atomic_min(
      ExecTag execTag, T *const dest, const T val) {
    if constexpr (is_integral_v<T>) {
      return atomicMin(dest, val);
    } else {
      T old = *dest;
      for (T assumed = old;
           assumed > val && (old = atomic_cas(execTag, dest, assumed, val)) != assumed;
           assumed = old);
      return old;
    }
  }
#endif
#if defined(__MUSACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, musa_exec_tag>, T> atomic_min(
      ExecTag execTag, T *const dest, const T val) {
    if constexpr (is_integral_v<T>) {
      return atomicMin(dest, val);
    } else {
      T old = *dest;
      for (T assumed = old;
           assumed > val && (old = atomic_cas(execTag, dest, assumed, val)) != assumed;
           assumed = old);
      return old;
    }
  }
#endif
  template <typename ExecTag, typename T>
  inline enable_if_type<is_host_execution_tag<ExecTag>(), T> atomic_min(ExecTag execTag,
                                                                        T *const dest,
                                                                        const T val) {
    if constexpr (is_same_v<ExecTag, seq_exec_tag>) {
      const T old = *dest;
      if (old > val) *dest = val;
      return old;
    } else {
      T old = *dest;
      for (T assumed = old;
           assumed > val && (old = atomic_cas(execTag, dest, assumed, val)) != assumed;
           assumed = old);
      return old;
    }
  }

  ///
  /// bit-wise operations
  ///
#if defined(__CUDACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, cuda_exec_tag>, T> atomic_or(
      ExecTag, T *dest, const T val) {
    static_assert(ZS_ENABLE_CUDA, "ZS_ENABLE_CUDA must be set to enable cuda-backend atomic_or!");
    return atomicOr(dest, val);
  }
#endif
#if defined(__MUSACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, musa_exec_tag>, T> atomic_or(
      ExecTag, T *dest, const T val) {
    static_assert(ZS_ENABLE_MUSA, "ZS_ENABLE_MUSA must be set to enable musa-backend atomic_or!");
    return atomicOr(dest, val);
  }
#endif
  template <typename ExecTag, typename T>
  inline enable_if_type<is_host_execution_tag<ExecTag>(), T> atomic_or(ExecTag, T *dest,
                                                                       const T val) {
    if constexpr (is_same_v<ExecTag, seq_exec_tag>) {
      const T old = *dest;
      *dest |= val;
      return old;
    } else {
#if 1

#  if defined(_MSC_VER) || (defined(_WIN32) && defined(__INTEL_COMPILER))
      if constexpr (sizeof(T) == sizeof(char))
        return InterlockedOr8(const_cast<char volatile *>((char *)dest), (char)val);
      else if constexpr (sizeof(T) == sizeof(short))
        return InterlockedOr16(const_cast<short volatile *>((short *)dest), (short)val);
      else if constexpr (sizeof(T) == sizeof(long))
        return InterlockedOr(const_cast<long volatile *>((long *)dest), (long)val);
      else if constexpr (sizeof(T) == sizeof(__int64))
        return InterlockedOr64(const_cast<__int64 volatile *>((__int64 *)dest), (__int64)val);
#  else
      return __atomic_fetch_or(dest, val, __ATOMIC_SEQ_CST);
#  endif

#else
      std::atomic_ref<T> target{const_cast<T &>(*dest)};
      return target.fetch_or(val, std::memory_order_seq_cst);
#endif
    }
  }

#if defined(__CUDACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, cuda_exec_tag>, T> atomic_and(
      ExecTag, T *dest, const T val) {
    static_assert(ZS_ENABLE_CUDA, "ZS_ENABLE_CUDA must be set to enable cuda-backend atomic_and!");
    return atomicAnd(dest, val);
  }
#endif
#if defined(__MUSACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, musa_exec_tag>, T> atomic_and(
      ExecTag, T *dest, const T val) {
    static_assert(ZS_ENABLE_MUSA, "ZS_ENABLE_MUSA must be set to enable musa-backend atomic_and!");
    return atomicAnd(dest, val);
  }
#endif
  template <typename ExecTag, typename T>
  inline enable_if_type<is_host_execution_tag<ExecTag>(), T> atomic_and(ExecTag, T *dest,
                                                                        const T val) {
    if constexpr (is_same_v<ExecTag, seq_exec_tag>) {
      const T old = *dest;
      *dest &= val;
      return old;
    } else {
#if 1
#  if defined(_MSC_VER) || (defined(_WIN32) && defined(__INTEL_COMPILER))
      if constexpr (sizeof(T) == sizeof(char))
        return InterlockedAnd8(const_cast<char volatile *>((char *)dest), (char)val);
      else if constexpr (sizeof(T) == sizeof(short))
        return InterlockedAnd16(const_cast<short volatile *>((short *)dest), (short)val);
      else if constexpr (sizeof(T) == sizeof(long))
        return InterlockedAnd(const_cast<long volatile *>((long *)dest), (long)val);
      else if constexpr (sizeof(T) == sizeof(__int64))
        return InterlockedAnd64(const_cast<__int64 volatile *>((__int64 *)dest), (__int64)val);
#  else
      return __atomic_fetch_and(dest, val, __ATOMIC_SEQ_CST);
#  endif
#else
      std::atomic_ref<T> target{const_cast<T &>(*dest)};
      return target.fetch_and(val, std::memory_order_seq_cst);
#endif
    }
  }

#if defined(__CUDACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, cuda_exec_tag>, T> atomic_xor(
      ExecTag, T *dest, const T val) {
    static_assert(ZS_ENABLE_CUDA, "ZS_ENABLE_CUDA must be set to enable cuda-backend atomic_xor!");
    return atomicXor(dest, val);
  }
#endif

#if defined(__MUSACC__)
  template <typename ExecTag, typename T>
  __forceinline__ __device__ enable_if_type<is_same_v<ExecTag, musa_exec_tag>, T> atomic_xor(
      ExecTag, T *dest, const T val) {
    static_assert(ZS_ENABLE_MUSA, "ZS_ENABLE_MUSA must be set to enable musa-backend atomic_xor!");
    return atomicXor(dest, val);
  }
#endif

  template <typename ExecTag, typename T>
  inline enable_if_type<is_host_execution_tag<ExecTag>(), T> atomic_xor(ExecTag, T *dest,
                                                                        const T val) {
    if constexpr (is_same_v<ExecTag, seq_exec_tag>) {
      const T old = *dest;
      *dest ^= val;
      return old;
    } else {
#if 1
#  if defined(_MSC_VER) || (defined(_WIN32) && defined(__INTEL_COMPILER))
      if constexpr (sizeof(T) == sizeof(char))
        return InterlockedXor8(const_cast<char volatile *>((char *)dest), (char)val);
      else if constexpr (sizeof(T) == sizeof(short))
        return InterlockedXor16(const_cast<short volatile *>((short *)dest), (short)val);
      else if constexpr (sizeof(T) == sizeof(long))
        return InterlockedXor(const_cast<long volatile *>((long *)dest), (long)val);
      else if constexpr (sizeof(T) == sizeof(__int64))
        return InterlockedXor64(const_cast<__int64 volatile *>((__int64 *)dest), (__int64)val);
#  else
      return __atomic_fetch_xor(dest, val, __ATOMIC_SEQ_CST);
#  endif
#else
      std::atomic_ref<T> target{const_cast<T &>(*dest)};
      return target.fetch_xor(val, std::memory_order_seq_cst);
#endif
    }
  }

  template <typename ExecTag, typename T>
  inline T atomic_load(ExecTag, const T *src) {
    if constexpr (is_same_v<ExecTag, seq_exec_tag>) {
      return *src;
    } else {
      auto *mutableSrc = const_cast<T *>(src);
      T expected = *const_cast<const volatile T *>(src);
      return atomic_cas(ExecTag{}, mutableSrc, expected, expected);
    }
  }

  template <typename ExecTag, typename T>
  inline void atomic_store(ExecTag, T *dest, T value) {
    if constexpr (is_same_v<ExecTag, seq_exec_tag>)
      *dest = value;
    else
      (void)atomic_exch(ExecTag{}, dest, value);
  }

  namespace detail {
    template <typename T, bool IsPointer = is_pointer_v<T>, bool IsEnum = is_enum_v<T>,
              bool IsBool = is_same_v<T, bool>>
    struct atomic_storage_impl {
      using type = T;
    };

    template <typename T>
    struct atomic_storage_impl<T, true, false, false> {
      using type = uintptr_t;
    };

    template <typename T>
    struct atomic_storage_impl<T, false, true, false> {
      using type = underlying_type_t<T>;
    };

    template <typename T>
    struct atomic_storage_impl<T, false, false, true> {
      using type = u32;
    };

    template <typename T>
    struct atomic_storage {
      using type = typename atomic_storage_impl<T>::type;
    };

    template <typename T>
    using atomic_storage_t = typename atomic_storage<T>::type;

    template <typename T>
    inline atomic_storage_t<T> to_atomic_storage(T value) noexcept {
      if constexpr (is_pointer_v<T>)
        return reinterpret_cast<atomic_storage_t<T>>(value);
      else if constexpr (is_same_v<T, bool>)
        return value ? 1u : 0u;
      else if constexpr (is_enum_v<T>)
        return static_cast<atomic_storage_t<T>>(value);
      else
        return value;
    }

    template <typename T>
    inline T from_atomic_storage(atomic_storage_t<T> value) noexcept {
      if constexpr (is_pointer_v<T>)
        return reinterpret_cast<T>(value);
      else if constexpr (is_same_v<T, bool>)
        return value != 0;
      else if constexpr (is_enum_v<T>)
        return static_cast<T>(value);
      else
        return value;
    }
  }  // namespace detail

  template <typename T>
  class Atomic {
  public:
    using value_type = T;
    using storage_type = detail::atomic_storage_t<T>;

    Atomic() noexcept = default;
    constexpr Atomic(T value) noexcept : _value{detail::to_atomic_storage(value)} {}

    Atomic(const Atomic &) = delete;
    Atomic &operator=(const Atomic &) = delete;

    storage_type *native_handle() noexcept { return &_value; }
    const storage_type *native_handle() const noexcept { return &_value; }

    T load(memory_order = memory_order_seq_cst) const noexcept {
      return detail::from_atomic_storage<T>(atomic_load(omp_c, &_value));
    }

    void store(T value, memory_order = memory_order_seq_cst) noexcept {
      atomic_store(omp_c, &_value, detail::to_atomic_storage(value));
    }

    T exchange(T value, memory_order = memory_order_seq_cst) noexcept {
      return detail::from_atomic_storage<T>(
          atomic_exch(omp_c, &_value, detail::to_atomic_storage(value)));
    }

    bool compare_exchange_weak(T &expected, T desired,
                               memory_order = memory_order_seq_cst,
                               memory_order = memory_order_seq_cst) noexcept {
      return compare_exchange_strong(expected, desired);
    }

    bool compare_exchange_strong(T &expected, T desired,
                                 memory_order = memory_order_seq_cst,
                                 memory_order = memory_order_seq_cst) noexcept {
      auto expectedStorage = detail::to_atomic_storage(expected);
      const auto observed = atomic_cas(omp_c, &_value, expectedStorage,
                                       detail::to_atomic_storage(desired));
      if (observed == expectedStorage) return true;
      expected = detail::from_atomic_storage<T>(observed);
      return false;
    }

    template <typename Q = T, enable_if_type<is_integral_v<Q> && !is_same_v<Q, bool>, int> = 0>
    T fetch_add(T value, memory_order = memory_order_seq_cst) noexcept {
      return atomic_add(omp_c, &_value, value);
    }

    template <typename Q = T, enable_if_type<is_integral_v<Q> && !is_same_v<Q, bool>, int> = 0>
    T fetch_sub(T value, memory_order = memory_order_seq_cst) noexcept {
      return atomic_add(omp_c, &_value, -value);
    }

    void wait(T expected, memory_order = memory_order_seq_cst) const noexcept {
      _waitMutex.lock();
      _waitCv.wait(_waitMutex, [&] { return load(memory_order_acquire) != expected; });
      _waitMutex.unlock();
    }

    void notify_one() noexcept { _waitCv.notify_one(); }
    void notify_all() noexcept { _waitCv.notify_all(); }

  private:
    static_assert((is_integral_v<storage_type> || is_same_v<storage_type, uintptr_t>)
                      && sizeof(storage_type) <= sizeof(u64),
                  "Atomic supports bool, pointers, enums, and host integral types up to 64 bits.");

    mutable Mutex _waitMutex{};
    mutable ConditionVariable _waitCv{};
    storage_type _value{};
  };

  template <typename T>
  using atomic = Atomic<T>;

  using atomic_bool = Atomic<bool>;
  using atomic_size_t = Atomic<size_t>;

}  // namespace zs