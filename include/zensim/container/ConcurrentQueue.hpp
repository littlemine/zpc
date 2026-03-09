#pragma once
<<<<<<< HEAD

#include "zensim/execution/Atomics.hpp"
=======
#include <atomic>

>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
#include "zensim/ZpcMeta.hpp"

namespace zs {

<<<<<<< HEAD
  namespace detail {
    constexpr size_t cache_line_size = 64;

    constexpr size_t next_power_of_two(size_t value) {
      --value;
      value |= value >> 1;
      value |= value >> 2;
      value |= value >> 4;
      value |= value >> 8;
      value |= value >> 16;
      if constexpr (sizeof(size_t) > 4) value |= value >> 32;
      return value + 1;
    }
  }  // namespace detail

  template <typename T, size_t RequestedCapacity = 1024>
  struct ConcurrentQueue {
    static_assert(RequestedCapacity > 0, "Capacity must be > 0");

=======
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
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    static constexpr size_t Capacity = detail::next_power_of_two(RequestedCapacity);
    static constexpr size_t Mask = Capacity - 1;

    ConcurrentQueue() noexcept {
      for (size_t i = 0; i < Capacity; ++i)
<<<<<<< HEAD
        atomic_store(omp_c, &_cells[i].sequence, i);
      atomic_store(omp_c, &_enqueuePos, size_t{0});
      atomic_store(omp_c, &_dequeuePos, size_t{0});
    }

    ~ConcurrentQueue() {
      T item{};
      while (try_dequeue(item)) {}
    }

    ConcurrentQueue(const ConcurrentQueue &) = delete;
    ConcurrentQueue &operator=(const ConcurrentQueue &) = delete;

    template <typename U>
    bool try_enqueue(U &&item) {
      Cell *cell = nullptr;
      size_t position = atomic_load(omp_c, &_enqueuePos);
      for (;;) {
        cell = &_cells[position & Mask];
        size_t sequence = atomic_load(omp_c, &cell->sequence);
        auto diff = static_cast<sint_t>(sequence) - static_cast<sint_t>(position);
        if (diff == 0) {
          if (atomic_cas(omp_c, &_enqueuePos, position, position + 1) == position)
            break;
        } else if (diff < 0) {
          return false;
        } else {
          position = atomic_load(omp_c, &_enqueuePos);
        }
      }

      ::new (static_cast<void *>(&cell->storage)) T(zs::forward<U>(item));
      atomic_store(omp_c, &cell->sequence, position + 1);
      return true;
    }

    bool try_dequeue(T &item) {
      Cell *cell = nullptr;
      size_t position = atomic_load(omp_c, &_dequeuePos);
      for (;;) {
        cell = &_cells[position & Mask];
        size_t sequence = atomic_load(omp_c, &cell->sequence);
        auto diff = static_cast<sint_t>(sequence) - static_cast<sint_t>(position + 1);
        if (diff == 0) {
          if (atomic_cas(omp_c, &_dequeuePos, position, position + 1) == position)
            break;
        } else if (diff < 0) {
          return false;
        } else {
          position = atomic_load(omp_c, &_dequeuePos);
        }
      }

      item = zs::move(*reinterpret_cast<T *>(&cell->storage));
      reinterpret_cast<T *>(&cell->storage)->~T();
      atomic_store(omp_c, &cell->sequence, position + Mask + 1);
=======
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
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
      return true;
    }

    size_t size_approx() const noexcept {
<<<<<<< HEAD
      const auto enqueue = atomic_load(omp_c, &_enqueuePos);
      const auto dequeue = atomic_load(omp_c, &_dequeuePos);
      return enqueue >= dequeue ? enqueue - dequeue : 0;
=======
      auto e = _enqueuePos.load(std::memory_order_relaxed);
      auto d = _dequeuePos.load(std::memory_order_relaxed);
      return e >= d ? e - d : 0;
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    }

    bool empty_approx() const noexcept { return size_approx() == 0; }

  private:
    struct Cell {
<<<<<<< HEAD
      size_t sequence;
      alignas(alignof(T)) byte storage[sizeof(T)];
    };

    alignas(detail::cache_line_size) size_t _enqueuePos;
    alignas(detail::cache_line_size) size_t _dequeuePos;
    Cell _cells[Capacity];
  };

  template <typename T, size_t RequestedCapacity = 1024>
  struct SpscQueue {
=======
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
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    static constexpr size_t Capacity = detail::next_power_of_two(RequestedCapacity);
    static constexpr size_t Mask = Capacity - 1;

    SpscQueue() noexcept = default;
<<<<<<< HEAD

    ~SpscQueue() {
      T item{};
      while (try_dequeue(item)) {}
    }

    SpscQueue(const SpscQueue &) = delete;
    SpscQueue &operator=(const SpscQueue &) = delete;

    template <typename U>
    bool try_enqueue(U &&item) {
      const size_t head = atomic_load(omp_c, &_head);
      const size_t next = (head + 1) & Mask;
      if (next == atomic_load(omp_c, &_tail)) return false;
      ::new (static_cast<void *>(&_cells[head].storage)) T(zs::forward<U>(item));
      atomic_store(omp_c, &_head, next);
      return true;
    }

    bool try_dequeue(T &item) {
      const size_t tail = atomic_load(omp_c, &_tail);
      if (tail == atomic_load(omp_c, &_head)) return false;
      item = zs::move(*reinterpret_cast<T *>(&_cells[tail].storage));
      reinterpret_cast<T *>(&_cells[tail].storage)->~T();
      atomic_store(omp_c, &_tail, (tail + 1) & Mask);
=======
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
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
      return true;
    }

    size_t size_approx() const noexcept {
<<<<<<< HEAD
      const auto head = atomic_load(omp_c, &_head);
      const auto tail = atomic_load(omp_c, &_tail);
      return (head - tail) & Mask;
=======
      auto h = _head.load(std::memory_order_relaxed);
      auto t = _tail.load(std::memory_order_relaxed);
      return (h - t) & Mask;
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
    }

  private:
    struct Cell {
      alignas(alignof(T)) byte storage[sizeof(T)];
    };

<<<<<<< HEAD
    alignas(detail::cache_line_size) size_t _head{0};
    alignas(detail::cache_line_size) size_t _tail{0};
    Cell _cells[Capacity];
  };

}  // namespace zs
=======
    alignas(detail::cache_line_size) std::atomic<size_t> _head{0};
    alignas(detail::cache_line_size) std::atomic<size_t> _tail{0};
    Cell _cells[Capacity];
  };

}  // namespace zs
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
