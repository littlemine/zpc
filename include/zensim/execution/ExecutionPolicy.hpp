#pragma once

#include <cassert>
#include <numeric>

#include "zensim/TypeAlias.hpp"
#include "zensim/memory/MemoryResource.h"
#include "zensim/profile/CppTimers.hpp"
#include "zensim/zpc_tpls/fmt/format.h"
#include "zensim/zpc_tpls/magic_enum/magic_enum.hpp"
#include "zensim/types/Function.h"
#include "zensim/types/Iterator.h"
#include "zensim/types/Polymorphism.h"
#include "zensim/types/Property.h"
namespace zs {

  using exec_tags = variant<host_exec_tag, omp_exec_tag, cuda_exec_tag, hip_exec_tag>;

  constexpr const char *execution_space_tag[] = {"HOST", "OPENMP", "CUDA", "HIP"};
  constexpr const char *get_execution_tag_name(execspace_e execpol) {
    return execution_space_tag[magic_enum::enum_integer(execpol)];
  }

  constexpr exec_tags suggest_exec_space(const MemoryLocation &mloc) {
    switch (mloc.memspace()) {
      case memsrc_e::host:
#ifdef _OPENMP
        return exec_omp;
#else
        return exec_seq;
#endif
      case memsrc_e::device:
      case memsrc_e::um:
        return exec_cuda;
    }
    throw std::runtime_error(
        fmt::format("no valid execution space suggestions for the memory handle [{}, {}]\n",
                    get_memory_tag_name(mloc.memspace()), (int)mloc.devid()));
    return exec_seq;
  }

  struct DeviceHandle {
    NodeID nodeid{0};   ///<
    ProcID procid{-1};  ///< processor id (cpu: negative, gpu: positive)
  };

  struct ParallelTask {
    // vulkan compute: ***.spv
    // lambda functions or functors (with host or device decoration)
    // cuda module file
    // string literals
    std::string source{};
    std::function<void()> func;
  };

#define assert_with_msg(exp, msg) assert(((void)msg, exp))

  /// execution policy
  template <typename Derived> struct ExecutionPolicyInterface {
    bool launch(const ParallelTask &kernel) const noexcept { return selfPtr()->do_launch(kernel); }
    bool shouldSync() const noexcept { return selfPtr()->do_shouldSync(); }
    bool shouldWait() const noexcept { return selfPtr()->do_shouldWait(); }
    bool shouldProfile() const noexcept { return selfPtr()->do_shouldProfile(); }

    Derived &sync(bool sync_) noexcept {
      _sync = sync_;
      return *selfPtr();
    }
    Derived &profile(bool profile_) noexcept {
      _profile = profile_;
      return *selfPtr();
    }

    // constexpr DeviceHandle device() const noexcept { return handle; }

  protected:
    constexpr bool do_launch(const ParallelTask &) const noexcept { return false; }
    constexpr bool do_shouldSync() const noexcept { return _sync; }
    constexpr bool do_shouldWait() const noexcept { return _wait; }
    constexpr bool do_shouldProfile() const noexcept { return _profile; }

    constexpr Derived *selfPtr() noexcept { return static_cast<Derived *>(this); }
    constexpr const Derived *selfPtr() const noexcept { return static_cast<const Derived *>(this); }

    bool _sync{true}, _wait{false}, _profile{false};
    // DeviceHandle handle{0, -1};
  };

  struct SequentialExecutionPolicy : ExecutionPolicyInterface<SequentialExecutionPolicy> {
    using exec_tag = host_exec_tag;
    template <typename Range, typename F> constexpr void operator()(Range &&range, F &&f) const {
      using fts = function_traits<F>;
      if constexpr (fts::arity == 0)
        for (auto &&it : range) f();
      else {
        for (auto &&it : range) {
          if constexpr (is_std_tuple<remove_cvref_t<decltype(it)>>::value)
            std::apply(f, it);
          else
            std::invoke(f, it);
        }
      }
    }

    template <std::size_t I, std::size_t... Is, typename... Iters, typename... Policies,
              typename... Ranges, typename... Bodies>
    constexpr void exec(index_seq<Is...> indices, std::tuple<Iters...> prefixIters,
                        const zs::tuple<Policies...> &policies, const zs::tuple<Ranges...> &ranges,
                        const Bodies &...bodies) const {
      // using Range = zs::select_indexed_type<I, std::decay_t<Ranges>...>;
      const auto &range = zs::get<I>(ranges);
      if constexpr (I + 1 == sizeof...(Ranges)) {
        for (auto &&it : range) {
          const auto args = shuffle(indices, std::tuple_cat(prefixIters, std::make_tuple(it)));
          (std::apply(FWD(bodies), args), ...);
        }
      } else if constexpr (I + 1 < sizeof...(Ranges)) {
        auto &policy = zs::get<I + 1>(policies);
        for (auto &&it : range)
          policy.template exec<I + 1>(indices, std::tuple_cat(prefixIters, std::make_tuple(it)),
                                      policies, ranges, bodies...);
      }
    }

