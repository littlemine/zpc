#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

#include "zensim/execution/AsyncRuntime.hpp"

int main() {
  using namespace zs;

  AsyncRuntime runtime{2};
  assert(runtime.contains_executor("inline"));
  assert(runtime.contains_executor("thread_pool"));

  int order[4] = {0, 0, 0, 0};
  std::atomic<int> orderIndex{0};

  auto first = runtime.submit(AsyncSubmission{
      "inline",
      AsyncTaskDesc{"first", AsyncDomain::control, AsyncQueueClass::control},
      make_host_endpoint(),
      [&](AsyncExecutionContext &) {
        order[orderIndex.fetch_add(1)] = 1;
        return AsyncPollStatus::completed;
      }});

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

  assert(second.event().wait_for(std::chrono::seconds(2)));
  assert(second.status() == AsyncTaskStatus::completed);
  assert(orderIndex.load() == 2);
  assert(order[0] == 1);
  assert(order[1] == 2);

  struct CountingRoutine : AsyncRoutineBase<CountingRoutine> {
    explicit CountingRoutine(int *v) : value{v} {}

    AsyncPollStatus operator()(AsyncExecutionContext &ctx) {
      ZS_ASYNC_ROUTINE_BEGIN(this);
      *value += 1;
      ZS_ASYNC_ROUTINE_SUSPEND(this);
      ZS_ASYNC_ROUTINE_CANCEL_IF_STOPPED(this, ctx.cancellation);
      *value += 2;
      ZS_ASYNC_ROUTINE_END(this);
    }

    int *value;
  };

  int routineValue = 0;
  CountingRoutine routine{&routineValue};
  auto routineHandle = runtime.submit(AsyncSubmission{
      "inline",
      AsyncTaskDesc{"routine", AsyncDomain::control, AsyncQueueClass::control},
      make_host_endpoint(),
      [routine](AsyncExecutionContext &ctx) mutable { return routine(ctx); }});

  assert(routineHandle.status() == AsyncTaskStatus::suspended);
  assert(routineValue == 1);
  assert(runtime.resume(routineHandle));
  assert(routineHandle.event().wait_for(std::chrono::seconds(2)));
  assert(routineHandle.status() == AsyncTaskStatus::completed);
  assert(routineValue == 3);

  AsyncStopSource stop;
  int cancelledValue = 0;
  CountingRoutine cancelledRoutine{&cancelledValue};
  auto cancelledHandle = runtime.submit(AsyncSubmission{
      "inline",
      AsyncTaskDesc{"cancelled", AsyncDomain::control, AsyncQueueClass::control},
      make_host_endpoint(),
      [cancelledRoutine](AsyncExecutionContext &ctx) mutable { return cancelledRoutine(ctx); },
      {}, stop.token()});

  stop.request_stop();
  assert(runtime.resume(cancelledHandle));
  assert(cancelledHandle.event().wait_for(std::chrono::seconds(2)));
  assert(cancelledHandle.status() == AsyncTaskStatus::cancelled);
  assert(cancelledValue == 1);

  std::atomic<int> threadCounter{0};
  ManagedThread thread;
  assert(thread.start(
      [&](ManagedThread &self) {
        while (!self.stop_requested()) {
          threadCounter.fetch_add(1);
          std::this_thread::yield();
        }
      },
      "managed"));

  assert(thread.joinable());
  thread.request_stop();
  assert(thread.join());
  assert(threadCounter.load() > 0);

  return 0;
}