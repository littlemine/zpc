#include <atomic>
#include <cassert>
#include <thread>

#include "zensim/execution/AsyncRuntime.hpp"

namespace {

  struct CountingValue {
    static int liveCount;

    CountingValue() : value{0} { ++liveCount; }
    explicit CountingValue(int v) : value{v} { ++liveCount; }
    CountingValue(const CountingValue &o) : value{o.value} { ++liveCount; }
    CountingValue(CountingValue &&o) noexcept : value{o.value} {
      o.value = -1;
      ++liveCount;
    }
    ~CountingValue() { --liveCount; }

    int value;
  };

  int CountingValue::liveCount = 0;

  struct AwaitingRoutine : zs::AsyncRoutineBase<AwaitingRoutine> {
    AwaitingRoutine(bool *readyIn, int *stateIn) : ready{readyIn}, state{stateIn} {}

    bool *ready;
    int *state;

    zs::AsyncPollStatus operator()(zs::AsyncExecutionContext &) {
      ZS_ASYNC_ROUTINE_BEGIN(this);
      *state = 1;
      ZS_ASYNC_ROUTINE_AWAIT(this, *ready);
      *state = 2;
      ZS_ASYNC_ROUTINE_END(this);
    }
  };

  struct InterruptibleRoutine : zs::AsyncRoutineBase<InterruptibleRoutine> {
    explicit InterruptibleRoutine(int *stateIn) : state{stateIn} {}

    int *state;

    zs::AsyncPollStatus operator()(zs::AsyncExecutionContext &ctx) {
      ZS_ASYNC_ROUTINE_BEGIN(this);
      ZS_ASYNC_ROUTINE_CANCEL_IF_STOPPED(this, ctx.cancellation);
      *state = 7;
      ZS_ASYNC_ROUTINE_END(this);
    }
  };

}  // namespace

int main() {
  using namespace zs;

  {
    StaticVector<int, 2> values;
    assert(values.empty());
    values.push_back(3);
    values.emplace_back(5);
    assert(values.size() == 2);
    assert(values[0] == 3);
    assert(values[1] == 5);

    bool overflowed = false;
    try {
      values.push_back(8);
    } catch (const StaticException<> &) {
      overflowed = true;
    }
    assert(overflowed);

    StaticVector<int, 2> copied{values};
    values.clear();
    assert(values.empty());
    assert(copied.size() == 2);
    assert(copied[0] == 3);
    assert(copied[1] == 5);
  }

  {
    assert(CountingValue::liveCount == 0);
    StaticVector<CountingValue, 2> values;
    values.emplace_back(11);
    values.emplace_back(13);
    assert(CountingValue::liveCount == 2);

    StaticVector<CountingValue, 2> assigned;
    assigned = values;
    assert(CountingValue::liveCount == 4);
    assert(assigned[0].value == 11);
    assert(assigned[1].value == 13);

    values.clear();
    assert(CountingValue::liveCount == 2);
  }
  assert(CountingValue::liveCount == 0);

  {
    AsyncStopSource source;
    auto token = source.token();
    AsyncStopToken tokenCopy = token;
    assert(!token.stop_requested());
    assert(!token.interrupt_requested());

    source.request_interrupt();
    assert(token.interrupt_requested());
    assert(tokenCopy.interrupt_requested());
    assert(!token.stop_requested());

    source.request_stop();
    assert(token.stop_requested());
    assert(tokenCopy.stop_requested());

    AsyncStopSource moved = zs::move(source);
    auto movedToken = moved.token();
    assert(movedToken.stop_requested());
    assert(movedToken.interrupt_requested());
  }

  {
    bool ready = false;
    int state = 0;
    AwaitingRoutine routine{&ready, &state};
    AsyncExecutionContext ctx{};

    assert(routine(ctx) == AsyncPollStatus::suspend);
    assert(state == 1);
    assert(!routine.finished_routine());

    ready = true;
    assert(routine(ctx) == AsyncPollStatus::completed);
    assert(state == 2);
    assert(routine.finished_routine());

    routine.reset_routine();
    state = 0;
    ready = true;
    assert(routine(ctx) == AsyncPollStatus::completed);
    assert(state == 2);
  }

  {
    AsyncStopSource stop;
    stop.request_interrupt();
    int state = 0;
    InterruptibleRoutine routine{&state};
    AsyncExecutionContext ctx{0, nullptr, nullptr, stop.token()};

    assert(routine(ctx) == AsyncPollStatus::cancelled);
    assert(state == 0);
    assert(routine.finished_routine());
  }

  {
    ManagedThread thread;
    assert(!thread.joinable());

    std::atomic<int> phase{0};
    std::atomic<int> iterations{0};
    const bool started = thread.start(
        [&](ManagedThread &self) {
          phase.store(1, std::memory_order_release);
          for (;;) {
            auto token = self.stop_token();
            if (self.stop_requested() || token.interrupt_requested()) break;
            iterations.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
          }
          phase.store(2, std::memory_order_release);
        },
        "foundation-thread");
    assert(started);
    assert(!thread.start([&](ManagedThread &) {}, "duplicate"));

    while (phase.load(std::memory_order_acquire) == 0) std::this_thread::yield();
    assert(thread.joinable());
    thread.request_interrupt();
    thread.request_stop();
    assert(thread.stop_token().interrupt_requested());
    assert(thread.stop_requested());
    assert(thread.join());
    assert(!thread.joinable());
    assert(phase.load(std::memory_order_acquire) == 2);
    assert(iterations.load(std::memory_order_relaxed) > 0);
  }

  return 0;
}