    /// serial version of several parallel primitives
    template <class ForwardIt, class UnaryFunction>
    constexpr void for_each(ForwardIt &&first, ForwardIt &&last, UnaryFunction &&f) const {
      (*this)(detail::iter_range(FWD(first), FWD(last)), FWD(f));
    }

    template <class InputIt, class OutputIt,
              class BinaryOperation = std::plus<remove_cvref_t<decltype(*std::declval<InputIt>())>>>
    constexpr void inclusive_scan(InputIt &&first, InputIt &&last, OutputIt &&d_first,
                                  BinaryOperation &&binary_op = {}) const {
      auto prev = *(d_first++) = *(first++);
      while (first != last) *(d_first++) = prev = binary_op(prev, *(first++));
    }
    template <class InputIt, class OutputIt,
              class T = remove_cvref_t<decltype(*std::declval<InputIt>())>,
              class BinaryOperation = std::plus<T>>
    constexpr void exclusive_scan(InputIt &&first, InputIt &&last, OutputIt &&d_first,
                                  T init = monoid_op<BinaryOperation>::e,
                                  BinaryOperation &&binary_op = {}) const {
      *(d_first++) = init;
      do {
        *(d_first++) = init = binary_op(init, *first);
      } while (++first != last);
    }
    template <class InputIt, class OutputIt,
              class T = remove_cvref_t<decltype(*std::declval<InputIt>())>,
              class BinaryOp = std::plus<T>>
    constexpr void reduce(InputIt &&first, InputIt &&last, OutputIt &&d_first,
                          T init = monoid_op<BinaryOp>::e, BinaryOp &&binary_op = {}) const {
      for (; first != last;) init = binary_op(init, *(first++));
      *d_first = init;
    }
    template <class InputIt, class OutputIt> constexpr void radix_sort(
        InputIt &&first, InputIt &&last, OutputIt &&d_first, int sbit = 0,
        int ebit
        = sizeof(typename std::iterator_traits<remove_cvref_t<InputIt>>::value_type) * 8) const {
      using IterT = remove_cvref_t<InputIt>;
      using DiffT = typename std::iterator_traits<IterT>::difference_type;
      using InputValueT = typename std::iterator_traits<IterT>::value_type;

      const auto dist = last - first;
      bool skip = false;
      constexpr int binBits = 8;  // by byte
      int binCount = 1 << binBits;
      int binMask = binCount - 1;

      std::vector<DiffT> binGlobalSizes(binCount);
      std::vector<DiffT> binOffsets(binCount);

      std::vector<InputValueT> buffers[2];
      buffers[0].resize(dist);
      buffers[1].resize(dist);
      InputValueT *cur{buffers[0].data()}, *next{buffers[1].data()};

      /// sign-related handling
      for (DiffT i = 0; i < dist; ++i) {
        if constexpr (std::is_signed_v<InputValueT>)
          cur[i] = *(first + i) ^ ((InputValueT)1 << (sizeof(InputValueT) * 8 - 1));
        else
          cur[i] = *(first + i);
      }

      for (int st = sbit; st < ebit; st += binBits) {
        if (st + binBits > ebit) {
          binMask >>= (st + binBits - ebit);
          binCount >>= (st + binBits - ebit);
        }

        for (DiffT i = 0; i < binCount; ++i) binGlobalSizes[i] = 0;
        for (DiffT i = 0; i < dist; ++i) binGlobalSizes[(cur[i] >> st) & binMask]++;

        binOffsets[0] = 0;
        skip = binGlobalSizes[0] == dist;
        for (int i = 1; i < binCount; ++i) {
          if (binGlobalSizes[i] == dist) {
            skip = true;
            break;
          }
          binOffsets[i] = binOffsets[i - 1] + binGlobalSizes[i - 1];
        }
        if (!skip) {
          for (int i = 0; i < binCount; i++) binGlobalSizes[i] += binOffsets[i];

          for (DiffT i = dist - 1; i >= 0; --i)
            next[--binGlobalSizes[(cur[i] >> st) & binMask]] = cur[i];
          std::swap(cur, next);
        }
      }

      /// sign-related handling
      for (DiffT i = 0; i < dist; ++i) {
        if constexpr (std::is_signed_v<InputValueT>)
          *(d_first + i) = cur[i] ^ ((InputValueT)1 << (sizeof(InputValueT) * 8 - 1));
        else
          *(d_first + i) = cur[i];
      }
    }
    template <class KeyIter, class ValueIter,
              typename Tn = typename std::iterator_traits<remove_cvref_t<KeyIter>>::difference_type>
    void radix_sort_pair(
        KeyIter &&keysIn, ValueIter &&valsIn, KeyIter &&keysOut, ValueIter &&valsOut, Tn count = 0,
        int sbit = 0,
        int ebit
        = sizeof(typename std::iterator_traits<remove_cvref_t<KeyIter>>::value_type) * 8) const {
      using KeyT = typename std::iterator_traits<KeyIter>::value_type;
      using ValueT = typename std::iterator_traits<ValueIter>::value_type;
      using DiffT = typename std::iterator_traits<KeyIter>::difference_type;

      const auto dist = count;
      bool skip = false;
      constexpr int binBits = 8;  // by byte
      int binCount = 1 << binBits;
      int binMask = binCount - 1;

      std::vector<DiffT> binGlobalSizes(binCount);
      std::vector<DiffT> binOffsets(binCount);

      std::vector<KeyT> keyBuffers[2];
      std::vector<ValueT> valBuffers[2];
      keyBuffers[0].resize(count);
      keyBuffers[1].resize(count);
      valBuffers[0].resize(count);
      valBuffers[1].resize(count);
      KeyT *cur{keyBuffers[0].data()}, *next{keyBuffers[1].data()};
      ValueT *curVals{valBuffers[0].data()}, *nextVals{valBuffers[1].data()};

      /// sign-related handling
      for (DiffT i = 0; i < dist; ++i) {
        if constexpr (std::is_signed_v<KeyT>)
          cur[i] = *(keysIn + i) ^ ((KeyT)1 << (sizeof(KeyT) * 8 - 1));
        else
          cur[i] = *(keysIn + i);
        curVals[i] = *(valsIn + i);
      }

      for (int st = sbit; st < ebit; st += binBits) {
        if (st + binBits > ebit) {
          binMask >>= (st + binBits - ebit);
          binCount >>= (st + binBits - ebit);
        }

        for (DiffT i = 0; i < binCount; ++i) binGlobalSizes[i] = 0;
        for (DiffT i = 0; i < dist; ++i) binGlobalSizes[(cur[i] >> st) & binMask]++;

        binOffsets[0] = 0;
        skip = binGlobalSizes[0] == dist;
        for (int i = 1; i < binCount; ++i) {
          if (binGlobalSizes[i] == dist) {
            skip = true;
            break;
          }
          binOffsets[i] = binOffsets[i - 1] + binGlobalSizes[i - 1];
        }
        if (!skip) {
          for (int i = 0; i < binCount; i++) binGlobalSizes[i] += binOffsets[i];

          for (DiffT i = dist - 1; i >= 0; --i) {
            // next[--binGlobalSizes[(cur[i] >> st) & binMask]] = cur[i];
            const auto loc = --binGlobalSizes[(cur[i] >> st) & binMask];
            next[loc] = cur[i];
            nextVals[loc] = curVals[i];
          }
          std::swap(cur, next);
          std::swap(curVals, nextVals);
        }
      }

      /// sign-related handling
      for (DiffT i = 0; i < dist; ++i) {
        if constexpr (std::is_signed_v<KeyT>)
          *(keysOut + i) = cur[i] ^ ((KeyT)1 << (sizeof(KeyT) * 8 - 1));
        else
          *(keysOut + i) = cur[i];
        *(valsOut + i) = curVals[i];
      }
    }

