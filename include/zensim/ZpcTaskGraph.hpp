#pragma once

#if defined(__has_include) && !__has_include(<coroutine>)
#  error "ZpcTaskGraph.hpp requires C++20 coroutine support."
#endif

#include "zensim/ZpcCoroutine.hpp"
#include "zensim/execution/AsyncScheduler.hpp"

namespace zs {

<<<<<<< HEAD
=======
  /// =========================================================================
  /// TaskGraph — DAG-based task scheduling using CoroTaskNode
  /// =========================================================================
  /// Nodes hold coroutine tasks (Future<void>). Edges express dependencies.
  /// submit() walks the DAG, schedules root nodes, and lets the scheduler
  /// propagate successor activation.
  ///
  /// Usage:
  ///   TaskGraph graph;
  ///   auto* a = graph.addNode(my_coroutine_a(), "A");
  ///   auto* b = graph.addNode(my_coroutine_b(), "B");
  ///   auto* c = graph.addNode(my_coroutine_c(), "C");
  ///   graph.addEdge(a, c);  // C depends on A
  ///   graph.addEdge(b, c);  // C depends on B
  ///   graph.submit(scheduler);
  ///   graph.wait(scheduler);

>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
  struct TaskGraph {
    static constexpr size_t kMaxNodes = 256;

    TaskGraph() noexcept = default;
    ~TaskGraph() {
<<<<<<< HEAD
      const size_t count = _numNodes.load();
      for (size_t i = 0; i < count; ++i) {
=======
      for (size_t i = 0; i < _numNodes; ++i) {
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
        _nodes[i].~CoroTaskNode();
      }
    }

<<<<<<< HEAD
    TaskGraph(const TaskGraph &) = delete;
    TaskGraph &operator=(const TaskGraph &) = delete;

    CoroTaskNode *addNode(Future<void> &&task, const char *tag = "") {
      size_t index = _numNodes.load();
      do {
        if (index >= kMaxNodes) return nullptr;
      } while (!_numNodes.compare_exchange_weak(index, index + 1));

      auto *node = &_nodes[index];
      ::new (static_cast<void *>(node)) CoroTaskNode{};
      node->_task = zs::move(task);
      node->_tag = tag;
      node->_state.store(CoroTaskNode::idle);
      return node;
    }

    CoroTaskNode *addNode(function<void()> fn, const char *tag = "") {
      auto coroutine = [](function<void()> call) -> Future<void> {
        if (call) call();
        co_return;
      };
      return addNode(coroutine(zs::move(fn)), tag);
    }

    void addEdge(CoroTaskNode *from, CoroTaskNode *to) {
      if (from && to) from->to(*to);
    }

    void submit(AsyncScheduler &scheduler) {
      const size_t count = _numNodes.load();
      for (size_t i = 0; i < count; ++i) _nodes[i]._state.store(CoroTaskNode::idle);
      for (size_t i = 0; i < count; ++i) {
        if (_nodes[i]._numDeps.load() == 0) {
=======
    TaskGraph(const TaskGraph&) = delete;
    TaskGraph& operator=(const TaskGraph&) = delete;

    /// Add a coroutine task as a node.
    CoroTaskNode* addNode(Future<void>&& task, const char* tag = "") {
      if (_numNodes >= kMaxNodes) return nullptr;
      auto* node = &_nodes[_numNodes++];
      ::new (static_cast<void*>(node)) CoroTaskNode{};
      node->_task = zs::move(task);
      node->_tag = tag;
      node->_state = CoroTaskNode::idle;
      return node;
    }

    /// Add a plain function as a node (wrapped in a trivial coroutine).
    CoroTaskNode* addNode(function<void()> fn, const char* tag = "") {
      // Wrap in a one-shot coroutine
      auto coro = [](function<void()> f) -> Future<void> {
        if (f) f();
        co_return;
      };
      return addNode(coro(zs::move(fn)), tag);
    }

    /// Add dependency edge: `from` must complete before `to` starts.
    void addEdge(CoroTaskNode* from, CoroTaskNode* to) {
      if (from && to) from->to(*to);
    }

    /// Submit the entire graph to a scheduler. Schedules all root nodes
    /// (nodes with zero dependencies).
    void submit(AsyncScheduler& scheduler) {
      // Reset all node states
      for (size_t i = 0; i < _numNodes; ++i) _nodes[i]._state = CoroTaskNode::idle;

      // Find and schedule root nodes (no predecessors)
      for (size_t i = 0; i < _numNodes; ++i) {
        if (_nodes[i]._numDeps.load(std::memory_order_relaxed) == 0) {
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
          scheduler.schedule(&_nodes[i]);
        }
      }
    }

<<<<<<< HEAD
    void wait(AsyncScheduler &scheduler) { scheduler.wait(); }

    bool allDone() const noexcept {
      const size_t count = _numNodes.load();
      for (size_t i = 0; i < count; ++i) {
        if (_nodes[i]._state.load() != CoroTaskNode::done) return false;
=======
    /// Block until all nodes are done.
    void wait(AsyncScheduler& scheduler) {
      scheduler.wait();
    }

    /// Check if all nodes are done.
    bool allDone() const noexcept {
      for (size_t i = 0; i < _numNodes; ++i) {
        if (_nodes[i]._state != CoroTaskNode::done) return false;
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
      }
      return true;
    }

<<<<<<< HEAD
    size_t numNodes() const noexcept { return _numNodes.load(); }

  private:
    alignas(alignof(CoroTaskNode)) byte _nodeStorage[sizeof(CoroTaskNode) * kMaxNodes]{};
    CoroTaskNode *_nodes = reinterpret_cast<CoroTaskNode *>(_nodeStorage);
    atomic_size_t _numNodes{0};
  };

}  // namespace zs
=======
    size_t numNodes() const noexcept { return _numNodes; }

  private:
    alignas(alignof(CoroTaskNode)) byte _nodeStorage[sizeof(CoroTaskNode) * kMaxNodes]{};
    CoroTaskNode* _nodes = reinterpret_cast<CoroTaskNode*>(_nodeStorage);
    size_t _numNodes{0};
  };

}  // namespace zs
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
