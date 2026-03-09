#pragma once

#if defined(__has_include) && !__has_include(<coroutine>)
#  error "AsyncScheduler.hpp requires C++20 coroutine support."
#endif

#include <coroutine>

#include "zensim/ZpcAsync.hpp"
#include "zensim/ZpcCoroutine.hpp"
#include "zensim/ZpcFunction.hpp"
#include "zensim/container/ConcurrentQueue.hpp"
#include "zensim/execution/AsyncMemoryPool.hpp"
#include "zensim/execution/Intrinsics.hpp"
#include "zensim/execution/ManagedThread.hpp"

namespace zs {

  struct CoroTaskNode;

  struct CoroTaskEdge {
    CoroTaskNode *node{nullptr};
    CoroTaskEdge *next{nullptr};
  };

  struct CoroTaskNode {
    CoroTaskNode &to(CoroTaskNode &dst) {
      append_edge_(_succs, &dst, _numSuccs);
      dst.append_edge_(dst._preds, this, dst._numPreds);
      dst._numDeps.fetch_add(1);
      return *this;
    }

    ~CoroTaskNode() {
      release_edges_(_preds);
      release_edges_(_succs);
    }

    enum state_e : u8 { idle = 0, planned, scheduled, running, done };

    template <typename Fn>
    void for_each_predecessor(Fn &&fn) const {
      for (auto *edge = _preds.load(); edge; edge = edge->next) fn(edge->node);
    }

    template <typename Fn>
    void for_each_successor(Fn &&fn) const {
      for (auto *edge = _succs.load(); edge; edge = edge->next) fn(edge->node);
    }

    Atomic<CoroTaskEdge *> _preds{nullptr};
    Atomic<CoroTaskEdge *> _succs{nullptr};
    atomic_size_t _numPreds{0};
    atomic_size_t _numSuccs{0};
    Atomic<int> _numDeps{0};
    Future<void> _task{};
    BasicSmallString<> _tag{};

    Atomic<state_e> _state{idle};

  private:
    void append_edge_(Atomic<CoroTaskEdge *> &head, CoroTaskNode *node, atomic_size_t &count) {
      auto *edge = detail::async_pool<CoroTaskEdge, 1024>().acquire();
      edge->node = node;
      CoroTaskEdge *expected = head.load();
      do {
        edge->next = expected;
      } while (!head.compare_exchange_weak(expected, edge));
      count.fetch_add(1);
    }

    void release_edges_(Atomic<CoroTaskEdge *> &head) noexcept {
      auto *edge = head.exchange(nullptr);
      while (edge) {
        auto *next = edge->next;
        detail::async_pool<CoroTaskEdge, 1024>().release(edge);
        edge = next;
      }
    }
  };

  struct AsyncScheduler {
    static constexpr size_t kMaxWorkers = 32;
    inline static thread_local const AsyncScheduler *_currentScheduler{nullptr};
    inline static thread_local i32 _currentWorkerId{-1};

    struct TaskHandle {
      enum kind_e : u8 { empty = 0, normal_fn, once_coro, task_node, stop_signal };

      TaskHandle() noexcept : _kind{empty}, _node{nullptr} {}
      explicit TaskHandle(function<void()> fn) : _kind{normal_fn}, _fn{zs::move(fn)} {}
      explicit TaskHandle(std::coroutine_handle<> handle) : _kind{once_coro}, _coro{handle} {}
      explicit TaskHandle(CoroTaskNode *node) : _kind{task_node}, _node{node} {}

      static TaskHandle make_stop() {
        TaskHandle task;
        task._kind = stop_signal;
        return task;
      }

      TaskHandle(TaskHandle &&other) noexcept : _kind{other._kind} {
        switch (_kind) {
          case normal_fn:
            ::new (&_fn) function<void()>(zs::move(other._fn));
            break;
          case once_coro:
            _coro = other._coro;
            break;
          case task_node:
            _node = other._node;
            break;
          default:
            break;
        }
        other._kind = empty;
      }

      TaskHandle &operator=(TaskHandle &&other) noexcept {
        if (this != &other) {
          destroy_();
          _kind = other._kind;
          switch (_kind) {
            case normal_fn:
              ::new (&_fn) function<void()>(zs::move(other._fn));
              break;
            case once_coro:
              _coro = other._coro;
              break;
            case task_node:
              _node = other._node;
              break;
            default:
              break;
          }
          other._kind = empty;
        }
        return *this;
      }

      ~TaskHandle() { destroy_(); }

      TaskHandle(const TaskHandle &) = delete;
      TaskHandle &operator=(const TaskHandle &) = delete;

      kind_e kind() const noexcept { return _kind; }
      bool valid() const noexcept { return _kind != empty; }
      function<void()> &as_fn() { return _fn; }
      std::coroutine_handle<> &as_coro() { return _coro; }
      CoroTaskNode *&as_node() { return _node; }

    private:
      void destroy_() {
        if (_kind == normal_fn) _fn.~function();
        _kind = empty;
      }