  protected:
    bool do_launch(const ParallelTask &) const noexcept;
    bool do_sync() const noexcept { return true; }
    friend struct ExecutionPolicyInterface<SequentialExecutionPolicy>;
  };

  struct CudaExecutionPolicy;
  struct OmpExecutionPolicy;

  constexpr SequentialExecutionPolicy par_exec(host_exec_tag) noexcept {
    return SequentialExecutionPolicy{};
  }
  constexpr SequentialExecutionPolicy seq_exec() noexcept { return SequentialExecutionPolicy{}; }

  /// ========================================================================
  /// kernel, for_each, reduce, scan, gather, sort
  /// ========================================================================
  /// this can only be called on host side
  template <std::size_t... Is, typename... Policies, typename... Ranges, typename... Bodies>
  constexpr void par_exec(zs::tuple<Policies...> policies, zs::tuple<Ranges...> ranges,
                          Bodies &&...bodies) {
    /// these backends should all be on the host side
    static_assert(sizeof...(Policies) == sizeof...(Ranges),
                  "there should be a corresponding policy for every range\n");
    static_assert(sizeof...(Is) == 0 || sizeof...(Is) == sizeof...(Ranges),
                  "loop index mapping not legal\n");
    using Indices
        = conditional_t<sizeof...(Is) == 0, std::index_sequence_for<Ranges...>, index_seq<Is...>>;
    if constexpr (sizeof...(Policies) == 0)
      return;
    else {
      auto &policy = policies.template get<0>();
      policy.template exec<0>(Indices{}, std::tuple<>{}, policies, ranges, bodies...);
    }
  }

