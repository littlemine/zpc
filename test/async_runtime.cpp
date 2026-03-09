<<<<<<< HEAD
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
=======
#include <atomic>
#include <cassert>
#include <chrono>
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
#include <thread>

#include "zensim/ZpcAsync.hpp"
#include "zensim/ZpcCoroutine.hpp"
#include "zensim/ZpcTaskGraph.hpp"
#include "zensim/container/ConcurrentQueue.hpp"
#include "zensim/execution/AsyncAwaitables.hpp"
#include "zensim/execution/AsyncRuntime.hpp"
#include "zensim/execution/AsyncScheduler.hpp"

using namespace zs;

<<<<<<< HEAD
static void require(bool condition, const char *message) {
  if (condition) return;
  std::fprintf(stderr, "[async-test] requirement failed: %s\n", message);
  std::fflush(stderr);
  std::abort();
}

=======
// =========================================================================
// Test 1: StaticVector basics
// =========================================================================
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
static void test_static_vector() {
  StaticVector<int, 8> v;
  assert(v.empty());
  assert(v.size() == 0);

  v.push_back(10);
  v.push_back(20);
  v.push_back(30);
  assert(v.size() == 3);
<<<<<<< HEAD
=======
  assert(v[0] == 10);
  assert(v[1] == 20);
  assert(v[2] == 30);
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
  assert(v.front() == 10);
  assert(v.back() == 30);

  v.pop_back();
  assert(v.size() == 2);
  assert(v.back() == 20);
<<<<<<< HEAD
}

static void test_stop_tokens() {
  AsyncStopSource source;
  auto token = source.token();
  auto token2 = token;

  assert(!token.stop_requested());
  assert(!token2.stop_requested());
=======

  v.clear();
  assert(v.empty());
}

// =========================================================================
// Test 2: AsyncStopSource / AsyncStopToken
// =========================================================================
static void test_stop_tokens() {
  AsyncStopSource source;
  auto token = source.token();
  auto token2 = token;  // copy

  assert(!token.stop_requested());
  assert(!token2.stop_requested());
  assert(!source.stop_requested());

>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
  source.request_stop();
  assert(token.stop_requested());
  assert(token2.stop_requested());
  assert(source.stop_requested());

<<<<<<< HEAD
=======
  // Interrupt
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
  assert(!token.interrupt_requested());
  source.request_interrupt();
  assert(token.interrupt_requested());
}

