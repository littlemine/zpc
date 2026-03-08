#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <cstdio>
#include <new>
#include <thread>
#include <vector>

#include "zensim/execution/AsyncMemoryPool.hpp"
#include "zensim/execution/Atomics.hpp"

using namespace zs;

namespace {
  using clock_t = std::chrono::steady_clock;

  struct BenchNode {
    u64 value{0};
    BenchNode *next{nullptr};
  };

  template <typename Fn>
  double bench_ms(Fn &&fn) {
    const auto start = clock_t::now();
    fn();
    const auto end = clock_t::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
  }

  double bench_std_fetch_add(size_t iterations) {
    std::atomic<u64> value{0};
    return bench_ms([&] {
      for (size_t i = 0; i < iterations; ++i) value.fetch_add(1, std::memory_order_seq_cst);
    });
  }

  double bench_zs_fetch_add(size_t iterations) {
    atomic<u64> value{0};
    return bench_ms([&] {
      for (size_t i = 0; i < iterations; ++i) value.fetch_add(1);
    });
  }

  double bench_std_fetch_add_contended(size_t threads, size_t iterations_per_thread) {
    std::atomic<u64> value{0};
    std::vector<std::thread> workers;
    workers.reserve(threads);
    return bench_ms([&] {
      for (size_t tid = 0; tid < threads; ++tid) {
        workers.emplace_back([&] {
          for (size_t i = 0; i < iterations_per_thread; ++i)
            value.fetch_add(1, std::memory_order_seq_cst);
        });
      }
      for (auto &worker : workers) worker.join();
    });
  }

  double bench_zs_fetch_add_contended(size_t threads, size_t iterations_per_thread) {
    atomic<u64> value{0};
    std::vector<std::thread> workers;
    workers.reserve(threads);
    return bench_ms([&] {
      for (size_t tid = 0; tid < threads; ++tid) {
        workers.emplace_back([&] {
          for (size_t i = 0; i < iterations_per_thread; ++i) value.fetch_add(1);
        });
      }
      for (auto &worker : workers) worker.join();
    });
  }

  double bench_std_new_delete(size_t iterations) {
    return bench_ms([&] {
      for (size_t i = 0; i < iterations; ++i) {
        auto *node = new BenchNode{static_cast<u64>(i), nullptr};
        delete node;
      }
    });
  }

  double bench_async_pool(size_t iterations) {
    detail::AsyncObjectPool<BenchNode, 1024> pool;
    return bench_ms([&] {
      for (size_t i = 0; i < iterations; ++i) {
        auto *node = pool.acquire();
        node->value = static_cast<u64>(i);
        pool.release(node);
      }
    });
  }
}  // namespace

int main() {
  try {
    constexpr size_t single_thread_iterations = 2'000'000;
    constexpr size_t contended_iterations = 300'000;
    constexpr size_t pool_iterations = 1'000'000;
    const size_t thread_count = std::max<size_t>(
        2, std::min<size_t>(4, static_cast<size_t>(std::thread::hardware_concurrency())));

    const double std_single = bench_std_fetch_add(single_thread_iterations);
    const double zs_single = bench_zs_fetch_add(single_thread_iterations);
    const double std_contended =
        bench_std_fetch_add_contended(thread_count, contended_iterations);
    const double zs_contended = bench_zs_fetch_add_contended(thread_count, contended_iterations);
    const double std_heap = bench_std_new_delete(pool_iterations);
    const double zs_pool = bench_async_pool(pool_iterations);

    std::printf("async benchmark\n");
    std::printf("std::atomic fetch_add single-thread : %.3f ms\n", std_single);
    std::printf("zs::atomic  fetch_add single-thread : %.3f ms (ratio %.3f)\n", zs_single,
                zs_single / std_single);
    std::printf("std::atomic fetch_add %zu-thread     : %.3f ms\n", thread_count,
                std_contended);
    std::printf("zs::atomic  fetch_add %zu-thread     : %.3f ms (ratio %.3f)\n", thread_count,
                zs_contended, zs_contended / std_contended);
    std::printf("new/delete BenchNode                : %.3f ms\n", std_heap);
    std::printf("async pool BenchNode                : %.3f ms (ratio %.3f)\n", zs_pool,
                zs_pool / std_heap);
    std::fflush(stdout);
    return 0;
  } catch (const std::exception &ex) {
    std::fprintf(stderr, "async benchmark failed: %s\n", ex.what());
    std::fflush(stderr);
  } catch (...) {
    std::fprintf(stderr, "async benchmark failed: unknown exception\n");
    std::fflush(stderr);
  }
  return 1;
}