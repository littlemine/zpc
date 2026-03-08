#pragma once

#if defined(__has_include) && !__has_include(<coroutine>)
#  error "AsyncScheduler.hpp requires C++20 coroutine support."
#endif

#include <atomic>
#include <coroutine>

#include "zensim/ZpcAsync.hpp"
#include "zensim/ZpcCoroutine.hpp"
#include "zensim/ZpcFunction.hpp"
#include "zensim/container/ConcurrentQueue.hpp"
#include "zensim/execution/ConcurrencyPrimitive.hpp"
#include "zensim/execution/ManagedThread.hpp"
#include "zensim/types/SmallVector.hpp"

namespace zs {

  /// =========================================================================
  /// CoroTaskNode — DAG node for coroutine-based task scheduling
  /// =========================================================================

  struct CoroTaskNode {
    CoroTaskNode& to(CoroTaskNode& dst) {
      if (_numSuccs < kMaxEdges) _succs[_numSuccs++] = &dst;
      if (dst._numPreds < kMaxEdges) dst._preds[dst._numPreds++] = this;
      dst._numDeps.fetch_add(1, std::memory_order_relaxed);
      return *this;
    }

    enum state_e : u8 { idle = 0, planned, scheduled, running, done };

    static constexpr size_t kMaxEdges = 16;
    CoroTaskNode* _preds[kMaxEdges]{};
    CoroTaskNode* _succs[kMaxEdges]{};
    size_t _numPreds{0};
    size_t _numSuccs{0};
    std::atomic<int> _numDeps{0};
    Future<void> _task{};
    BasicSmallString<> _tag{};
    state_e _state{idle};
  };

  /// =========================================================================
  /// AsyncScheduler — work-stealing scheduler with ManagedThread workers
  /// =========================================================================
  /// Inspired by zs-app Scheduler (Taro) but reimplemented with std-free types.
  ///
  /// Features:
  /// - Global MPMC queue + per-worker local queues
  /// - Work stealing from random peers when local queue is empty
  /// - Coroutine scheduling via co_await scheduler.schedule()
  /// - DAG scheduling via CoroTaskNode
  /// - Futex-based sleep/wake (no condition variables for hot path)

  struct AsyncScheduler {
    static constexpr size_t kMaxWorkers = 32;

    /// Task variant — discriminated union without std::variant
    struct TaskHandle {
      enum kind_e : u8 { empty = 0, normal_fn, once_coro, task_node, stop_signal };

      TaskHandle() noexcept : _kind{empty}, _fn{} {}
      explicit TaskHandle(function<void()> fn) : _kind{normal_fn}, _fn{zs::move(fn)} {}
      explicit TaskHandle(std::coroutine_handle<> h) : _kind{once_coro}, _coro{h} {}
      explicit TaskHandle(CoroTaskNode* n) : _kind{task_node}, _node{n} {}

      static TaskHandle make_stop() {
        TaskHandle t;
        t._kind = stop_signal;
        return t;
      }

      TaskHandle(TaskHandle&& o) noexcept : _kind{o._kind} {
        switch (_kind) {
          case normal_fn:
            ::new (&_fn) function<void()>(zs::move(o._fn));
            break;
          case once_coro:
            _coro = o._coro;
            break;
          case task_node:
            _node = o._node;
            break;
          default:
            break;
        }
        o._kind = empty;
      }
      TaskHandle& operator=(TaskHandle&& o) noexcept {
        if (this != &o) {
          destroy_();
          _kind = o._kind;
          switch (_kind) {
            case normal_fn:
              ::new (&_fn) function<void()>(zs::move(o._fn));
              break;
            case once_coro:
              _coro = o._coro;
              break;
            case task_node:
              _node = o._node;
              break;
            default:
              break;
          }
          o._kind = empty;
        }
        return *this;
      }
      ~TaskHandle() { destroy_(); }

      TaskHandle(const TaskHandle&) = delete;
      TaskHandle& operator=(const TaskHandle&) = delete;

      kind_e kind() const noexcept { return _kind; }
      bool valid() const noexcept { return _kind != empty; }
      function<void()>& as_fn() { return _fn; }
      std::coroutine_handle<>& as_coro() { return _coro; }
      CoroTaskNode*& as_node() { return _node; }