      kind_e _kind;
      union {
        function<void()> _fn;
        std::coroutine_handle<> _coro;
        CoroTaskNode *_node;
      };
    };

    struct alignas(64) Worker {
      ManagedThread thread{};
      ConcurrentQueue<TaskHandle, 256> localQueue{};
      Atomic<u32> status{2};
      i32 index{-1};
    };

    explicit AsyncScheduler(size_t numThreads = 4);
    ~AsyncScheduler();

    AsyncScheduler(const AsyncScheduler &) = delete;
    AsyncScheduler &operator=(const AsyncScheduler &) = delete;

    void enqueue(function<void()> fn, i32 workerId = -1) { enqueue_(TaskHandle{zs::move(fn)}, workerId); }
    void enqueue(std::coroutine_handle<> coro, i32 workerId = -1) { enqueue_(TaskHandle{coro}, workerId); }
    void enqueue(CoroTaskNode *node, i32 workerId = -1) { enqueue_(TaskHandle{node}, workerId); }

    auto schedule(i32 workerId = -1) {
      struct Awaiter : std::suspend_always {
        AsyncScheduler &scheduler;
        i32 worker;
        Awaiter(AsyncScheduler &sched, i32 wid) : scheduler{sched}, worker{wid} {}
        void await_suspend(std::coroutine_handle<> handle) { scheduler.enqueue_(TaskHandle{handle}, worker); }
      };
      return Awaiter{*this, workerId};
    }

    bool schedule(CoroTaskNode *node);
    void wait();
    void shutdown();

    size_t numWorkers() const noexcept { return _numWorkers; }
    size_t numJobsRemaining() const noexcept { return _remainingJobs.load(); }
    bool idle() const noexcept { return numJobsRemaining() == 0; }
    i32 current_worker_id() const noexcept {
      return _currentScheduler == this ? _currentWorkerId : -1;
    }
    void pause() noexcept { _pauseState.store(1); }
    void resume() noexcept {
      if (_pauseState.exchange(0) != 0) {
        _pauseState.notify_all();
        wake_any_worker_();
      }
    }
    bool paused() const noexcept { return _pauseState.load() != 0; }
    i32 resolve_worker_id(i32 workerId = -1) const noexcept {
      if (workerId >= 0) return workerId;
      return current_worker_id();
    }

  private:
    void enqueue_(TaskHandle &&task, i32 workerId = -1);
    void process_(Worker &worker, TaskHandle &task);
    void worker_loop_(ManagedThread &self, i32 workerIndex);
    bool try_steal_(Worker &thief, TaskHandle &out);
    void wake_worker_(Worker &worker) noexcept;
    void wake_any_worker_() noexcept;

    Worker *_workers{nullptr};
    size_t _numWorkers{0};
    ConcurrentQueue<TaskHandle, 1024> _globalQueue{};
    atomic_size_t _remainingJobs{0};
    Atomic<u32> _stealCounter{0};
    Atomic<u32> _pauseState{0};
  };

  inline AsyncScheduler::AsyncScheduler(size_t numThreads) {
    if (numThreads == 0) numThreads = 1;
    if (numThreads > kMaxWorkers) numThreads = kMaxWorkers;
    _numWorkers = numThreads;
    _workers = new Worker[numThreads];

    for (size_t i = 0; i < numThreads; ++i) {
      _workers[i].index = static_cast<i32>(i);
      _workers[i].thread.start(
          [this, i](ManagedThread &self) { worker_loop_(self, static_cast<i32>(i)); },
          "sched-worker");
    }
  }

  inline AsyncScheduler::~AsyncScheduler() {
    shutdown();
    delete[] _workers;
    _workers = nullptr;
  }

  inline void AsyncScheduler::shutdown() {
    if (!_workers) return;
    _pauseState.store(0);
    _pauseState.notify_all();
    for (size_t i = 0; i < _numWorkers; ++i) _workers[i].thread.request_stop();
    for (size_t i = 0; i < _numWorkers; ++i) {
      _workers[i].status.store(2);
      _workers[i].status.notify_one();
    }
    for (size_t i = 0; i < _numWorkers; ++i) _workers[i].thread.join();
  }

  inline void AsyncScheduler::wait() {
    size_t attempts = 0;
    while (!idle()) {
      wake_any_worker_();
      if (attempts < 64)
        pause_cpu();
      else
        ManagedThread::yield_current();
      ++attempts;
    }
  }

  inline void AsyncScheduler::wake_worker_(Worker &worker) noexcept {
    worker.status.store(2);
    worker.status.notify_one();
  }

  inline void AsyncScheduler::wake_any_worker_() noexcept {
    for (size_t i = 0; i < _numWorkers; ++i) {
      u32 expected = 0;
      if (_workers[i].status.compare_exchange_weak(expected, 2)) {
        _workers[i].status.notify_one();
        return;
      }
    }

    if (_numWorkers != 0) {
        _workers[_stealCounter.fetch_add(1) % _numWorkers]
          .status.notify_one();
    }
  }

