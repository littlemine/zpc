#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>
#include <vector>

#include "zensim/container/ConcurrentQueue.hpp"
#include "zensim/zpc_tpls/moodycamel/concurrent_queue/concurrentqueue.h"

namespace {

  using clock_t = std::chrono::steady_clock;

  struct BenchResult {
    double elapsedMs{0.0};
    double throughputMops{0.0};
  };

  template <typename Fn>
  BenchResult run_benchmark(size_t operations, Fn &&fn) {
    const auto start = clock_t::now();
    fn();
    const auto end = clock_t::now();
    const auto elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
    const auto throughputMops = elapsedMs > 0.0 ? (static_cast<double>(operations) / 1.0e6)
                                                       / (elapsedMs / 1000.0)
                                                 : 0.0;
    return {elapsedMs, throughputMops};
  }

  template <typename Queue>
  BenchResult bench_spsc(std::string_view name, size_t iterations) {
    Queue queue;
    std::atomic<size_t> consumed{0};
    long long checksum = 0;

    auto result = run_benchmark(iterations, [&] {
      std::thread producer([&] {
        for (size_t i = 0; i < iterations; ++i) {
          while (!queue.try_enqueue(static_cast<int>(i))) std::this_thread::yield();
        }
      });

      std::thread consumer([&] {
        int value = 0;
        while (consumed.load(std::memory_order_relaxed) < iterations) {
          if (queue.try_dequeue(value)) {
            checksum += value;
            consumed.fetch_add(1, std::memory_order_relaxed);
          } else {
            std::this_thread::yield();
          }
        }
      });

      producer.join();
      consumer.join();
    });

    const auto expected = static_cast<long long>(iterations - 1) * static_cast<long long>(iterations)
                          / 2;
    if (checksum != expected) {
      std::fprintf(stderr, "%.*s spsc checksum mismatch: got=%lld expected=%lld\n",
                   static_cast<int>(name.size()), name.data(), checksum, expected);
      std::exit(1);
    }
    return result;
  }

  template <typename Queue>
  BenchResult bench_mpmc(std::string_view name, size_t producers, size_t consumers,
                         size_t iterationsPerProducer) {
    Queue queue;
    std::atomic<size_t> consumed{0};
    std::atomic<long long> checksum{0};
    const size_t totalItems = producers * iterationsPerProducer;

    auto result = run_benchmark(totalItems, [&] {
      std::vector<std::thread> producerThreads;
      std::vector<std::thread> consumerThreads;
      producerThreads.reserve(producers);
      consumerThreads.reserve(consumers);

      for (size_t producerId = 0; producerId < producers; ++producerId) {
        producerThreads.emplace_back([&, producerId] {
          const int base = static_cast<int>(producerId * iterationsPerProducer);
          for (size_t i = 0; i < iterationsPerProducer; ++i) {
            const int value = base + static_cast<int>(i);
            while (!queue.try_enqueue(value)) std::this_thread::yield();
          }
        });
      }

      for (size_t consumerId = 0; consumerId < consumers; ++consumerId) {
        consumerThreads.emplace_back([&] {
          int value = 0;
          for (;;) {
            const auto observed = consumed.load(std::memory_order_relaxed);
            if (observed >= totalItems) break;
            if (queue.try_dequeue(value)) {
              checksum.fetch_add(value, std::memory_order_relaxed);
              consumed.fetch_add(1, std::memory_order_relaxed);
            } else {
              std::this_thread::yield();
            }
          }
        });
      }

      for (auto &thread : producerThreads) thread.join();
      while (consumed.load(std::memory_order_relaxed) < totalItems) std::this_thread::yield();
      for (auto &thread : consumerThreads) thread.join();
    });

    const auto expected = static_cast<long long>(totalItems - 1) * static_cast<long long>(totalItems)
                          / 2;
    if (checksum.load(std::memory_order_relaxed) != expected) {
      std::fprintf(stderr, "%.*s mpmc checksum mismatch: got=%lld expected=%lld\n",
                   static_cast<int>(name.size()), name.data(),
                   checksum.load(std::memory_order_relaxed), expected);
      std::exit(1);
    }
    return result;
  }

  template <size_t Capacity>
  struct ZsQueueAdapter {
    zs::ConcurrentQueue<int, Capacity> queue{};

    bool try_enqueue(int value) { return queue.try_enqueue(value); }
    bool try_dequeue(int &value) { return queue.try_dequeue(value); }
  };

  template <size_t Capacity>
  struct MoodyCamelQueueAdapter {
    moodycamel::ConcurrentQueue<int> queue{Capacity};

    bool try_enqueue(int value) { return queue.try_enqueue(value); }
    bool try_dequeue(int &value) { return queue.try_dequeue(value); }
  };

  void print_result(std::string_view scenario, std::string_view name, const BenchResult &result) {
    std::printf("%-10.*s %-12.*s : %9.3f ms  %9.3f Mops/s\n", static_cast<int>(scenario.size()),
                scenario.data(), static_cast<int>(name.size()), name.data(), result.elapsedMs,
                result.throughputMops);
  }

}  // namespace

int main() {
  constexpr size_t queueCapacity = 1 << 14;
  constexpr size_t spscIterations = 2'000'000;
  constexpr size_t mpmcIterationsPerProducer = 500'000;

  const auto hardwareThreads = std::max<size_t>(2, std::thread::hardware_concurrency());
  const auto producerCount = std::min<size_t>(4, hardwareThreads / 2 == 0 ? 1 : hardwareThreads / 2);
  const auto consumerCount = std::min<size_t>(4, std::max<size_t>(1, hardwareThreads - producerCount));

  std::printf("Concurrent queue benchmark\n");
  std::printf("capacity=%zu spsc_iterations=%zu producers=%zu consumers=%zu per_producer=%zu\n",
              queueCapacity, spscIterations, producerCount, consumerCount,
              mpmcIterationsPerProducer);

  const auto zsSpsc = bench_spsc<ZsQueueAdapter<queueCapacity>>("zpc", spscIterations);
  const auto moodySpsc = bench_spsc<MoodyCamelQueueAdapter<queueCapacity>>("moodycamel",
                                                                           spscIterations);
  const auto zsMpmc = bench_mpmc<ZsQueueAdapter<queueCapacity>>("zpc", producerCount,
                                                                consumerCount,
                                                                mpmcIterationsPerProducer);
  const auto moodyMpmc = bench_mpmc<MoodyCamelQueueAdapter<queueCapacity>>(
      "moodycamel", producerCount, consumerCount, mpmcIterationsPerProducer);

  print_result("spsc", "zpc", zsSpsc);
  print_result("spsc", "moodycamel", moodySpsc);
  print_result("mpmc", "zpc", zsMpmc);
  print_result("mpmc", "moodycamel", moodyMpmc);

  return 0;
}