<<<<<<< HEAD
static void test_async_routine() {
  struct CountingRoutine : AsyncRoutineBase<CountingRoutine> {
    explicit CountingRoutine(int *v) : value{v} {}
=======
// =========================================================================
// Test 3: AsyncRoutineBase macros
// =========================================================================
static void test_async_routine() {
  struct CountingRoutine : AsyncRoutineBase<CountingRoutine> {
    explicit CountingRoutine(int* v) : value{v} {}
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)

    AsyncPollStatus operator()(AsyncStopToken cancellation) {
      ZS_ASYNC_ROUTINE_BEGIN(this);
      *value += 1;
      ZS_ASYNC_ROUTINE_SUSPEND(this);
      ZS_ASYNC_ROUTINE_CANCEL_IF_STOPPED(this, cancellation);
      *value += 2;
      ZS_ASYNC_ROUTINE_END(this);
    }

<<<<<<< HEAD
    int *value;
  };

  int value = 0;
  CountingRoutine routine{&value};
  AsyncStopToken none{};
  assert(routine(none) == AsyncPollStatus::suspend);
  assert(value == 1);
  assert(routine(none) == AsyncPollStatus::completed);
  assert(value == 3);
  assert(routine.done());

  int cancelledValue = 0;
  CountingRoutine cancelled{&cancelledValue};
  AsyncStopSource stop;
  auto token = stop.token();
  assert(cancelled(token) == AsyncPollStatus::suspend);
  stop.request_stop();
  assert(cancelled(token) == AsyncPollStatus::cancelled);
  assert(cancelledValue == 1);
}

static void test_managed_thread() {
  atomic<int> counter{0};
  Mutex seenMutex{};
  int seenWorker = 0;
  int enteredWorker = 0;
  int stopBeforeLoop = 0;
  ManagedThread thread;
  const bool started = thread.start(
      [&](ManagedThread &self) {
        seenMutex.lock();
        enteredWorker = 1;
        stopBeforeLoop = self.stop_requested() ? 1 : 0;
        seenMutex.unlock();
        while (!self.stop_requested()) {
          seenMutex.lock();
          seenWorker += 1;
          seenMutex.unlock();
=======
    int* value;
  };

  // Normal execution
  int val = 0;
  CountingRoutine routine{&val};
  AsyncStopToken emptyToken{};

  auto status = routine(emptyToken);
  assert(status == AsyncPollStatus::suspend);
  assert(val == 1);

  status = routine(emptyToken);
  assert(status == AsyncPollStatus::completed);
  assert(val == 3);
  assert(routine.done());

  // Cancelled execution
  int cancelVal = 0;
  CountingRoutine cancelRoutine{&cancelVal};
  AsyncStopSource stopSrc;
  auto cancelToken = stopSrc.token();

  cancelRoutine(cancelToken);  // first step: += 1, suspend
  assert(cancelVal == 1);

  stopSrc.request_stop();
  status = cancelRoutine(cancelToken);  // second step: check stop -> cancel
  assert(status == AsyncPollStatus::cancelled);
  assert(cancelVal == 1);  // did not add 2
}

// =========================================================================
// Test 4: ManagedThread
// =========================================================================
static void test_managed_thread() {
  std::atomic<int> counter{0};
  ManagedThread thread;
  assert(thread.start(
      [&](ManagedThread& self) {
        while (!self.stop_requested()) {
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
          counter.fetch_add(1);
          std::this_thread::yield();
        }
      },
<<<<<<< HEAD
      "managed");
  assert(started);

  assert(thread.joinable());
  size_t spins = 0;
  while (counter.load() < 10 && spins++ < 5'000'000) std::this_thread::yield();
  seenMutex.lock();
  const int seenSnapshot = seenWorker;
  const int enteredSnapshot = enteredWorker;
  const int stopSnapshot = stopBeforeLoop;
  seenMutex.unlock();
  assert(counter.load() >= 10);
  assert(enteredSnapshot == 1);
  assert(stopSnapshot == 0);
  assert(seenSnapshot >= 10);

  thread.request_stop();
  assert(thread.join());
  assert(counter.load() >= 10);
}

static void test_concurrent_queue() {
  ConcurrentQueue<int, 64> queue;
  assert(queue.empty_approx());
  assert(queue.try_enqueue(1));
  assert(queue.try_enqueue(2));
  assert(queue.try_enqueue(3));
  assert(queue.size_approx() == 3);

  int value = 0;
  assert(queue.try_dequeue(value) && value == 1);
  assert(queue.try_dequeue(value) && value == 2);
  assert(queue.try_dequeue(value) && value == 3);
  assert(!queue.try_dequeue(value));
}

static void test_async_event() {
  auto event = AsyncEvent::create();
  assert(!event.ready());
  assert(event.status() == AsyncTaskStatus::pending);

  bool callbackFired = false;
  event.on_complete([&]() { callbackFired = true; });
  event.complete(AsyncTaskStatus::completed);
  assert(event.ready());
  assert(event.status() == AsyncTaskStatus::completed);
  assert(callbackFired);
}

=======
      "test-worker"));

  assert(thread.joinable());
  assert(thread.running());

  // Let it run a bit
  while (counter.load() < 10) std::this_thread::yield();

  thread.request_stop();
  assert(thread.join());
  assert(!thread.joinable());
  assert(counter.load() >= 10);
}

// =========================================================================
// Test 5: ConcurrentQueue (MPMC)
// =========================================================================
static void test_concurrent_queue() {
  ConcurrentQueue<int, 64> q;
  assert(q.empty_approx());

  // Single-threaded enqueue/dequeue
  assert(q.try_enqueue(1));
  assert(q.try_enqueue(2));
  assert(q.try_enqueue(3));
  assert(q.size_approx() == 3);

  int val;
  assert(q.try_dequeue(val));
  assert(val == 1);
  assert(q.try_dequeue(val));
  assert(val == 2);
  assert(q.try_dequeue(val));
  assert(val == 3);
  assert(!q.try_dequeue(val));  // empty

  // Multi-threaded stress test
  ConcurrentQueue<int, 1024> sq;
  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};

  ManagedThread producer;
  producer.start(
      [&](ManagedThread& self) {
        for (int i = 0; i < 500; ++i) {
          while (!sq.try_enqueue(i)) std::this_thread::yield();
          produced.fetch_add(1);
        }
      },
      "producer");

  ManagedThread consumer;
  consumer.start(
      [&](ManagedThread& self) {
        int v;
        while (consumed.load() < 500) {
          if (sq.try_dequeue(v)) consumed.fetch_add(1);
          else std::this_thread::yield();
        }
      },
      "consumer");

  producer.join();
  consumer.join();
  assert(produced.load() == 500);
  assert(consumed.load() == 500);
}