    private:
      void destroy_() {
        if (_kind == normal_fn) _fn.~function();
        _kind = empty;
      }

      kind_e _kind;
      union {
        function<void()> _fn;
        std::coroutine_handle<> _coro;
        CoroTaskNode* _node;
      };
    };

    /// Worker — one per thread
    struct Worker {
      ManagedThread thread{};
      ConcurrentQueue<TaskHandle, 256> localQueue{};
      std::atomic<u32> status{2};  // 0=sleep, 1=busy, 2=signaled
      i32 index{-1};
    };

    explicit AsyncScheduler(size_t numThreads = 4);
    ~AsyncScheduler();

    AsyncScheduler(const AsyncScheduler&) = delete;
    AsyncScheduler& operator=(const AsyncScheduler&) = delete;

    /// Enqueue a plain function
    void enqueue(function<void()> fn, i32 workerId = -1) {
      enqueue_(TaskHandle{zs::move(fn)}, workerId);
    }

    /// Enqueue a coroutine handle (fire-once)
    void enqueue(std::coroutine_handle<> coro, i32 workerId = -1) {
      enqueue_(TaskHandle{coro}, workerId);
    }

    /// Enqueue a DAG task node
    void enqueue(CoroTaskNode* node, i32 workerId = -1) {
      enqueue_(TaskHandle{node}, workerId);
    }

    /// Awaitable — co_await scheduler.schedule() to resume on a worker
    auto schedule(i32 workerId = -1) {
      struct Awaiter : std::suspend_always {
        AsyncScheduler& sched;
        i32 wid;
        Awaiter(AsyncScheduler& s, i32 w) : sched{s}, wid{w} {}
        void await_suspend(std::coroutine_handle<> h) {
          sched.enqueue_(TaskHandle{h}, wid);
        }
      };
      return Awaiter{*this, workerId};
    }

    /// Schedule an entire DAG rooted at `node` (topological walk)
    bool schedule(CoroTaskNode* node);

    /// Block until all jobs complete
    void wait();

    /// Request all workers to stop
    void shutdown();

    size_t numWorkers() const noexcept { return _numWorkers; }
    size_t numJobsRemaining() const noexcept {
      return _remainingJobs.load(std::memory_order_relaxed);
    }
    bool idle() const noexcept { return numJobsRemaining() == 0; }

  private:
    void enqueue_(TaskHandle&& task, i32 workerId = -1);
    void process_(Worker& w, TaskHandle& task);
    void worker_loop_(ManagedThread& self, i32 workerIndex);
    bool try_steal_(Worker& thief, TaskHandle& out);

    Worker* _workers{nullptr};
    size_t _numWorkers{0};
    ConcurrentQueue<TaskHandle, 1024> _globalQueue{};
    std::atomic<size_t> _remainingJobs{0};
    std::atomic<u32> _stealCounter{0};
  };

  // =========================================================================
  // Inline implementations
  // =========================================================================

  inline AsyncScheduler::AsyncScheduler(size_t numThreads) {
    if (numThreads == 0) numThreads = 1;
    if (numThreads > kMaxWorkers) numThreads = kMaxWorkers;
    _numWorkers = numThreads;
    _workers = new Worker[numThreads];

    for (size_t i = 0; i < numThreads; ++i) {
      _workers[i].index = static_cast<i32>(i);
      _workers[i].thread.start(
          [this, i](ManagedThread& self) { worker_loop_(self, static_cast<i32>(i)); },
          "sched-worker");
    }
  }

  inline AsyncScheduler::~AsyncScheduler() {
    shutdown();
    delete[] _workers;
    _workers = nullptr;
  }

  inline void AsyncScheduler::shutdown() {
    for (size_t i = 0; i < _numWorkers; ++i) _workers[i].thread.request_stop();
    // Wake all workers so they notice the stop
    for (size_t i = 0; i < _numWorkers; ++i) {
      _workers[i].status.store(2, std::memory_order_release);
      _workers[i].status.notify_one();
    }
    for (size_t i = 0; i < _numWorkers; ++i) _workers[i].thread.join();
  }

  inline void AsyncScheduler::wait() {
    while (!idle()) {
      // Nudge all workers
      for (size_t i = 0; i < _numWorkers; ++i) {
        u32 expected = 0;
        if (_workers[i].status.compare_exchange_weak(expected, 2, std::memory_order_relaxed))
          _workers[i].status.notify_one();
      }
    }
  }