  /// default policy is 'sequential'
  /// this should be able to be used within a kernel
  template <std::size_t... Is, typename... Ranges, typename... Bodies>
  constexpr void par_exec(zs::tuple<Ranges...> ranges, Bodies &&...bodies) {
    using SeqPolicies =
        typename gen_seq<sizeof...(Ranges)>::template uniform_types_t<zs::tuple,
                                                                      SequentialExecutionPolicy>;
    par_exec<Is...>(SeqPolicies{}, std::move(ranges), FWD(bodies)...);
  }

  //
  template <typename ExecPol> constexpr bool is_backend_available(ExecPol = {}) noexcept {
    return false;
  }

  template <typename ExecTag> constexpr bool is_backend_activated(ExecTag) noexcept {
    if constexpr (is_same_v<ExecTag, omp_exec_tag>)
      return ZS_ENABLE_OPENMP;
    else if constexpr (is_same_v<ExecTag, cuda_exec_tag>)
      return ZS_ENABLE_CUDA;
    else if constexpr (is_same_v<ExecTag, host_exec_tag>)
      return true;
    return false;
  }

  // ===================== parallel pattern wrapper ====================
  /// for_each
  template <class ExecutionPolicy, class ForwardIt, class UnaryFunction>
  constexpr void for_each(ExecutionPolicy &&policy, ForwardIt &&first, ForwardIt &&last,
                          UnaryFunction &&f) {
    policy.for_each(FWD(first), FWD(last), FWD(f));
  }
  /// transform
  template <class ExecutionPolicy, class ForwardIt, class UnaryFunction>
  constexpr void transform(ExecutionPolicy &&policy, ForwardIt &&first, ForwardIt &&last,
                           UnaryFunction &&f) {
    policy.for_each(FWD(first), FWD(last), FWD(f));
  }
  /// scan
  template <class ExecutionPolicy, class InputIt, class OutputIt,
            class BinaryOperation = std::plus<remove_cvref_t<decltype(*std::declval<InputIt>())>>>
  constexpr void inclusive_scan(ExecutionPolicy &&policy, InputIt &&first, InputIt &&last,
                                OutputIt &&d_first, BinaryOperation &&binary_op = {}) {
    policy.inclusive_scan(FWD(first), FWD(last), FWD(d_first), FWD(binary_op));
  }
  template <class ExecutionPolicy, class InputIt, class OutputIt,
            class T = remove_cvref_t<decltype(*std::declval<InputIt>())>,
            class BinaryOperation = std::plus<T>>
  constexpr void exclusive_scan(ExecutionPolicy &&policy, InputIt &&first, InputIt &&last,
                                OutputIt &&d_first, T init = monoid_op<BinaryOperation>::e,
                                BinaryOperation &&binary_op = {}) {
    policy.exclusive_scan(FWD(first), FWD(last), FWD(d_first), init, FWD(binary_op));
  }
  /// reduce
  template <class ExecutionPolicy, class InputIt, class OutputIt, class T,
            class BinaryOp = std::plus<T>>
  constexpr void reduce(ExecutionPolicy &&policy, InputIt &&first, InputIt &&last,
                        OutputIt &&d_first, T init, BinaryOp &&binary_op = {}) {
    policy.reduce(FWD(first), FWD(last), FWD(d_first), init, FWD(binary_op));
  }
  /// sort
  template <class ExecutionPolicy, class KeyIter, class ValueIter,
            typename Tn = typename std::iterator_traits<remove_cvref_t<KeyIter>>::difference_type>
  constexpr std::enable_if_t<std::is_convertible_v<
      typename std::iterator_traits<remove_cvref_t<KeyIter>>::iterator_category,
      std::random_access_iterator_tag>>
  radix_sort_pair(ExecutionPolicy &&policy, KeyIter &&keysIn, ValueIter &&valsIn, KeyIter &&keysOut,
                  ValueIter &&valsOut, Tn count, int sbit = 0,
                  int ebit
                  = sizeof(typename std::iterator_traits<remove_cvref_t<KeyIter>>::value_type)
                    * 8) {
    policy.radix_sort_pair(FWD(keysIn), FWD(valsIn), FWD(keysOut), FWD(valsOut), count, sbit, ebit);
  }
  template <class ExecutionPolicy, class InputIt, class OutputIt> constexpr void radix_sort(
      ExecutionPolicy &&policy, InputIt &&first, InputIt &&last, OutputIt &&d_first, int sbit = 0,
      int ebit = sizeof(typename std::iterator_traits<remove_cvref_t<InputIt>>::value_type) * 8) {
    policy.radix_sort(FWD(first), FWD(last), FWD(d_first), sbit, ebit);
  }
  /// gather/ select (flagged, if, unique)

}  // namespace zs