// =========================================================================
// Test 6: AsyncEvent
// =========================================================================
static void test_async_event() {
  auto evt = AsyncEvent::create();
  assert(!evt.ready());
  assert(evt.status() == AsyncTaskStatus::pending);

  // Callback
  bool callbackFired = false;
  evt.on_complete([&]() { callbackFired = true; });

  evt.complete(AsyncTaskStatus::completed);
  assert(evt.ready());
  assert(evt.status() == AsyncTaskStatus::completed);
  assert(callbackFired);

  // Already complete — callback fires immediately
  bool cb2 = false;
  evt.on_complete([&]() { cb2 = true; });
  assert(cb2);
}

// =========================================================================
// Test 7: AsyncRuntime with inline + thread_pool executors
// =========================================================================
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
static void test_async_runtime() {
  AsyncRuntime runtime{2};
  assert(runtime.contains_executor("inline"));
  assert(runtime.contains_executor("thread_pool"));

<<<<<<< HEAD
  int order[4] = {0, 0, 0, 0};
  atomic<int> orderIndex{0};
=======
  // Inline execution
  int order[4] = {0, 0, 0, 0};
  std::atomic<int> orderIndex{0};
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)

  auto first = runtime.submit(AsyncSubmission{
      "inline",
      AsyncTaskDesc{"first", AsyncDomain::control, AsyncQueueClass::control},
      make_host_endpoint(),
<<<<<<< HEAD
      [&](AsyncExecutionContext &) {
        order[orderIndex.fetch_add(1)] = 1;
        return AsyncPollStatus::completed;
      }});
  assert(first.valid());
  assert(first.event().ready());

  AsyncSubmission secondSubmission{};
  secondSubmission.executor = "thread_pool";
  secondSubmission.desc = AsyncTaskDesc{"second", AsyncDomain::thread, AsyncQueueClass::compute};
  secondSubmission.endpoint = make_host_endpoint(AsyncBackend::thread_pool,
                                                 AsyncQueueClass::compute, "thread-pool");
  secondSubmission.step = [&](AsyncExecutionContext &) {
    order[orderIndex.fetch_add(1)] = 2;
    return AsyncPollStatus::completed;
  };
  secondSubmission.prerequisites.push_back(first.event());
  auto second = runtime.submit(zs::move(secondSubmission));

  assert(second.event().wait_for(2000));
=======
      [&](AsyncExecutionContext&) {
        order[orderIndex.fetch_add(1)] = 1;
        return AsyncPollStatus::completed;
      }});

  assert(first.valid());
  assert(first.event().ready());

  // Thread pool execution with dependency
  AsyncSubmission secondSub{};
  secondSub.executor = "thread_pool";
  secondSub.desc = AsyncTaskDesc{"second", AsyncDomain::thread, AsyncQueueClass::compute};
  secondSub.endpoint
      = make_host_endpoint(AsyncBackend::thread_pool, AsyncQueueClass::compute, "thread-pool");
  secondSub.step = [&](AsyncExecutionContext&) {
    order[orderIndex.fetch_add(1)] = 2;
    return AsyncPollStatus::completed;
  };
  secondSub.prerequisites.push_back(first.event());
  auto second = runtime.submit(zs::move(secondSub));

  assert(second.event().wait_for(2000));  // 2 seconds
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
  assert(second.status() == AsyncTaskStatus::completed);
  assert(orderIndex.load() == 2);
  assert(order[0] == 1);
  assert(order[1] == 2);
}

