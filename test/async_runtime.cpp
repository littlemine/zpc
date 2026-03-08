#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

#include "zensim/ZpcAsync.hpp"
#include "zensim/ZpcCoroutine.hpp"
#include "zensim/ZpcTaskGraph.hpp"
#include "zensim/container/ConcurrentQueue.hpp"
#include "zensim/execution/AsyncAwaitables.hpp"
#include "zensim/execution/AsyncRuntime.hpp"
#include "zensim/execution/AsyncScheduler.hpp"

using namespace zs;

// =========================================================================
// Test 1: StaticVector basics
// =========================================================================
static void test_static_vector() {
  StaticVector<int, 8> v;
  assert(v.empty());
  assert(v.size() == 0);

  v.push_back(10);
  v.push_back(20);
  v.push_back(30);
  assert(v.size() == 3);
  assert(v[0] == 10);
  assert(v[1] == 20);
  assert(v[2] == 30);
  assert(v.front() == 10);
  assert(v.back() == 30);

  v.pop_back();
  assert(v.size() == 2);
  assert(v.back() == 20);

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

  source.request_stop();
  assert(token.stop_requested());
  assert(token2.stop_requested());
  assert(source.stop_requested());

  // Interrupt
  assert(!token.interrupt_requested());
  source.request_interrupt();
  assert(token.interrupt_requested());
}

// =========================================================================
// Test 3: AsyncRoutineBase macros
// =========================================================================
static void test_async_routine() {
  struct CountingRoutine : AsyncRoutineBase<CountingRoutine> {
    explicit CountingRoutine(int* v) : value{v} {}

    AsyncPollStatus operator()(AsyncStopToken cancellation) {
      ZS_ASYNC_ROUTINE_BEGIN(this);
      *value += 1;
      ZS_ASYNC_ROUTINE_SUSPEND(this);
      ZS_ASYNC_ROUTINE_CANCEL_IF_STOPPED(this, cancellation);
      *value += 2;
      ZS_ASYNC_ROUTINE_END(this);
    }

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
          counter.fetch_add(1);
          std::this_thread::yield();
        }
      },
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
static void test_async_runtime() {
  AsyncRuntime runtime{2};
  assert(runtime.contains_executor("inline"));
  assert(runtime.contains_executor("thread_pool"));

  // Inline execution
  int order[4] = {0, 0, 0, 0};
  std::atomic<int> orderIndex{0};

  auto first = runtime.submit(AsyncSubmission{
      "inline",
      AsyncTaskDesc{"first", AsyncDomain::control, AsyncQueueClass::control},
      make_host_endpoint(),
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
  assert(second.status() == AsyncTaskStatus::completed);
  assert(orderIndex.load() == 2);
  assert(order[0] == 1);
  assert(order[1] == 2);
}

// =========================================================================
// Test 8: Resumable routine via AsyncRuntime
// =========================================================================
static void test_resumable_routine() {
  AsyncRuntime runtime{1};

  struct StepRoutine : AsyncRoutineBase<StepRoutine> {
    explicit StepRoutine(int* v) : value{v} {}
    AsyncPollStatus operator()(AsyncExecutionContext& ctx) {
      ZS_ASYNC_ROUTINE_BEGIN(this);
      *value += 1;
      ZS_ASYNC_ROUTINE_SUSPEND(this);
      ZS_ASYNC_ROUTINE_CANCEL_IF_STOPPED(this, ctx.cancellation);
      *value += 2;
      ZS_ASYNC_ROUTINE_END(this);
    }
    int* value;
  };

  int routineValue = 0;
  StepRoutine routine{&routineValue};
  auto handle = runtime.submit(AsyncSubmission{
      "inline",
      AsyncTaskDesc{"routine", AsyncDomain::control, AsyncQueueClass::control},
      make_host_endpoint(),
      [routine](AsyncExecutionContext& ctx) mutable { return routine(ctx); }});

  assert(handle.status() == AsyncTaskStatus::suspended);
  assert(routineValue == 1);

  assert(runtime.resume(handle));
  assert(handle.event().wait_for(2000));
  assert(handle.status() == AsyncTaskStatus::completed);
  assert(routineValue == 3);
}

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
static void test_generator() {
  auto range = [](int start, int end) -> Generator<int> {
    for (int i = start; i < end; ++i) co_yield i;
  };

  auto gen = range(0, 5);
  int expected = 0;
  for (auto& val : gen) {
    assert(val == expected++);
  }
  assert(expected == 5);
}

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
  assert(results[0] < results[2]);
  assert(results[1] < results[2]);
}

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
  return 0;
}
