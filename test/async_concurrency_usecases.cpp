/// @file async_concurrency_usecases.cpp
/// @brief Comprehensive use-case tests for every async/concurrency primitive in zpc.
///
/// Each section documents:
///   - WHAT the primitive is
///   - WHEN to use it (typical scenarios)
///   - WHERE it fits in the zpc stack
///   - HOW to use it correctly
///   - Profile metrics (wall-time, throughput where applicable)
///
/// Primitives covered:
///   1.  Futex          — OS-level blocking; foundation for Mutex & CV
///   2.  Mutex          — Lightweight futex-based mutual exclusion
///   3.  ConditionVariable — Futex-based signaling between threads
///   4.  Atomic<T>      — Lock-free atomic operations (host-side)
///   5.  ConcurrentQueue — MPMC lock-free queue
///   6.  SpscQueue      — Single-producer / single-consumer queue
///   7.  ManagedThread  — Lifecycle-managed OS thread
///   8.  AsyncStopSource/Token — Cooperative cancellation
///   9.  AsyncEvent     — Ref-counted, waitable completion signal
///  10.  AsyncRuntime   — Executor-based task submission engine
///  11.  AsyncScheduler — Coroutine-aware work-stealing scheduler
///  12.  Future<T>/Task — C++20 coroutine primitives
///  13.  Generator<T>   — C++20 lazy pull-based iteration
///  14.  TaskGraph      — DAG-ordered task execution
///  15.  ExecutionPolicy (Sequential) — Serial parallel-primitive interface
///  16.  SharedMemoryRegion (NEW) — Cross-process shared memory
///  17.  NamedChannel (NEW)       — Cross-process message channel via named pipe
///  18.  ExecutionGraph (NEW)     — Backend-agnostic render-graph with hazard analysis
///       18a. MPM pipeline (serial chain, single lane)
///       18b. Independent passes (no hazard, parallel lanes)
///       18c. Cycle detection / WAW ordering
///       18d. GPU lane overlap (shadow map ROP + compute cull ALU)
///       18e. Multi-stream compute (N independent kernels → N lanes)
///  19.  PassCostHint (NEW)         — Cost-aware topo sort tie-breaking
///  20.  Memory abstractions (NEW)
///       20a. memsrc_e taxonomy (file_mapped, shared_ipc tags)
///       20b. PageAccess / vmr_t::protect() interface
///       20c. ByteStream / FileStream / StdioStream interfaces
///       20d. MappedFile : vmr_t interface

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <thread>
#include <vector>

#include "zensim/ZpcAsync.hpp"
#include "zensim/ZpcCoroutine.hpp"
#include "zensim/ZpcTaskGraph.hpp"
#include "zensim/container/ConcurrentQueue.hpp"
#include "zensim/execution/AsyncAwaitables.hpp"
#include "zensim/execution/AsyncRuntime.hpp"
#include "zensim/execution/AsyncScheduler.hpp"
#include "zensim/execution/ConcurrencyPrimitive.hpp"
#include "zensim/execution/ExecutionPolicy.hpp"
#include "zensim/execution/Atomics.hpp"
#include "zensim/process/SharedMemoryRegion.hpp"
#include "zensim/process/NamedChannel.hpp"
#include "zensim/execution/ExecutionGraph.hpp"
#include "zensim/memory/MemoryResource.h"
#include "zensim/memory/MappedFile.hpp"
#include "zensim/io/ByteStream.hpp"
#include "zensim/types/Property.h"

using namespace zs;

// ═══════════════════════════════════════════════════════════════════════════
// Profiling infrastructure
// ═══════════════════════════════════════════════════════════════════════════

using hrclock = std::chrono::steady_clock;

struct ProfileResult {
  const char *name;
  double elapsedUs;
  double throughputMops;  // million-ops per second; 0 when N/A
};

static ProfileResult profile(const char *name, size_t ops, auto &&fn) {
  const auto t0 = hrclock::now();
  fn();
  const auto t1 = hrclock::now();
  const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
  const double mops = ops > 0 && us > 0.0 ? (double)ops / us : 0.0;
  return {name, us, mops};
}

static void report(const ProfileResult &r) {
  if (r.throughputMops > 0.0)
    std::printf("  %-44s %10.1f us  %8.3f Mops/s\n", r.name, r.elapsedUs, r.throughputMops);
  else
    std::printf("  %-44s %10.1f us\n", r.name, r.elapsedUs);
}

static void require(bool cond, const char *msg) {
  if (cond) return;
  std::fprintf(stderr, "[FAIL] %s\n", msg);
  std::fflush(stderr);
  std::abort();
}

// ═══════════════════════════════════════════════════════════════════════════
// 1. Futex
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Lowest-level blocking primitive. Puts the calling thread to sleep
///        until another thread wakes it. Backed by OS (WaitOnAddress / futex).
///
/// WHEN:  Building custom synchronisation (spinlock → futex hybrid, event,
///        semaphore). Rarely used directly; prefer Mutex / CV.
///
/// WHERE: Foundation layer; ConcurrencyPrimitive.hpp.
///
/// HOW:   Futex::wait(&atom, expected) blocks if atom == expected.
///        Futex::wake(&atom, count)    wakes up to `count` waiters.
///        Always update the atomic BEFORE calling wake.