<<<<<<< HEAD
=======
// =========================================================================
// Test 8: Resumable routine via AsyncRuntime
// =========================================================================
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
static void test_resumable_routine() {
  AsyncRuntime runtime{1};

  struct StepRoutine : AsyncRoutineBase<StepRoutine> {
<<<<<<< HEAD
    explicit StepRoutine(int *v) : value{v} {}

    AsyncPollStatus operator()(AsyncExecutionContext &ctx) {
=======
    explicit StepRoutine(int* v) : value{v} {}
    AsyncPollStatus operator()(AsyncExecutionContext& ctx) {
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
      ZS_ASYNC_ROUTINE_BEGIN(this);
      *value += 1;
      ZS_ASYNC_ROUTINE_SUSPEND(this);
      ZS_ASYNC_ROUTINE_CANCEL_IF_STOPPED(this, ctx.cancellation);
      *value += 2;
      ZS_ASYNC_ROUTINE_END(this);
    }
<<<<<<< HEAD

    int *value;
=======
    int* value;
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
  };

  int routineValue = 0;
  StepRoutine routine{&routineValue};
  auto handle = runtime.submit(AsyncSubmission{
      "inline",
      AsyncTaskDesc{"routine", AsyncDomain::control, AsyncQueueClass::control},
      make_host_endpoint(),
<<<<<<< HEAD
      [routine](AsyncExecutionContext &ctx) mutable { return routine(ctx); }});

  assert(handle.status() == AsyncTaskStatus::suspended);
  assert(routineValue == 1);
=======
      [routine](AsyncExecutionContext& ctx) mutable { return routine(ctx); }});

  assert(handle.status() == AsyncTaskStatus::suspended);
  assert(routineValue == 1);

>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
  assert(runtime.resume(handle));
  assert(handle.event().wait_for(2000));
  assert(handle.status() == AsyncTaskStatus::completed);
  assert(routineValue == 3);
}

<<<<<<< HEAD
static void test_coroutines() {
  auto simpleTask = []() -> Task {
    co_return;
  };
  auto task = simpleTask();
  assert(!task.isDone());
  task.resume();
  assert(task.isDone());

  auto add = [](int a, int b) -> Future<int> {
    co_return a + b;
  };
  auto future = add(3, 4);
  future.resume();
  assert(future.isDone());
  assert(future.get() == 7);

  auto outer = []() -> Future<int> {
    auto inner = []() -> Future<int> {
      co_return 42;
    };
    int value = co_await inner();
    co_return value * 2;
  };
  assert(sync_wait(outer()) == 84);
}

=======
// =========================================================================
// Test 9: C++20 Coroutines — Future<R>, Task
// =========================================================================
static void test_coroutines() {
  // Simple void task
  auto simple_task = []() -> Task {
    co_return;
  };

  auto t = simple_task();
  assert(!t.isDone());
  t.resume();
  assert(t.isDone());

  // Value-returning future
  auto add = [](int a, int b) -> Future<int> {
    co_return a + b;
  };

  auto f = add(3, 4);
  f.resume();
  assert(f.isDone());
  assert(f.get() == 7);

  // Chained coroutines
  auto inner = []() -> Future<int> {
    co_return 42;
  };
  auto outer = [&]() -> Future<int> {
    int val = co_await inner();
    co_return val * 2;
  };

  auto chain = outer();
  sync_wait(zs::move(chain));
  // chain is consumed by sync_wait, but the test is that it didn't crash/deadlock
}

