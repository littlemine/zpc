#pragma once

#if defined(__has_include) && !__has_include(<coroutine>)
#  error "ZpcTaskGraph.hpp requires C++20 coroutine support."
#endif

#include "zensim/ZpcCoroutine.hpp"
#include "zensim/execution/AsyncScheduler.hpp"

namespace zs {

  struct TaskGraph {
    static constexpr size_t kMaxNodes = 256;

    TaskGraph() noexcept = default;
    ~TaskGraph() {
      const size_t count = _numNodes.load();
      for (size_t i = 0; i < count; ++i) {
        _nodes[i].~CoroTaskNode();
      }
    }

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
          scheduler.schedule(&_nodes[i]);
        }
      }
    }

    void wait(AsyncScheduler &scheduler) { scheduler.wait(); }

    bool allDone() const noexcept {
      const size_t count = _numNodes.load();
      for (size_t i = 0; i < count; ++i) {
        if (_nodes[i]._state.load() != CoroTaskNode::done) return false;
      }
      return true;
    }

    size_t numNodes() const noexcept { return _numNodes.load(); }

  private:
    alignas(alignof(CoroTaskNode)) byte _nodeStorage[sizeof(CoroTaskNode) * kMaxNodes]{};
    CoroTaskNode *_nodes = reinterpret_cast<CoroTaskNode *>(_nodeStorage);
    atomic_size_t _numNodes{0};
  };

}  // namespace zs