  inline void AsyncScheduler::enqueue_(TaskHandle &&task, i32 workerId) {
    if (workerId >= 0 && workerId < static_cast<i32>(_numWorkers)) {
      const auto currentWorker = current_worker_id();
      auto &worker = _workers[workerId];
      const bool keepLocal = currentWorker == workerId || worker.status.load() == 0;
      if (keepLocal) {
        while (!worker.localQueue.try_enqueue(zs::move(task))) {
          TaskHandle displaced;
          if (worker.localQueue.try_dequeue(displaced)) {
            size_t globalAttempts = 0;
            while (!_globalQueue.try_enqueue(zs::move(displaced))) {
              if (globalAttempts < 64)
                pause_cpu();
              else
                ManagedThread::yield_current();
              ++globalAttempts;
            }
            wake_any_worker_();
          } else {
            ManagedThread::yield_current();
          }
        }
        _remainingJobs.fetch_add(1);
        wake_worker_(worker);
        if (_numWorkers > 1) wake_any_worker_();
        return;
      }
    }

    size_t enqueueAttempts = 0;
    while (!_globalQueue.try_enqueue(zs::move(task))) {
      if (enqueueAttempts < 64)
        pause_cpu();
      else
        ManagedThread::yield_current();
      ++enqueueAttempts;
    }
    _remainingJobs.fetch_add(1);
    wake_any_worker_();
  }

  inline void AsyncScheduler::process_(Worker &worker, TaskHandle &task) {
    switch (task.kind()) {
      case TaskHandle::normal_fn:
        if (task.as_fn()) task.as_fn()();
        _remainingJobs.fetch_sub(1);
        break;

      case TaskHandle::once_coro:
        if (task.as_coro()) task.as_coro().resume();
        _remainingJobs.fetch_sub(1);
        break;

      case TaskHandle::task_node: {
        auto *node = task.as_node();
        if (node && node->_task.getHandle()) {
          node->_state.store(CoroTaskNode::running);
          node->_task.getHandle().resume();

          if (node->_task.getHandle().done()) {
            node->_state.store(CoroTaskNode::done);
            node->for_each_successor([&](CoroTaskNode *successor) {
              if (successor->_numDeps.fetch_sub(1) == 1) {
                enqueue_(TaskHandle{successor});
              }
            });
          } else {
            enqueue_(TaskHandle{node}, worker.index);
            _remainingJobs.fetch_add(1);
          }
        }
        _remainingJobs.fetch_sub(1);
        break;
      }

      case TaskHandle::stop_signal:
      case TaskHandle::empty:
      default:
        break;
    }
  }

  inline void AsyncScheduler::worker_loop_(ManagedThread &self, i32 workerIndex) {
    auto &worker = _workers[workerIndex];
    struct WorkerScope {
      const AsyncScheduler *previousScheduler;
      i32 previousWorkerId;

      WorkerScope(const AsyncScheduler *scheduler, i32 workerId)
          : previousScheduler{AsyncScheduler::_currentScheduler},
            previousWorkerId{AsyncScheduler::_currentWorkerId} {
        AsyncScheduler::_currentScheduler = scheduler;
        AsyncScheduler::_currentWorkerId = workerId;
      }

      ~WorkerScope() {
        AsyncScheduler::_currentScheduler = previousScheduler;
        AsyncScheduler::_currentWorkerId = previousWorkerId;
      }
    } scope{this, workerIndex};

    while (!self.stop_requested()) {
      worker.status.store(1);

      while (_pauseState.load() != 0 && !self.stop_requested()) _pauseState.wait(1);
      if (self.stop_requested()) break;

      TaskHandle task;
      if (worker.localQueue.try_dequeue(task)) {
        process_(worker, task);
        continue;
      }

      if (_globalQueue.try_dequeue(task)) {
        process_(worker, task);
        continue;
      }

      if (try_steal_(worker, task)) {
        process_(worker, task);
        continue;
      }

      u32 expected = 1;
      if (!worker.status.compare_exchange_strong(expected, 0)) continue;
      worker.status.wait(0);
    }
  }

  inline bool AsyncScheduler::try_steal_(Worker &thief, TaskHandle &out) {
    if (_numWorkers <= 1) return false;

    const size_t start = _stealCounter.fetch_add(1) % _numWorkers;
    const size_t thiefIndex = static_cast<size_t>(thief.index);
    for (size_t offset = 0; offset < _numWorkers; ++offset) {
      const size_t victimIndex = (start + offset) % _numWorkers;
      if (victimIndex == thiefIndex) continue;

      auto &victim = _workers[victimIndex];
      if (victim.localQueue.try_dequeue(out)) return true;
    }
    return false;
  }

  inline bool AsyncScheduler::schedule(CoroTaskNode *node) {
    if (!node) return false;
    auto expected = CoroTaskNode::idle;
    if (!node->_state.compare_exchange_strong(expected, CoroTaskNode::planned)) return true;

    if (node->_numDeps.load() != 0) {
      bool okay = true;
      node->for_each_predecessor([&](CoroTaskNode *pred) { okay = schedule(pred) && okay; });
      if (!okay) return false;
    } else {
      enqueue(node);
    }
    return true;
  }

}  // namespace zs