// =========================================================================
// Test 10: Generator
// =========================================================================
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
static void test_generator() {
  auto range = [](int start, int end) -> Generator<int> {
    for (int i = start; i < end; ++i) co_yield i;
  };

<<<<<<< HEAD
  int expected = 0;
  for (auto &value : range(0, 5)) {
    assert(value == expected++);
=======
  auto gen = range(0, 5);
  int expected = 0;
  for (auto& val : gen) {
    assert(val == expected++);
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
  }
  assert(expected == 5);
}

<<<<<<< HEAD
static void test_scheduler() {
  AsyncScheduler scheduler{4};
  atomic<int> counter{0};
  constexpr int N = 100;

  for (int i = 0; i < N; ++i) {
    scheduler.enqueue([&counter]() { counter.fetch_add(1); });
  }

  size_t spins = 0;
  while (counter.load() < N && scheduler.numJobsRemaining() != 0 && spins++ < 5'000'000)
    std::this_thread::yield();
  std::printf("[async-test] scheduler counter=%d jobs=%zu spins=%zu\n", counter.load(),
              scheduler.numJobsRemaining(), spins);
  std::fflush(stdout);

  scheduler.wait();
  assert(counter.load() == N);
}

static void test_scheduler_pressure() {
  AsyncScheduler scheduler{4};
  atomic<int> counter{0};
  constexpr int N = 4096;

  for (int i = 0; i < N; ++i) {
    scheduler.enqueue([&counter]() { counter.fetch_add(1); });
  }

  size_t spins = 0;
  while (counter.load() < N && scheduler.numJobsRemaining() != 0 && spins++ < 10'000'000)
    std::this_thread::yield();
  std::printf("[async-test] scheduler_pressure counter=%d jobs=%zu spins=%zu\n",
              counter.load(), scheduler.numJobsRemaining(), spins);
  std::fflush(stdout);

  scheduler.wait();
  assert(counter.load() == N);
}

static void test_scheduler_runtime_interop() {
  AsyncScheduler scheduler{2};
  AsyncRuntime runtime{2};
  atomic<int> produced{0};
  atomic<int> runtimeSteps{0};
  AsyncFlag schedulerReady{};
  std::thread::id scheduledThreadId{};
  std::thread::id runtimeThreadId{};
  std::thread::id resumedThreadId{};

  scheduler.enqueue([&]() {
    schedulerReady.signal();
  });

  size_t readySpins = 0;
  while (!schedulerReady.is_signaled() && readySpins++ < 50'000) {
    scheduler.wait();
    std::this_thread::yield();
  }
  require(schedulerReady.is_signaled(), "scheduler thread did not become ready");

  auto flow = [&]() -> Future<int> {
    co_await schedule_on(scheduler);
    scheduledThreadId = std::this_thread::get_id();

    auto handle = runtime.submit(AsyncSubmission{
        "thread_pool",
        AsyncTaskDesc{"interop", AsyncDomain::thread, AsyncQueueClass::compute},
        make_host_endpoint(AsyncBackend::thread_pool, AsyncQueueClass::compute, "interop"),
        [&](AsyncExecutionContext &) {
          runtimeThreadId = std::this_thread::get_id();
          runtimeSteps.fetch_add(1);
          produced.store(21);
          return AsyncPollStatus::completed;
        }});

    auto status = co_await co_await_submission_on(handle, scheduler);
    require(status == AsyncTaskStatus::completed, "runtime submission did not complete");
    resumedThreadId = std::this_thread::get_id();
    co_return produced.load() * 2;
  };

  auto future = flow();
  future.resume();
  while (!future.isDone()) {
    scheduler.wait();
    std::this_thread::yield();
  }

    require(future.get() == 42, "scheduler/runtime interop produced unexpected result");
    require(runtimeSteps.load() == 1, "runtime submission executed unexpected number of steps");
    require(runtimeThreadId != std::thread::id{}, "runtime worker thread was not recorded");
    require(scheduledThreadId != std::thread::id{},
      "scheduler/runtime interop did not record the owning scheduler thread");
    require(resumedThreadId == scheduledThreadId,
      "scheduler/runtime interop did not resume on the owning scheduler thread");
    require(resumedThreadId != runtimeThreadId,
      "scheduler/runtime interop resumed on the runtime worker thread");
}

static void test_task_graph() {
  AsyncScheduler scheduler{2};
  atomic<int> order{0};
  int results[3] = {0, 0, 0};

  TaskGraph graph;
  auto *a = graph.addNode([&]() { results[0] = order.fetch_add(1) + 1; }, "A");
  auto *b = graph.addNode([&]() { results[1] = order.fetch_add(1) + 1; }, "B");
  auto *c = graph.addNode([&]() { results[2] = order.fetch_add(1) + 1; }, "C");
  assert(graph.numNodes() == 3);
  assert(a != nullptr);
  assert(b != nullptr);
  assert(c != nullptr);
  graph.addEdge(a, c);
  graph.addEdge(b, c);

  graph.submit(scheduler);
  graph.wait(scheduler);

  assert(graph.allDone());
  assert(results[2] == 3);
=======
// =========================================================================
// Test 11: AsyncScheduler — work-stealing
// =========================================================================
static void test_scheduler() {
  AsyncScheduler sched{4};
  std::atomic<int> counter{0};
  constexpr int N = 100;

  for (int i = 0; i < N; ++i) {
    sched.enqueue([&counter]() { counter.fetch_add(1); });
  }

  sched.wait();
  assert(counter.load() == N);
}

// =========================================================================
// Test 12: TaskGraph — DAG scheduling
// =========================================================================
static void test_task_graph() {
  AsyncScheduler sched{2};
  std::atomic<int> order{0};
  int results[3] = {0, 0, 0};

  TaskGraph graph;
  auto* a = graph.addNode(
      [&]() {
        results[0] = order.fetch_add(1) + 1;
      },
      "A");
  auto* b = graph.addNode(
      [&]() {
        results[1] = order.fetch_add(1) + 1;
      },
      "B");
  auto* c = graph.addNode(
      [&]() {
        results[2] = order.fetch_add(1) + 1;
      },
      "C");

  // C depends on both A and B
  graph.addEdge(a, c);
  graph.addEdge(b, c);

  graph.submit(sched);
  graph.wait(sched);

  // C must be last
  assert(results[2] == 3);
  // A and B can be in either order, but both before C
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
  assert(results[0] < results[2]);
  assert(results[1] < results[2]);
}

<<<<<<< HEAD
int main() {
  auto run = [](const char *name, auto &&fn) {
    std::printf("[async-test] %s\n", name);
    std::fflush(stdout);
    fn();
  };

  run("static_vector", test_static_vector);
  run("stop_tokens", test_stop_tokens);
  run("async_routine", test_async_routine);
  run("managed_thread", test_managed_thread);
  run("concurrent_queue", test_concurrent_queue);
  run("async_event", test_async_event);
  run("async_runtime", test_async_runtime);
  run("resumable_routine", test_resumable_routine);
  run("coroutines", test_coroutines);
  run("generator", test_generator);
  run("scheduler", test_scheduler);
  run("scheduler_pressure", test_scheduler_pressure);
  run("scheduler_runtime_interop", test_scheduler_runtime_interop);
  run("task_graph", test_task_graph);

  std::puts("All async runtime tests passed.");
=======
// =========================================================================
// Main
// =========================================================================
int main() {
  printf("[1/12] test_static_vector...\n"); fflush(stdout);
  test_static_vector();
  printf("[2/12] test_stop_tokens...\n"); fflush(stdout);
  test_stop_tokens();
  printf("[3/12] test_async_routine...\n"); fflush(stdout);
  test_async_routine();
  printf("[4/12] test_managed_thread...\n"); fflush(stdout);
  test_managed_thread();
  printf("[5/12] test_concurrent_queue...\n"); fflush(stdout);
  test_concurrent_queue();
  printf("[6/12] test_async_event...\n"); fflush(stdout);
  test_async_event();
  printf("[7/12] test_async_runtime...\n"); fflush(stdout);
  test_async_runtime();
  printf("[8/12] test_resumable_routine...\n"); fflush(stdout);
  test_resumable_routine();
  printf("[9/12] test_coroutines...\n"); fflush(stdout);
  test_coroutines();
  printf("[10/12] test_generator...\n"); fflush(stdout);
  test_generator();
  printf("[11/12] test_scheduler...\n"); fflush(stdout);
  test_scheduler();
  printf("[12/12] test_task_graph...\n"); fflush(stdout);
  test_task_graph();

  printf("All tests passed!\n");
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
  return 0;
}
