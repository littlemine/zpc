#pragma once
#include <atomic>

#include "zensim/ZpcMeta.hpp"

namespace zs {

  /// =========================================================================
  /// ConcurrentQueue<T, Capacity> — bounded lock-free MPMC ring buffer
  /// =========================================================================
  /// Based on Dmitry Vyukov's bounded MPMC queue.
  /// Cache-line padded to avoid false sharing.
  /// Capacity is rounded up to the next power of two.
  ///
  /// For unbounded use-cases, chain multiple bounded queues or use a
  /// linked-list approach with pool allocation.

  namespace detail {
    constexpr size_t cache_line_size = 64;

    constexpr size_t next_power_of_two(size_t v) {
      --v;
      v |= v >> 1;
      v |= v >> 2;
      v |= v >> 4;
      v |= v >> 8;
      v |= v >> 16;
      if constexpr (sizeof(size_t) > 4) v |= v >> 32;
      return v + 1;
    }
  }  // namespace detail

  template <typename T, size_t RequestedCapacity = 1024> struct ConcurrentQueue {
    static_assert(RequestedCapacity > 0, "Capacity must be > 0");
    static constexpr size_t Capacity = detail::next_power_of_two(RequestedCapacity);
    static constexpr size_t Mask = Capacity - 1;

    ConcurrentQueue() noexcept {
      for (size_t i = 0; i < Capacity; ++i)
        _cells[i].sequence.store(i, std::memory_order_relaxed);
      _enqueuePos.store(0, std::memory_order_relaxed);
      _dequeuePos.store(0, std::memory_order_relaxed);
    }

    ~ConcurrentQueue() {
      // drain remaining items
      T tmp;
      while (try_dequeue(tmp)) {}
    }

    ConcurrentQueue(const ConcurrentQueue&) = delete;
    ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;

    /// Try to enqueue. Returns false if full.
    template <typename U> bool try_enqueue(U&& item) {
      Cell* cell;
      size_t pos = _enqueuePos.load(std::memory_order_relaxed);
      for (;;) {
        cell = &_cells[pos & Mask];
        size_t seq = cell->sequence.load(std::memory_order_acquire);
        auto diff = static_cast<sint_t>(seq) - static_cast<sint_t>(pos);
        if (diff == 0) {
          if (_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
            break;
        } else if (diff < 0) {
          return false;  // full
        } else {
          pos = _enqueuePos.load(std::memory_order_relaxed);
        }
      }
      ::new (static_cast<void*>(&cell->storage)) T(zs::forward<U>(item));
      cell->sequence.store(pos + 1, std::memory_order_release);
      return true;
    }

    /// Try to dequeue. Returns false if empty.
    bool try_dequeue(T& item) {
      Cell* cell;
      size_t pos = _dequeuePos.load(std::memory_order_relaxed);
      for (;;) {
        cell = &_cells[pos & Mask];
        size_t seq = cell->sequence.load(std::memory_order_acquire);
        auto diff = static_cast<sint_t>(seq) - static_cast<sint_t>(pos + 1);
        if (diff == 0) {
          if (_dequeuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
            break;
        } else if (diff < 0) {
          return false;  // empty
        } else {
          pos = _dequeuePos.load(std::memory_order_relaxed);
        }
      }
      item = zs::move(*reinterpret_cast<T*>(&cell->storage));
      reinterpret_cast<T*>(&cell->storage)->~T();
      cell->sequence.store(pos + Mask + 1, std::memory_order_release);
      return true;
    }

    size_t size_approx() const noexcept {
      auto e = _enqueuePos.load(std::memory_order_relaxed);
      auto d = _dequeuePos.load(std::memory_order_relaxed);
      return e >= d ? e - d : 0;
    }

    bool empty_approx() const noexcept { return size_approx() == 0; }

  private:
    struct Cell {
      std::atomic<size_t> sequence;
      alignas(alignof(T)) byte storage[sizeof(T)];
    };

    // Pad to avoid false sharing between producer and consumer positions
    alignas(detail::cache_line_size) std::atomic<size_t> _enqueuePos;
    alignas(detail::cache_line_size) std::atomic<size_t> _dequeuePos;
    Cell _cells[Capacity];
  };

  /// =========================================================================
  /// SpscQueue<T, Capacity> — single-producer single-consumer bounded queue
  /// =========================================================================
  /// Simpler and faster than MPMC when only one thread produces and one consumes.

  template <typename T, size_t RequestedCapacity = 1024> struct SpscQueue {
    static constexpr size_t Capacity = detail::next_power_of_two(RequestedCapacity);
    static constexpr size_t Mask = Capacity - 1;

    SpscQueue() noexcept = default;
    ~SpscQueue() {
      T tmp;
      while (try_dequeue(tmp)) {}
    }

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    template <typename U> bool try_enqueue(U&& item) {
      size_t head = _head.load(std::memory_order_relaxed);
      size_t next = (head + 1) & Mask;
      if (next == _tail.load(std::memory_order_acquire)) return false;  // full
      ::new (static_cast<void*>(&_cells[head].storage)) T(zs::forward<U>(item));
      _head.store(next, std::memory_order_release);
      return true;
    }

    bool try_dequeue(T& item) {
      size_t tail = _tail.load(std::memory_order_relaxed);
      if (tail == _head.load(std::memory_order_acquire)) return false;  // empty
      item = zs::move(*reinterpret_cast<T*>(&_cells[tail].storage));
      reinterpret_cast<T*>(&_cells[tail].storage)->~T();
      _tail.store((tail + 1) & Mask, std::memory_order_release);
      return true;
    }

    size_t size_approx() const noexcept {
      auto h = _head.load(std::memory_order_relaxed);
      auto t = _tail.load(std::memory_order_relaxed);
      return (h - t) & Mask;
    }

  private:
    struct Cell {
      alignas(alignof(T)) byte storage[sizeof(T)];
    };

    alignas(detail::cache_line_size) std::atomic<size_t> _head{0};
    alignas(detail::cache_line_size) std::atomic<size_t> _tail{0};
    Cell _cells[Capacity];
  };

}  // namespace zs