  inline void AsyncScheduler::enqueue_(TaskHandle&& task, i32 workerId) {
    _remainingJobs.fetch_add(1, std::memory_order_relaxed);

    if (workerId >= 0 && workerId < static_cast<i32>(_numWorkers)) {
      _workers[workerId].localQueue.try_enqueue(zs::move(task));
      _workers[workerId].status.store(2, std::memory_order_release);
      _workers[workerId].status.notify_one();
    } else {
      _globalQueue.try_enqueue(zs::move(task));
      // Wake any sleeping worker
      for (size_t i = 0; i < _numWorkers; ++i) {
        u32 expected = 0;
        if (_workers[i].status.compare_exchange_weak(expected, 2, std::memory_order_relaxed)) {
          _workers[i].status.notify_one();
          break;
        }
      }
    }
  }

  inline void AsyncScheduler::process_(Worker& w, TaskHandle& task) {
    switch (task.kind()) {
      case TaskHandle::normal_fn:
        if (task.as_fn()) task.as_fn()();
        _remainingJobs.fetch_sub(1, std::memory_order_relaxed);
        break;

      case TaskHandle::once_coro:
        if (task.as_coro()) task.as_coro().resume();
        _remainingJobs.fetch_sub(1, std::memory_order_relaxed);
        break;

      case TaskHandle::task_node: {
        auto* node = task.as_node();
        if (node && node->_task.getHandle()) {
          node->_state = CoroTaskNode::running;
          node->_task.getHandle().resume();

          if (node->_task.getHandle().done()) {
            node->_state = CoroTaskNode::done;
            // Propagate to successors
            for (size_t i = 0; i < node->_numSuccs; ++i) {
              auto* succ = node->_succs[i];
              if (succ->_numDeps.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                enqueue_(TaskHandle{succ});
              }
            }
          } else {
            // Re-enqueue for coherence on same worker
            enqueue_(TaskHandle{node}, w.index);
            _remainingJobs.fetch_add(1, std::memory_order_relaxed);  // counteract the sub below
          }
        }
        _remainingJobs.fetch_sub(1, std::memory_order_relaxed);
        break;
      }

      case TaskHandle::stop_signal:
      case TaskHandle::empty:
      default:
        break;
    }
  }

  inline void AsyncScheduler::worker_loop_(ManagedThread& self, i32 workerIndex) {
    auto& w = _workers[workerIndex];

    while (!self.stop_requested()) {
      TaskHandle task;

      // 1. Try local queue first
      if (w.localQueue.try_dequeue(task)) {
        w.status.store(1, std::memory_order_relaxed);
        process_(w, task);
        continue;
      }

      // 2. Try global queue
      if (_globalQueue.try_dequeue(task)) {
        w.status.store(1, std::memory_order_relaxed);
        process_(w, task);
        continue;
      }

      // 3. Try stealing from a peer
      if (try_steal_(w, task)) {
        w.status.store(1, std::memory_order_relaxed);
        process_(w, task);
        continue;
      }

      // 4. Nothing to do — sleep
      w.status.store(0, std::memory_order_release);
      w.status.wait(0);  // futex-based wait (C++20 atomic::wait)
    }
  }

  inline bool AsyncScheduler::try_steal_(Worker& thief, TaskHandle& out) {
    // Round-robin starting from a rotating offset to spread contention
    u32 start = _stealCounter.fetch_add(1, std::memory_order_relaxed) % _numWorkers;
    for (size_t i = 0; i < _numWorkers; ++i) {
      size_t idx = (start + i) % _numWorkers;
      if (static_cast<i32>(idx) == thief.index) continue;
      if (_workers[idx].localQueue.try_dequeue(out)) return true;
    }
    return false;
  }

  inline bool AsyncScheduler::schedule(CoroTaskNode* node) {
    if (!node) return false;
    if (node->_state == CoroTaskNode::planned) return false;  // already visited
    node->_state = CoroTaskNode::planned;

    if (node->_numDeps.load(std::memory_order_relaxed) != 0) {
      for (size_t i = 0; i < node->_numPreds; ++i) {
        if (!schedule(node->_preds[i])) return false;
      }
    } else {
      enqueue(node);
    }
    return true;
  }

}  // namespace zs