static void test_futex_ping_pong() {
  std::printf("[usecase] Futex — thread ping-pong\n");

  std::atomic<u32> flag{0};  // 0 = ping's turn, 1 = pong's turn
  constexpr int rounds = 50'000;
  int pingCount = 0, pongCount = 0;

  auto pr = profile("futex_ping_pong", rounds * 2, [&] {
    std::thread pong([&] {
      for (int i = 0; i < rounds; ++i) {
        while (flag.load(std::memory_order_acquire) != 1)
          Futex::wait(&flag, 0);  // sleep while still ping's turn
        pongCount++;
        flag.store(0, std::memory_order_release);
        Futex::wake(&flag, 1);
      }
    });

    for (int i = 0; i < rounds; ++i) {
      while (flag.load(std::memory_order_acquire) != 0)
        Futex::wait(&flag, 1);
      pingCount++;
      flag.store(1, std::memory_order_release);
      Futex::wake(&flag, 1);
    }
    pong.join();
  });

  require(pingCount == rounds, "futex ping count");
  require(pongCount == rounds, "futex pong count");
  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. Mutex
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Lightweight process-local mutex using futex (3-state: unlocked /
///        locked / contended). 4 bytes, no heap allocation.
///
/// WHEN:  Protecting shared mutable state across threads.
///        Use when the critical section is NOT trivially short (for trivial
///        sections, consider Atomic instead).
///
/// WHERE: ConcurrencyPrimitive.hpp.  Used internally by AsyncEvent, AsyncRuntime.
///
/// HOW:   Mutex m{};
///        m.lock(); … m.unlock();
///        Or with std::lock_guard<Mutex>.

static void test_mutex_contention() {
  std::printf("[usecase] Mutex — contended counter increment\n");

  Mutex mtx{};
  int counter = 0;
  constexpr int perThread = 100'000;
  constexpr int numThreads = 4;

  auto pr = profile("mutex_contention", (size_t)perThread * numThreads, [&] {
    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (int t = 0; t < numThreads; ++t) {
      threads.emplace_back([&] {
        for (int i = 0; i < perThread; ++i) {
          std::lock_guard<Mutex> lk(mtx);
          ++counter;
        }
      });
    }
    for (auto &th : threads) th.join();
  });

  require(counter == perThread * numThreads, "mutex counter");
  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. ConditionVariable
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Futex-based condition variable.
///
/// WHEN:  Producer-consumer patterns where the consumer should SLEEP rather
///        than spin. Typically paired with a Mutex and a predicate.
///
/// WHERE: ConcurrencyPrimitive.hpp.  Used by AsyncEvent::wait().
///
/// HOW:   cv.wait(mutex, predicate)    — unlocks, sleeps, re-locks.
///        cv.notify_one() / notify_all()

static void test_condvar_producer_consumer() {
  std::printf("[usecase] ConditionVariable — bounded producer/consumer\n");

  Mutex mtx{};
  ConditionVariable cv{};
  int buffer = -1;
  bool ready = false;
  bool done = false;
  constexpr int N = 100'000;
  int consumed = 0;

  auto pr = profile("condvar_prodcons", N, [&] {
    std::thread consumer([&] {
      for (;;) {
        mtx.lock();
        cv.wait(mtx, [&] { return ready || done; });
        if (ready) {
          consumed += (buffer >= 0) ? 1 : 0;
          ready = false;
        }
        const bool finished = done && !ready;
        mtx.unlock();
        cv.notify_one();
        if (finished) break;
      }
    });

    for (int i = 0; i < N; ++i) {
      mtx.lock();
      cv.wait(mtx, [&] { return !ready; });
      buffer = i;
      ready = true;
      mtx.unlock();
      cv.notify_one();
    }
    mtx.lock();
    cv.wait(mtx, [&] { return !ready; });
    done = true;
    mtx.unlock();
    cv.notify_one();
    consumer.join();
  });

  require(consumed == N, "condvar consumed all");
  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. Atomic<T>
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Platform-uniform atomic type with wait/notify support.
///        Matches std::atomic API plus Futex-backed wait/notify_one/notify_all.
///
/// WHEN:  Lock-free counters, flags, CAS loops. Prefer over Mutex when the
///        "critical section" is a single read-modify-write operation.
///
/// WHERE: Atomics.hpp.  Backbone of ConcurrentQueue, AsyncScheduler internals.
///
/// HOW:   Atomic<int> a{0};
///        a.fetch_add(1);  a.compare_exchange_strong(expected, desired);
///        a.wait(0);       a.notify_one();

static void test_atomics_fetch_add_throughput() {
  std::printf("[usecase] Atomic<T> — multi-thread fetch_add throughput\n");

  Atomic<i64> counter{0};
  constexpr int perThread = 1'000'000;
  constexpr int numThreads = 4;

  auto pr = profile("atomic_fetch_add", (size_t)perThread * numThreads, [&] {
    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (int t = 0; t < numThreads; ++t) {
      threads.emplace_back([&] {
        for (int i = 0; i < perThread; ++i) counter.fetch_add(1);
      });
    }
    for (auto &th : threads) th.join();
  });

  require(counter.load() == (i64)perThread * numThreads, "atomic counter");
  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. ConcurrentQueue (MPMC)
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Lock-free bounded multi-producer multi-consumer queue.
///
/// WHEN:  Distributing work items among a pool of workers. E.g. the
///        AsyncThreadPoolExecutor uses one internally.
///
/// WHERE: container/ConcurrentQueue.hpp.
///
/// HOW:   ConcurrentQueue<T, Capacity> q;
///        q.try_enqueue(item);  q.try_dequeue(item);
///        Both return false on full/empty (never block).

static void test_concurrent_queue_mpmc() {
  std::printf("[usecase] ConcurrentQueue — 4P/4C throughput\n");

  constexpr size_t cap = 1 << 14;
  constexpr size_t perProducer = 500'000;
  constexpr int producers = 4, consumers = 4;
  const size_t total = (size_t)producers * perProducer;

  ConcurrentQueue<int, cap> queue;
  std::atomic<size_t> consumed{0};
  std::atomic<long long> checksum{0};

  auto pr = profile("mpmc_queue", total, [&] {
    std::vector<std::thread> prods, cons;
    for (int p = 0; p < producers; ++p) {
      prods.emplace_back([&, p] {
        const int base = (int)(p * perProducer);
        for (size_t i = 0; i < perProducer; ++i) {
          while (!queue.try_enqueue(base + (int)i)) std::this_thread::yield();
        }
      });
    }
    for (int c = 0; c < consumers; ++c) {
      cons.emplace_back([&] {
        int v;
        for (;;) {
          if (consumed.load(std::memory_order_relaxed) >= total) break;
          if (queue.try_dequeue(v)) {
            checksum.fetch_add(v, std::memory_order_relaxed);
            consumed.fetch_add(1, std::memory_order_relaxed);
          } else {
            std::this_thread::yield();
          }
        }
      });
    }
    for (auto &t : prods) t.join();
    while (consumed.load() < total) std::this_thread::yield();
    for (auto &t : cons) t.join();
  });

  const auto expected = (long long)(total - 1) * (long long)total / 2;
  require(checksum.load() == expected, "mpmc checksum");
  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 6. SpscQueue
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Single-producer single-consumer lock-free queue.
///        More cache-friendly than MPMC when the topology is known.
///
/// WHEN:  Dedicated reader/writer pairs: logging pipeline, audio callback
///        feeding the render thread, sensor → processing.
///
/// WHERE: container/ConcurrentQueue.hpp (same header, separate type).

static void test_spsc_queue_throughput() {
  std::printf("[usecase] SpscQueue — dedicated producer/consumer\n");

  constexpr size_t iterations = 2'000'000;
  SpscQueue<int> queue{1 << 14};
  std::atomic<size_t> consumed{0};
  long long checksum = 0;

  auto pr = profile("spsc_queue", iterations, [&] {
    std::thread producer([&] {
      for (size_t i = 0; i < iterations; ++i) {
        while (!queue.try_enqueue((int)i)) std::this_thread::yield();
      }
    });
    std::thread consumer([&] {
      int v;
      while (consumed.load(std::memory_order_relaxed) < iterations) {
        if (queue.try_dequeue(v)) {
          checksum += v;
          consumed.fetch_add(1, std::memory_order_relaxed);
        } else {
          std::this_thread::yield();
        }
      }
    });
    producer.join();
    consumer.join();
  });

  const auto expected = (long long)(iterations - 1) * (long long)iterations / 2;
  require(checksum == expected, "spsc checksum");
  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 7. ManagedThread
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  OS thread wrapper with cooperative stop/interrupt tokens and
///        lifecycle management (start/join/detach).
///
/// WHEN:  Long-running worker loops (simulation worker, I/O poller, GUI
///        message pump). Prefer over raw std::thread when you need graceful
///        shutdown via stop_requested().
///
/// WHERE: execution/ManagedThread.hpp.  Used by AsyncThreadPoolExecutor,
///        AsyncScheduler workers.
///
/// HOW:   ManagedThread t;
///        t.start([](ManagedThread &self) {
///          while (!self.stop_requested()) { ... }
///        }, "label");
///        t.request_stop();
///        t.join();

static void test_managed_thread_lifecycle() {
  std::printf("[usecase] ManagedThread — worker lifecycle\n");

  std::atomic<int> iterations{0};

  auto pr = profile("managed_thread", 0, [&] {
    ManagedThread thread;
    thread.start(
        [&](ManagedThread &self) {
          while (!self.stop_requested()) {
            iterations.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
          }
        },
        "usecase-worker");

    // let it spin for 5ms
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    thread.request_stop();
    require(thread.join(), "managed thread joined");
  });

  require(iterations.load() > 0, "managed thread ran");
  std::printf("    iterations in 5ms: %d\n", iterations.load());
  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 8. AsyncStopSource / AsyncStopToken
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Cooperative cancellation tokens. Source issues stop/interrupt;
///        token is a lightweight, copyable observer.
///
/// WHEN:  Cancelling async submissions, aborting long simulations, graceful
///        shutdown of thread pools. Two levels:
///        - stop_requested()     → "finish current iteration then exit"
///        - interrupt_requested() → "abort immediately if possible"
///
/// WHERE: ZpcAsync.hpp.  Threaded through AsyncExecutionContext, ManagedThread.

static void test_stop_token_cancellation() {
  std::printf("[usecase] AsyncStopToken — cooperative cancellation\n");

  auto pr = profile("stop_token", 0, [&] {
    AsyncStopSource source;
    auto t1 = source.token();
    auto t2 = t1;  // tokens are cheap copies

    require(!t1.stop_requested(), "not yet stopped");
    require(!t2.interrupt_requested(), "not yet interrupted");

    source.request_interrupt();
    require(t1.interrupt_requested(), "interrupt propagated to t1");
    require(t2.interrupt_requested(), "interrupt propagated to t2");
    require(!t1.stop_requested(), "stop distinct from interrupt");

    source.request_stop();
    require(t1.stop_requested(), "stop propagated");
    require(t2.stop_requested(), "stop propagated to copy");
  });

  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 9. AsyncEvent
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Reference-counted, thread-safe completion event with callback
///        chaining. Status transitions: pending → running → completed/
///        cancelled/failed.
///
/// WHEN:  Expressing "wait for this operation to finish" without polling.
///        E.g. wait for a GPU kernel, wait for an I/O transfer.
///        Callbacks fire synchronously on the completing thread.
///
/// WHERE: execution/AsyncRuntime.hpp (embedded in AsyncSubmissionState).
///
/// HOW:   auto ev = AsyncEvent::create();
///        ev.on_complete([] { ... });
///        // ... from another thread:
///        ev.complete(AsyncTaskStatus::completed);

static void test_async_event_fan_in() {
  std::printf("[usecase] AsyncEvent — fan-in (wait for N producers)\n");

  constexpr int N = 8;
  auto events = std::vector<AsyncEvent>(N);
  for (auto &e : events) e = AsyncEvent::create();

  std::atomic<int> callbacksFired{0};
  auto barrier = AsyncEvent::create();

  // When all N events complete, fire the barrier
  auto counter = std::make_shared<std::atomic<int>>(N);
  for (auto &e : events) {
    e.on_complete([&callbacksFired, &barrier, counter] {
      callbacksFired.fetch_add(1);
      if (counter->fetch_sub(1) == 1) barrier.complete();
    });
  }

  auto pr = profile("async_event_fan_in", N, [&] {
    std::vector<std::thread> threads;
    for (int i = 0; i < N; ++i) {
      threads.emplace_back([&events, i] {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        events[i].complete();
      });
    }
    barrier.wait();
    for (auto &t : threads) t.join();
  });

  require(callbacksFired.load() == N, "all callbacks fired");
  require(barrier.ready(), "barrier completed");
  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 10. AsyncRuntime
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Central task submission engine with pluggable executors (inline,
///        thread_pool, custom). Handles prerequisite chaining, cancellation,
///        and status tracking.
///
/// WHEN:  Submitting heterogeneous async work — CPU tasks, GPU dispatches,
///        I/O operations — through a single unified interface.
///        E.g. "run A on thread pool, then B inline once A finishes."
///
/// WHERE: execution/AsyncRuntime.hpp.
///
/// HOW:   AsyncRuntime runtime{4};
///        auto handle = runtime.submit(AsyncSubmission{ ... });
///        handle.event().wait();

static void test_async_runtime_pipeline() {
  std::printf("[usecase] AsyncRuntime — 3-stage pipeline with prerequisites\n");

  auto pr = profile("async_runtime_pipeline", 3, [&] {
    AsyncRuntime runtime{2};
    std::atomic<int> stage{0};

    // Stage 1: inline
    auto h1 = runtime.submit(AsyncSubmission{
        "inline",
        AsyncTaskDesc{"stage1", AsyncDomain::control, AsyncQueueClass::control},
        make_host_endpoint(),
        [&](AsyncExecutionContext &) {
          stage.store(1);
          return AsyncPollStatus::completed;
        }});

    // Stage 2: thread_pool, depends on stage 1
    AsyncSubmission s2{};
    s2.executor = "thread_pool";
    s2.desc = {"stage2", AsyncDomain::thread, AsyncQueueClass::compute};
    s2.endpoint = make_host_endpoint(AsyncBackend::thread_pool, AsyncQueueClass::compute);
    s2.step = [&](AsyncExecutionContext &) {
      require(stage.load() >= 1, "stage2 after stage1");
      stage.store(2);
      return AsyncPollStatus::completed;
    };
    s2.prerequisites.push_back(h1.event());
    auto h2 = runtime.submit(zs::move(s2));

    // Stage 3: inline, depends on stage 2
    AsyncSubmission s3{};
    s3.executor = "inline";
    s3.desc = {"stage3", AsyncDomain::control, AsyncQueueClass::control};
    s3.endpoint = make_host_endpoint();
    s3.step = [&](AsyncExecutionContext &) {
      require(stage.load() >= 2, "stage3 after stage2");
      stage.store(3);
      return AsyncPollStatus::completed;
    };
    s3.prerequisites.push_back(h2.event());
    auto h3 = runtime.submit(zs::move(s3));

    require(h3.event().wait_for(5000), "pipeline completed within 5s");
    require(stage.load() == 3, "all stages ran in order");
  });

  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 11. AsyncScheduler
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Coroutine-aware work-stealing scheduler with per-worker local
///        queues + global overflow queue.
///
/// WHEN:  High-throughput fork-join parallelism. Ideal for fine-grained tasks
///        where coroutines naturally suspend/resume (e.g. co_await on I/O,
///        co_await on a sub-task).
///
/// WHERE: execution/AsyncScheduler.hpp.
///
/// HOW:   AsyncScheduler sched{4};
///        sched.enqueue([] { ... });           // fire-and-forget function
///        co_await sched.schedule();           // resume on scheduler thread
///        sched.wait();                        // drain all work

static void test_scheduler_throughput() {
  std::printf("[usecase] AsyncScheduler — throughput (fire-and-forget)\n");

  constexpr int N = 100'000;
  AsyncScheduler scheduler{4};
  std::atomic<int> counter{0};

  auto pr = profile("scheduler_throughput", N, [&] {
    for (int i = 0; i < N; ++i) {
      scheduler.enqueue([&counter] { counter.fetch_add(1); });
    }
    scheduler.wait();
  });

  require(counter.load() == N, "scheduler completed all tasks");
  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 12. Future<T> / Task (C++20 coroutines)
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Lazy coroutine types.
///        Future<T> — returns a value via co_return.
///        Task      — alias for Future<void>.
///
/// WHEN:  Expressing asynchronous pipelines as sequential-looking code.
///        Natural fit with AsyncScheduler (co_await scheduler.schedule()).
///
/// WHERE: ZpcCoroutine.hpp.
///
/// HOW:   auto computation = []() -> Future<int> { co_return 42; };
///        int result = sync_wait(computation());

static void test_coroutine_chain() {
  std::printf("[usecase] Future<T> — coroutine chaining\n");

  auto pr = profile("coroutine_chain", 0, [&] {
    auto double_it = [](int x) -> Future<int> { co_return x * 2; };
    auto pipeline = [&]() -> Future<int> {
      int a = co_await double_it(10);  // 20
      int b = co_await double_it(a);   // 40
      int c = co_await double_it(b);   // 80
      co_return c;
    };

    int result = sync_wait(pipeline());
    require(result == 80, "coroutine chain produced 80");
  });

  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 13. Generator<T>
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Lazy pull-based iterator using co_yield.
///
/// WHEN:  Producing sequences on demand without allocating the entire result.
///        E.g. iterating over grid cells, streaming mesh vertices, generating
///        Fibonacci numbers.
///
/// WHERE: ZpcCoroutine.hpp.
///
/// HOW:   auto gen = []() -> Generator<int> {
///          for (int i = 0; ; ++i) co_yield i;
///        };
///        for (auto &v : gen()) { ... }

static void test_generator_lazy_range() {
  std::printf("[usecase] Generator<T> — lazy Fibonacci\n");

  auto fibonacci = []() -> Generator<i64> {
    i64 a = 0, b = 1;
    for (;;) {
      co_yield a;
      i64 tmp = a + b;
      a = b;
      b = tmp;
    }
  };

  auto pr = profile("generator_fib", 0, [&] {
    int count = 0;
    i64 last = 0;
    for (auto &val : fibonacci()) {
      last = val;
      if (++count == 50) break;  // fib(49)
    }
    require(last == 7778742049LL, "fib(49) correct");
  });

  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 14. TaskGraph
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  DAG-based task scheduling. Nodes represent work; edges represent
///        dependencies. Executed on an AsyncScheduler.
///
/// WHEN:  Complex dependency topologies — e.g. build systems, multi-pass
///        rendering, physics pipeline (broad phase → narrow phase → solve →
///        integrate).
///
/// WHERE: ZpcTaskGraph.hpp.
///
/// HOW:   TaskGraph g;
///        auto *a = g.addNode(fn_a, "A");
///        auto *b = g.addNode(fn_b, "B");
///        auto *c = g.addNode(fn_c, "C");
///        g.addEdge(a, c);  g.addEdge(b, c);
///        g.submit(scheduler);  g.wait(scheduler);

static void test_task_graph_diamond() {
  std::printf("[usecase] TaskGraph — diamond dependency\n");

  auto pr = profile("task_graph_diamond", 0, [&] {
    AsyncScheduler scheduler{4};
    std::atomic<int> order{0};
    int results[4] = {};

    TaskGraph graph;
    auto *root = graph.addNode([&] { results[0] = order.fetch_add(1) + 1; }, "root");
    auto *left = graph.addNode([&] { results[1] = order.fetch_add(1) + 1; }, "left");
    auto *right = graph.addNode([&] { results[2] = order.fetch_add(1) + 1; }, "right");
    auto *join = graph.addNode([&] { results[3] = order.fetch_add(1) + 1; }, "join");

    graph.addEdge(root, left);
    graph.addEdge(root, right);
    graph.addEdge(left, join);
    graph.addEdge(right, join);

    graph.submit(scheduler);
    graph.wait(scheduler);

    require(graph.allDone(), "all nodes done");
    require(results[0] < results[1], "root before left");
    require(results[0] < results[2], "root before right");
    require(results[3] == 4, "join is last");
  });

  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 15. SequentialExecutionPolicy
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Serial implementation of parallel primitive algorithms (for_each,
///        inclusive_scan, exclusive_scan, reduce, sort, radix_sort, etc.).
///        Uses CRTP; zero virtual dispatch.
///
/// WHEN:  As a baseline / reference, or when the data is too small for
///        parallel overhead. Same API as OmpExecutionPolicy / CudaExecutionPolicy,
///        so switching backends is a one-line change.
///
/// WHERE: execution/ExecutionPolicy.hpp.
///
/// HOW:   auto pol = seq_exec();
///        pol(range(0, N), [](int i) { ... });
///        pol.profile(true);  // enable CppTimer instrumentation

static void test_seq_exec_scan() {
  std::printf("[usecase] SequentialExecutionPolicy — prefix sum\n");

  constexpr int N = 1'000'000;
  std::vector<int> input(N);
  std::vector<int> output(N);
  std::iota(input.begin(), input.end(), 1);

  auto pol = seq_exec();

  auto pr = profile("seq_inclusive_scan", N, [&] {
    pol.inclusive_scan(input.begin(), input.end(), output.begin());
  });

  // sum(1..N) = N*(N+1)/2
  require(output.back() == (int)((long long)N * (N + 1) / 2), "scan result");
  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 16. SharedMemoryRegion (NEW — supplementing missing IPC primitive)
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Cross-process shared memory region. Allows two or more OS
///        processes to map the same physical memory.
///
/// WHEN:  Zero-copy data exchange between runtime instances on the same node.
///        E.g. a simulation server exposing its particle buffer to a
///        visualiser process, or a multi-process job scheduler sharing
///        a work-stealing deque.
///
/// WHERE: process/SharedMemoryRegion.hpp (NEW).
///
/// HOW:   // Process A (creator):
///        auto region = SharedMemoryRegion::create("sim_particles", sizeBytes);
///        float *buf = region.as<float>();
///
///        // Process B (opener):
///        auto region = SharedMemoryRegion::open("sim_particles", sizeBytes);
///        const float *buf = region.as<const float>();
///
///        // Both processes see the same memory. Use Mutex/Futex on a shared
///        // atomic header for synchronisation.

static void test_shared_memory_region() {
  std::printf("[usecase] SharedMemoryRegion — create / write / read / destroy\n");

  constexpr size_t regionSize = 4096;
  const char *name = "zpc_test_shmem_region";

  auto pr = profile("shmem_region", 0, [&] {
    // Create
    auto region = SharedMemoryRegion::create(name, regionSize);
    require(region.valid(), "shmem created");
    require(region.size() >= regionSize, "shmem size");

    // Write a pattern
    auto *data = region.as<u32>();
    const size_t count = regionSize / sizeof(u32);
    for (size_t i = 0; i < count; ++i) data[i] = (u32)(i * 0xDEAD);

    // Re-open (simulates another process; in-process test)
    auto mirror = SharedMemoryRegion::open(name, regionSize);
    require(mirror.valid(), "shmem re-opened");

    const auto *mirrorData = mirror.as<const u32>();
    for (size_t i = 0; i < count; ++i)
      require(mirrorData[i] == (u32)(i * 0xDEAD), "shmem data match");
  });

  // Cleanup (destructor unmaps; explicit unlink removes the name)
  SharedMemoryRegion::unlink(name);
  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 17. NamedChannel (NEW — supplementing missing IPC primitive)
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Cross-process bidirectional byte channel implemented over named
///        pipes (Windows) or Unix domain sockets (POSIX).
///
/// WHEN:  Sending structured messages (commands, results, small buffers)
///        between runtime instances. Heavier payloads should combine
///        NamedChannel (for signaling) with SharedMemoryRegion (for data).
///
/// WHERE: process/NamedChannel.hpp (NEW).
///
/// HOW:   // Process A (server):
///        auto server = NamedChannel::listen("zpc_control");
///        auto conn = server.accept();
///        conn.write(buf, len);
///
///        // Process B (client):
///        auto client = NamedChannel::connect("zpc_control");
///        client.read(buf, len);

static void test_named_channel_echo() {
  std::printf("[usecase] NamedChannel — in-process echo loopback\n");

  const char *name = "zpc_test_channel";
  constexpr int rounds = 10'000;
  constexpr size_t msgSize = 64;

  auto pr = profile("named_channel_echo", rounds, [&] {
    auto server = NamedChannel::listen(name);
    require(server.valid(), "channel listen");

    std::thread client_thread([&] {
      auto client = NamedChannel::connect(name);
      require(client.valid(), "channel connect");

      u8 sendBuf[msgSize];
      u8 recvBuf[msgSize];
      for (int i = 0; i < rounds; ++i) {
        std::memset(sendBuf, (u8)(i & 0xFF), msgSize);
        require(client.write(sendBuf, msgSize) == (i64)msgSize, "client write");
        require(client.read(recvBuf, msgSize) == (i64)msgSize, "client read echo");
        require(recvBuf[0] == sendBuf[0], "echo data match");
      }
    });

    auto conn = server.accept();
    require(conn.valid(), "channel accepted");

    u8 buf[msgSize];
    for (int i = 0; i < rounds; ++i) {
      require(conn.read(buf, msgSize) == (i64)msgSize, "server read");
      require(conn.write(buf, msgSize) == (i64)msgSize, "server echo write");
    }

    client_thread.join();
  });

  report(pr);
}

// ═══════════════════════════════════════════════════════════════════════════
// 18. ExecutionGraph (NEW — backend-agnostic resource-tracking graph)
// ═══════════════════════════════════════════════════════════════════════════
/// WHAT:  Declarative execution graph with automatic hazard analysis and
///        topological ordering. Passes declare resource access patterns;
///        the compiler injects synchronisation edges.
///
/// WHEN:  Expressing multi-stage pipelines where resources flow between
///        passes (P2G → grid solve → G2P, or vertex → rasterise → composite).
///        Replaces manual barrier/event/fence management.
///
/// WHERE: execution/ExecutionGraph.hpp (NEW).
///
/// HOW:   ExecutionGraph g;
///        auto buf = g.importResource({"particles", size});
///        auto grid = g.importResource({"grid", size});
///        auto &scatter = g.addPass("P2G", {{buf, read}, {grid, write}}, cb);
///        auto &gather  = g.addPass("G2P", {{grid, read}, {buf, write}}, cb);
///        auto compiled = g.compile();  // topo-sort + hazard edges
///        g.executeInline(compiled);    // or hand to CpuGraphExecutor

static void test_execution_graph_mpm_pipeline() {
  std::printf("[usecase] ExecutionGraph — MPM-style P2G/G2P pipeline\n");

  // ── Build graph ─────────────────────────────────────────────────────
  ExecutionGraph graph;

  auto particles = graph.importResource({"particles", 1 << 20});
  auto grid      = graph.importResource({"grid",      1 << 22});
  auto velocity  = graph.createTransientResource("velocity_field", 1 << 18);

  // Track pass execution order for verification.
  std::vector<int> executionOrder;
  Mutex orderMtx{};

  auto recordExec = [&](int id) {
    std::lock_guard<Mutex> lk(orderMtx);
    executionOrder.push_back(id);
  };

  // Pass 0: particle advection (read+write particles)
  graph.addPass("advect", {
    {particles, AccessMode::read_write, AccessDomain::device_compute}
  }, [&](AsyncExecutionContext &) { recordExec(0); });

  // Pass 1: P2G scatter (read particles, write grid)
  graph.addPass("P2G-scatter", {
    {particles, AccessMode::read,  AccessDomain::device_compute},
    {grid,      AccessMode::write, AccessDomain::device_compute}
  }, [&](AsyncExecutionContext &) { recordExec(1); });

  // Pass 2: grid velocity solve (read+write grid, write velocity)
  graph.addPass("grid-solve", {
    {grid,     AccessMode::read_write, AccessDomain::device_compute},
    {velocity, AccessMode::write,      AccessDomain::device_compute}
  }, [&](AsyncExecutionContext &) { recordExec(2); });

  // Pass 3: G2P gather (read grid + velocity, write particles)
  graph.addPass("G2P-gather", {
    {grid,      AccessMode::read,  AccessDomain::device_compute},
    {velocity,  AccessMode::read,  AccessDomain::device_compute},
    {particles, AccessMode::write, AccessDomain::device_compute}
  }, [&](AsyncExecutionContext &) { recordExec(3); });

  // ── Compile & verify structure ──────────────────────────────────────
  CompiledGraph compiled;
  auto prCompile = profile("exec_graph_compile", 0, [&] {
    compiled = graph.compile();
  });

  require(compiled.valid(), "graph compiled successfully");
  require(compiled.sortedPassIndices.size() == 4, "4 passes in topo order");

  // Verify hazard edges exist:
  // advect(rw particles) → P2G(r particles): RAW on particles
  // P2G(w grid) → grid-solve(rw grid): RAW on grid
  // advect(rw particles) → G2P(w particles): WAW on particles
  // grid-solve(rw grid) → G2P(r grid): RAW on grid
  // grid-solve(w velocity) → G2P(r velocity): RAW on velocity
  require(compiled.syncEdges.size() >= 4, "hazard edges detected");

  // Check that RAW on grid between P2G and grid-solve exists.
  bool foundP2GtoSolve = false;
  for (const auto &edge : compiled.syncEdges) {
    if (edge.srcPass == 1 && edge.dstPass == 2 &&
        edge.hazard == HazardKind::read_after_write) {
      foundP2GtoSolve = true;
    }
  }
  require(foundP2GtoSolve, "RAW edge: P2G → grid-solve");

  // Verify topological order constraints.
  auto posOf = [&](u32 passIdx) -> size_t {
    for (size_t i = 0; i < compiled.sortedPassIndices.size(); ++i)
      if (compiled.sortedPassIndices[i] == passIdx) return i;
    return ~(size_t)0;
  };
  require(posOf(0) < posOf(1), "advect before P2G");
  require(posOf(1) < posOf(2), "P2G before grid-solve");
  require(posOf(2) < posOf(3), "grid-solve before G2P");

  // All 4 passes form a linear hazard chain on the same queue class
  // (device_compute) → compiler should place them all on one lane.
  // This means zero cross-lane syncs (all ordering is lane-internal).
  require(compiled.laneAssignments[0] == compiled.laneAssignments[1],
          "advect and P2G on same lane");
  require(compiled.laneAssignments[1] == compiled.laneAssignments[2],
          "P2G and grid-solve on same lane");
  require(compiled.laneAssignments[2] == compiled.laneAssignments[3],
          "grid-solve and G2P on same lane");
  require(compiled.numCrossLaneSyncs == 0,
          "linear chain: no cross-lane sync needed");

  std::printf("    lanes: %zu  cross-lane syncs: %u\n",
              compiled.lanes.size(), compiled.numCrossLaneSyncs);

  report(prCompile);

  // ── Execute inline (serial baseline) ────────────────────────────────
  auto prInline = profile("exec_graph_inline", 4, [&] {
    graph.executeInline(compiled);
  });

  require(executionOrder.size() == 4, "all 4 passes executed");
  require(executionOrder[0] == (int)compiled.sortedPassIndices[0], "inline order[0]");
  require(executionOrder[1] == (int)compiled.sortedPassIndices[1], "inline order[1]");
  require(executionOrder[2] == (int)compiled.sortedPassIndices[2], "inline order[2]");
  require(executionOrder[3] == (int)compiled.sortedPassIndices[3], "inline order[3]");
  report(prInline);

  // ── Execute parallel (CpuGraphExecutor) ─────────────────────────────
  executionOrder.clear();
  auto prParallel = profile("exec_graph_cpu_parallel", 4, [&] {
    AsyncScheduler scheduler{4};
    CpuGraphExecutor executor{scheduler};
    auto event = executor.execute(graph, compiled);
    event.wait();
  });

  require(executionOrder.size() == 4, "parallel: all 4 passes executed");
  // Verify dependency ordering (not necessarily same as topo order,
  // but advect must precede P2G, P2G must precede solve, solve must
  // precede G2P).
  auto parallelPosOf = [&](int passId) -> size_t {
    for (size_t i = 0; i < executionOrder.size(); ++i)
      if (executionOrder[i] == passId) return i;
    return ~(size_t)0;
  };
  require(parallelPosOf(0) < parallelPosOf(1), "parallel: advect before P2G");
  require(parallelPosOf(1) < parallelPosOf(2), "parallel: P2G before grid-solve");
  require(parallelPosOf(2) < parallelPosOf(3), "parallel: grid-solve before G2P");
  report(prParallel);
}

static void test_execution_graph_independent_passes() {
  std::printf("[usecase] ExecutionGraph — independent passes (parallel opportunity)\n");

  // Two passes that touch completely separate resources → no hazards,
  // no sync edges, both can run in parallel.
  ExecutionGraph graph;

  auto bufA = graph.importResource({"buffer_A", 4096});
  auto bufB = graph.importResource({"buffer_B", 4096});

  std::atomic<int> counter{0};

  graph.addPass("write_A", {
    {bufA, AccessMode::write, AccessDomain::host_parallel}
  }, [&](AsyncExecutionContext &) {
    counter.fetch_add(1);
  });

  graph.addPass("write_B", {
    {bufB, AccessMode::write, AccessDomain::host_parallel}
  }, [&](AsyncExecutionContext &) {
    counter.fetch_add(1);
  });

  auto prCompile = profile("exec_graph_indep_compile", 0, [&] {
    auto compiled = graph.compile();
    require(compiled.valid(), "independent graph compiled");
    require(compiled.syncEdges.empty(), "no hazards between independent passes");
    require(compiled.sortedPassIndices.size() == 2, "2 passes");
    require(compiled.numCrossLaneSyncs == 0, "no cross-lane syncs (no edges at all)");
    // Independent passes on the same queue class with no conflict should
    // be spreadable — but the primary-lane-first heuristic may put both
    // on the same lane if there's no conflict.  Either assignment is valid.
    std::printf("    lanes: %zu\n", compiled.lanes.size());
  });
  report(prCompile);

  auto compiled = graph.compile();
  auto prExec = profile("exec_graph_indep_parallel", 2, [&] {
    AsyncScheduler scheduler{2};
    CpuGraphExecutor executor{scheduler};
    auto event = executor.execute(graph, compiled);
    event.wait();
  });

  require(counter.load() == 2, "both independent passes ran");
  report(prExec);
}

static void test_execution_graph_cycle_detection() {
  std::printf("[usecase] ExecutionGraph — cycle detection\n");

  ExecutionGraph graph;
  auto resA = graph.importResource({"res_A", 64});

  // Two passes that both read_write the same resource.
  // Since they're declared in order, pass0→pass1 (WAW), which is acyclic.
  graph.addPass("step0", {
    {resA, AccessMode::read_write, AccessDomain::host_sequential}
  }, [](AsyncExecutionContext &) {});

  graph.addPass("step1", {
    {resA, AccessMode::read_write, AccessDomain::host_sequential}
  }, [](AsyncExecutionContext &) {});

  auto pr = profile("exec_graph_cycle_check", 0, [&] {
    auto compiled = graph.compile();
    require(compiled.valid(), "sequential RW→RW is acyclic");
    require(compiled.sortedPassIndices.size() == 2, "both passes present");
    require(compiled.sortedPassIndices[0] == 0, "step0 first");
    require(compiled.sortedPassIndices[1] == 1, "step1 second");
    require(!compiled.syncEdges.empty(), "WAW edge present");

    // Both passes are on the same lane (same queue class, WAW hazard).
    require(compiled.laneAssignments[0] == compiled.laneAssignments[1],
            "WAW chain on same lane");
    require(compiled.numCrossLaneSyncs == 0, "no cross-lane sync needed");
  });

  report(pr);
}

static void test_execution_graph_lane_overlap_gpu() {
  std::printf("[usecase] ExecutionGraph — GPU lane overlap (shadow + compute cull)\n");

  // Scenario: a frame pipeline where a shadow-map pass (graphics, ROP-bound)
  // and a GPU-driven culling pass (compute, ALU-bound) operate on separate
  // resources and can overlap on different hardware queues/streams.
  //
  // GPU hardware insight:
  //   - Shadow map mostly consumes ROP (rasteriser output / blend units)
  //   - GPU-driven culling mostly consumes CUDA/shader cores
  //   - These are different fixed-function blocks → near-zero contention
  //   - The graph compiler should assign them to different lanes so an
  //     executor can submit them to concurrent queues/streams.
  //
  // After both complete, the shading pass reads their outputs → RAW
  // dependencies with cross-lane sync (semaphore / event).

  ExecutionGraph graph;

  auto scene     = graph.importResource({"scene_data",   1 << 22});
  auto depth     = graph.importResource({"shadow_depth", 1 << 20});
  auto drawCmds  = graph.importResource({"draw_cmds",    1 << 18});
  auto framebuf  = graph.importResource({"framebuffer",  1 << 22});

  std::atomic<int> counter{0};

  // Pass 0: shadow map — reads scene, writes depth (graphics queue)
  graph.addPass("shadow", {
    {scene, AccessMode::read,  AccessDomain::device_graphics},
    {depth, AccessMode::write, AccessDomain::device_graphics}
  }, [&](AsyncExecutionContext &) { counter.fetch_add(1); });

  // Pass 1: GPU-driven culling — reads scene, writes draw commands (compute queue)
  graph.addPass("cull", {
    {scene,    AccessMode::read,  AccessDomain::device_compute},
    {drawCmds, AccessMode::write, AccessDomain::device_compute}
  }, [&](AsyncExecutionContext &) { counter.fetch_add(1); });

  // Pass 2: shading — reads depth + draw commands + scene, writes framebuffer
  graph.addPass("shade", {
    {depth,    AccessMode::read,  AccessDomain::device_graphics},
    {drawCmds, AccessMode::read,  AccessDomain::device_graphics},
    {scene,    AccessMode::read,  AccessDomain::device_graphics},
    {framebuf, AccessMode::write, AccessDomain::device_graphics}
  }, [&](AsyncExecutionContext &) { counter.fetch_add(1); });

  auto compiled = graph.compile();

  auto pr = profile("exec_graph_lane_overlap_gpu", 0, [&] {
    require(compiled.valid(), "GPU overlap graph compiled");
    require(compiled.sortedPassIndices.size() == 3, "3 passes");

    // ── Lane assignment verification ──────────────────────────────
    // shadow (pass 0) → graphics lane
    // cull   (pass 1) → compute lane
    // These MUST be on different lanes (different queue classes).
    require(compiled.laneAssignments[0] != compiled.laneAssignments[1],
            "shadow and cull on different lanes");

    // Verify queue class assignments.
    require(compiled.queueAssignments[0] == AsyncQueueClass::graphics,
            "shadow → graphics queue");
    require(compiled.queueAssignments[1] == AsyncQueueClass::compute,
            "cull → compute queue");

    // Verify the lanes have different queue classes.
    u32 shadowLane = compiled.laneAssignments[0];
    u32 cullLane   = compiled.laneAssignments[1];
    require(compiled.lanes[shadowLane].queueClass == AsyncQueueClass::graphics,
            "shadow lane is graphics");
    require(compiled.lanes[cullLane].queueClass == AsyncQueueClass::compute,
            "cull lane is compute");

    // ── Sync edge classification ──────────────────────────────────
    // shadow → shade: RAW on depth (cross-lane if shade is on graphics lane
    //                 and shadow is also on graphics lane, that's same-lane;
    //                 depends on where shade gets assigned)
    // cull → shade:   RAW on drawCmds (cross-lane: compute → graphics)
    // These should have cross-lane sync for cull → shade at minimum.

    bool foundCullToShade = false;
    for (const auto &edge : compiled.syncEdges) {
      if (edge.srcPass == 1 && edge.dstPass == 2) {
        foundCullToShade = true;
        require(edge.crossLane, "cull→shade is cross-lane");
        require(edge.hazard == HazardKind::read_after_write,
                "cull→shade is RAW on drawCmds");
      }
    }
    require(foundCullToShade, "RAW edge cull → shade exists");

    // ── Cross-lane count ──────────────────────────────────────────
    require(compiled.numCrossLaneSyncs > 0,
            "at least one cross-lane sync (cull→shade)");

    // ── Lane timeline verification ────────────────────────────────
    // Each lane should have its passes in topo order.
    for (u32 i = 0; i < (u32)compiled.laneTimelines.size(); ++i) {
      const auto &timeline = compiled.laneTimelines[i];
      for (size_t k = 1; k < timeline.size(); ++k) {
        // Passes within a lane respect topo order.
        size_t posA = 0, posB = 0;
        for (size_t t = 0; t < compiled.sortedPassIndices.size(); ++t) {
          if (compiled.sortedPassIndices[t] == timeline[k-1]) posA = t;
          if (compiled.sortedPassIndices[t] == timeline[k])   posB = t;
        }
        require(posA < posB, "lane-internal topo order");
      }
    }
  });

  report(pr);

  // ── Execute parallel and verify all passes run ───────────────────
  auto prExec = profile("exec_graph_lane_overlap_exec", 3, [&] {
    AsyncScheduler scheduler{4};
    CpuGraphExecutor executor{scheduler};
    auto event = executor.execute(graph, compiled);
    event.wait();
  });

  require(counter.load() == 3, "all 3 GPU-overlap passes executed");
  report(prExec);

  std::printf("    lanes: %zu  cross-lane syncs: %u\n",
              compiled.lanes.size(), compiled.numCrossLaneSyncs);
}

static void test_execution_graph_multi_stream_compute() {
  std::printf("[usecase] ExecutionGraph — multi-stream compute overlap\n");

  // Scenario: three independent compute passes, each writing to a separate
  // buffer.  All are device_compute, but since they have no mutual hazard,
  // the compiler should spread them across multiple lanes (= multiple CUDA
  // streams) so the GPU can interleave their warps.
  //
  // Then a reduction pass reads all three → RAW from each, cross-lane sync.

  ExecutionGraph graph;

  auto bufA = graph.importResource({"buf_A", 4096});
  auto bufB = graph.importResource({"buf_B", 4096});
  auto bufC = graph.importResource({"buf_C", 4096});
  auto result = graph.importResource({"result", 4096});

  std::atomic<int> counter{0};

  // Three independent compute writes.
  graph.addPass("fill_A", {
    {bufA, AccessMode::write, AccessDomain::device_compute}
  }, [&](AsyncExecutionContext &) { counter.fetch_add(1); });

  graph.addPass("fill_B", {
    {bufB, AccessMode::write, AccessDomain::device_compute}
  }, [&](AsyncExecutionContext &) { counter.fetch_add(1); });

  graph.addPass("fill_C", {
    {bufC, AccessMode::write, AccessDomain::device_compute}
  }, [&](AsyncExecutionContext &) { counter.fetch_add(1); });

  // Reduction: reads all three, writes result.
  graph.addPass("reduce", {
    {bufA,   AccessMode::read,  AccessDomain::device_compute},
    {bufB,   AccessMode::read,  AccessDomain::device_compute},
    {bufC,   AccessMode::read,  AccessDomain::device_compute},
    {result, AccessMode::write, AccessDomain::device_compute}
  }, [&](AsyncExecutionContext &) { counter.fetch_add(1); });

  auto compiled = graph.compile();

  auto pr = profile("exec_graph_multi_stream", 0, [&] {
    require(compiled.valid(), "multi-stream graph compiled");
    require(compiled.sortedPassIndices.size() == 4, "4 passes");

    // The three fill passes should be on separate lanes (no mutual hazard,
    // all same queue class → compiler creates secondary lanes for overlap).
    u32 laneA = compiled.laneAssignments[0];
    u32 laneB = compiled.laneAssignments[1];
    u32 laneC = compiled.laneAssignments[2];

    // At least two of the three should be on different lanes.
    bool anyDifferent = (laneA != laneB) || (laneB != laneC) || (laneA != laneC);
    require(anyDifferent, "independent compute passes spread across lanes");

    // All three lanes should be compute queue class.
    require(compiled.lanes[laneA].queueClass == AsyncQueueClass::compute, "fill_A compute");
    require(compiled.lanes[laneB].queueClass == AsyncQueueClass::compute, "fill_B compute");
    require(compiled.lanes[laneC].queueClass == AsyncQueueClass::compute, "fill_C compute");

    // The reduction pass has RAW dependencies on all three fill passes.
    // Any fill→reduce edge crossing a lane boundary is a cross-lane sync.
    int crossLaneToReduce = 0;
    for (const auto &edge : compiled.syncEdges) {
      if (edge.dstPass == 3 && edge.crossLane)
        ++crossLaneToReduce;
    }
    // At least some fill→reduce edges should be cross-lane (since fills
    // are on different lanes from each other or from reduce).
    require(crossLaneToReduce > 0, "cross-lane syncs to reduce pass");
  });

  report(pr);

  auto prExec = profile("exec_graph_multi_stream_exec", 4, [&] {
    AsyncScheduler scheduler{4};
    CpuGraphExecutor executor{scheduler};
    auto event = executor.execute(graph, compiled);
    event.wait();
  });

  require(counter.load() == 4, "all 4 multi-stream passes executed");
  report(prExec);

  std::printf("    lanes: %zu  cross-lane syncs: %u\n",
              compiled.lanes.size(), compiled.numCrossLaneSyncs);
}

// ═══════════════════════════════════════════════════════════════════════════
// 19. PassCostHint — cost-aware topo sort tie-breaking
// ═══════════════════════════════════════════════════════════════════════════

static void test_pass_cost_hint_tiebreak() {
  // Two independent passes (no hazard). Pass B has higher cost than A.
  // With equal priority, the compiler should schedule B before A (HEFT:
  // prefer expensive work first to minimise critical-path latency).

  ExecutionGraph g;
  auto r1 = g.importResource({"bufA", 1024});
  auto r2 = g.importResource({"bufB", 1 << 20});

  auto &passA = g.addPass("cheap_pass");
  passA.accesses.push_back({r1, AccessMode::write, AccessDomain::device_compute});

  auto &passB = g.addPass("expensive_pass");
  passB.accesses.push_back({r2, AccessMode::write, AccessDomain::device_compute});

  // Explicit cost hints — B is 100x more expensive.
  passA.costHint.estimatedCycles = 10;
  passA.costHint.userProvided = true;
  passB.costHint.estimatedCycles = 1000;
  passB.costHint.userProvided = true;

  auto compiled = g.compile();
  require(compiled.valid(), "cost-hint graph compiles");

  // Both passes are independent (no hazard), so topo sort can order them
  // arbitrarily. With cost-aware tie-breaking, expensive_pass should come
  // first in sortedPassIndices.
  require(compiled.sortedPassIndices.size() == 2, "two passes");
  require(compiled.sortedPassIndices[0] == passB.index,
          "expensive pass scheduled first (HEFT heuristic)");

  std::printf("    cost-aware tiebreak: [%s, %s]\n",
              g.pass(compiled.sortedPassIndices[0]).label.data(),
              g.pass(compiled.sortedPassIndices[1]).label.data());
}

static void test_pass_cost_hint_auto_deduce() {
  // Test that deduce_cost_hint() produces sensible defaults from declared
  // resource accesses.

  std::vector<ResourceAccessDecl> accesses;
  accesses.push_back({ResourceHandle{1}, AccessMode::read, AccessDomain::device_compute,
                       0, 1 << 20});  // 1 MiB read
  accesses.push_back({ResourceHandle{2}, AccessMode::write, AccessDomain::device_compute,
                       0, 1 << 18});  // 256 KiB write

  auto hint = deduce_cost_hint(accesses, AccessDomain::device_compute);

  require(hint.memoryBytesRead == (1 << 20), "auto-deduce: read bytes");
  require(hint.memoryBytesWritten == (1 << 18), "auto-deduce: write bytes");
  require(hint.affinity == HardwareAffinity::alu, "auto-deduce: compute → ALU");
  require(hint.estimatedCycles > 0, "auto-deduce: non-zero cycles");
  require(!hint.userProvided, "auto-deduce: not user-provided");

  // Graphics domain should include ROP.
  auto gfxHint = deduce_cost_hint({}, AccessDomain::device_graphics);
  require(has_affinity(gfxHint.affinity, HardwareAffinity::rop),
          "auto-deduce: graphics → ROP");
  require(has_affinity(gfxHint.affinity, HardwareAffinity::alu),
          "auto-deduce: graphics → ALU");
}

static void test_hardware_affinity_flags() {
  // Verify bitwise operations on HardwareAffinity.
  auto combined = HardwareAffinity::alu | HardwareAffinity::rop;
  require(has_affinity(combined, HardwareAffinity::alu), "combined has ALU");
  require(has_affinity(combined, HardwareAffinity::rop), "combined has ROP");
  require(!has_affinity(combined, HardwareAffinity::dma), "combined lacks DMA");

  auto masked = combined & HardwareAffinity::rop;
  require(has_affinity(masked, HardwareAffinity::rop), "masked has ROP");
  require(!has_affinity(masked, HardwareAffinity::alu), "masked lacks ALU");
}

// ═══════════════════════════════════════════════════════════════════════════
// 20. Memory abstractions
// ═══════════════════════════════════════════════════════════════════════════

static void test_memsrc_taxonomy() {
  // Verify new memsrc_e values and their tag dispatch.

  // file_mapped
  require(static_cast<unsigned char>(memsrc_e::file_mapped) == 3, "file_mapped == 3");
  require(is_memory_tag(file_mapped_mem_tag{}), "file_mapped is a memory tag");
  require(get_memory_tag_enum(to_memory_source_tag(memsrc_e::file_mapped))
              == memsrc_e::file_mapped,
          "file_mapped round-trips through tag dispatch");

  // shared_ipc
  require(static_cast<unsigned char>(memsrc_e::shared_ipc) == 4, "shared_ipc == 4");
  require(is_memory_tag(shared_ipc_mem_tag{}), "shared_ipc is a memory tag");
  require(get_memory_tag_enum(to_memory_source_tag(memsrc_e::shared_ipc))
              == memsrc_e::shared_ipc,
          "shared_ipc round-trips through tag dispatch");

  // Legacy alias.
  require(memsrc_e::shared == memsrc_e::um, "shared remains alias for um");

  // Name strings.
  require(std::string(get_memory_tag_name(memsrc_e::file_mapped)) == "FILE_MAPPED",
          "file_mapped name string");
  require(std::string(get_memory_tag_name(memsrc_e::shared_ipc)) == "SHARED_IPC",
          "shared_ipc name string");
  require(std::string(get_memory_tag_name(memsrc_e::host)) == "HOST",
          "host name unchanged");

  std::printf("    memsrc_e: host=%u device=%u um=%u file_mapped=%u shared_ipc=%u\n",
              (unsigned)memsrc_e::host, (unsigned)memsrc_e::device, (unsigned)memsrc_e::um,
              (unsigned)memsrc_e::file_mapped, (unsigned)memsrc_e::shared_ipc);
}

static void test_page_access_enum() {
  // Verify PageAccess enum values exist and have distinct values.
  require(static_cast<unsigned char>(PageAccess::none) == 0, "PageAccess::none == 0");
  require(static_cast<unsigned char>(PageAccess::read) == 1, "PageAccess::read == 1");
  require(static_cast<unsigned char>(PageAccess::read_write) == 2, "PageAccess::read_write == 2");
  require(static_cast<unsigned char>(PageAccess::read_exec) == 3, "PageAccess::read_exec == 3");
  require(static_cast<unsigned char>(PageAccess::read_write_exec) == 4,
          "PageAccess::read_write_exec == 4");

  // VmrAllocHint.
  require(static_cast<unsigned char>(VmrAllocHint::none) == 0, "VmrAllocHint::none == 0");
  require(static_cast<unsigned char>(VmrAllocHint::huge_pages) == 1,
          "VmrAllocHint::huge_pages == 1");
}

static void test_byte_stream_stdio_interface() {
  // Verify StdioStream interface — stdout is writable, not seekable.
  StdioStream out(StdioKind::stdout_stream);
  require(out.is_writable(), "stdout is writable");
  require(!out.is_seekable(), "stdout is not seekable");
  require(out.is_open(), "stdout is open");
  require(out.seek(0) == -1, "stdout seek returns -1");
  require(out.tell() == -1, "stdout tell returns -1");
  require(out.size() == -1, "stdout size returns -1");

  // stdin is readable.
  StdioStream in(StdioKind::stdin_stream);
  require(in.is_readable(), "stdin is readable");
  require(!in.is_writable(), "stdin is not writable");

  // stderr is writable.
  StdioStream err(StdioKind::stderr_stream);
  require(err.is_writable(), "stderr is writable");
  require(!err.is_readable(), "stderr is not readable");

  // Write to stdout (should succeed).
  const char *msg = "    [ByteStream] stdio write test\n";
  auto written = out.write(msg, std::strlen(msg));
  require(written > 0, "stdout write succeeded");
}

static void test_mapped_file_interface() {
  // Verify MappedFile type traits — we can't actually mmap in unit tests
  // without a real filesystem path, but we can verify the class exists
  // and inherits correctly.

  // MappedFile inherits from vmr_t which inherits from mr_t.
  static_assert(std::is_base_of_v<vmr_t, MappedFile>,
                "MappedFile derives from vmr_t");
  static_assert(std::is_base_of_v<mr_t, MappedFile>,
                "MappedFile derives from mr_t (transitively)");

  // Check that MappedFileAccess enum has expected values.
  require(static_cast<unsigned char>(MappedFileAccess::read_only) == 0,
          "MappedFileAccess::read_only == 0");
  require(static_cast<unsigned char>(MappedFileAccess::read_write) == 1,
          "MappedFileAccess::read_write == 1");
  require(static_cast<unsigned char>(MappedFileAccess::copy_on_write) == 2,
          "MappedFileAccess::copy_on_write == 2");

  std::printf("    MappedFile type hierarchy verified (vmr_t -> mr_t)\n");
}

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main() {
  std::printf("═══════════════════════════════════════════════════════════════\n");
  std::printf("  ZPC Async / Concurrency Primitive Use-Case Tests\n");
  std::printf("═══════════════════════════════════════════════════════════════\n\n");

  auto run = [](const char *name, auto &&fn) {
    fn();
    std::printf("  [PASS] %s\n\n", name);
  };

  std::printf("── Thread-level primitives ──────────────────────────────────\n");
  run("Futex ping-pong",            test_futex_ping_pong);
  run("Mutex contention",           test_mutex_contention);
  run("ConditionVariable prodcons",  test_condvar_producer_consumer);
  run("Atomic fetch_add",           test_atomics_fetch_add_throughput);

  std::printf("── Lock-free containers ────────────────────────────────────\n");
  run("ConcurrentQueue MPMC",       test_concurrent_queue_mpmc);
  run("SpscQueue",                   test_spsc_queue_throughput);

  std::printf("── Thread management ───────────────────────────────────────\n");
  run("ManagedThread lifecycle",     test_managed_thread_lifecycle);
  run("StopToken cancellation",      test_stop_token_cancellation);

  std::printf("── Async runtime ───────────────────────────────────────────\n");
  run("AsyncEvent fan-in",          test_async_event_fan_in);
  run("AsyncRuntime pipeline",      test_async_runtime_pipeline);

  std::printf("── Scheduler & coroutines ──────────────────────────────────\n");
  run("AsyncScheduler throughput",   test_scheduler_throughput);
  run("Future<T> chain",            test_coroutine_chain);
  run("Generator<T> Fibonacci",     test_generator_lazy_range);
  run("TaskGraph diamond",          test_task_graph_diamond);

  std::printf("── Execution policy ────────────────────────────────────────\n");
  run("SequentialExec scan",        test_seq_exec_scan);

  std::printf("── Process-level IPC (NEW) ─────────────────────────────────\n");
  run("SharedMemoryRegion",         test_shared_memory_region);
  run("NamedChannel echo",          test_named_channel_echo);

  std::printf("── Execution graph (NEW) ───────────────────────────────────\n");
  run("ExecutionGraph MPM pipeline",    test_execution_graph_mpm_pipeline);
  run("ExecutionGraph independent",     test_execution_graph_independent_passes);
  run("ExecutionGraph cycle detection", test_execution_graph_cycle_detection);
  run("ExecutionGraph GPU lane overlap", test_execution_graph_lane_overlap_gpu);
  run("ExecutionGraph multi-stream",    test_execution_graph_multi_stream_compute);

  std::printf("── Pass cost hints (NEW) ───────────────────────────────────\n");
  run("PassCostHint tiebreak",         test_pass_cost_hint_tiebreak);
  run("PassCostHint auto-deduce",      test_pass_cost_hint_auto_deduce);
  run("HardwareAffinity flags",        test_hardware_affinity_flags);

  std::printf("── Memory abstractions (NEW) ───────────────────────────────\n");
  run("memsrc_e taxonomy",             test_memsrc_taxonomy);
  run("PageAccess enum",               test_page_access_enum);
  run("ByteStream StdioStream",        test_byte_stream_stdio_interface);
  run("MappedFile interface",          test_mapped_file_interface);

  std::printf("═══════════════════════════════════════════════════════════════\n");
  std::printf("  All use-case tests passed.\n");
  std::printf("═══════════════════════════════════════════════════════════════\n");
  return 0;
}
