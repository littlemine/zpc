#pragma once

#include "zensim/execution/Atomics.hpp"
#include "zensim/ZpcMeta.hpp"

namespace zs {

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

    static constexpr size_t Capacity = detail::next_power_of_two(RequestedCapacity);
    static constexpr size_t Mask = Capacity - 1;

    ConcurrentQueue() noexcept {
      for (size_t i = 0; i < Capacity; ++i)
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
      return true;
    }

    size_t size_approx() const noexcept {
      const auto enqueue = atomic_load(omp_c, &_enqueuePos);
      const auto dequeue = atomic_load(omp_c, &_dequeuePos);
      return enqueue >= dequeue ? enqueue - dequeue : 0;
    }

    bool empty_approx() const noexcept { return size_approx() == 0; }

  private:
    struct Cell {
      size_t sequence;
      alignas(alignof(T)) byte storage[sizeof(T)];
    };

    alignas(detail::cache_line_size) size_t _enqueuePos;
    alignas(detail::cache_line_size) size_t _dequeuePos;
    Cell _cells[Capacity];
  };

  template <typename T, size_t RequestedCapacity = 1024>
  struct SpscQueue {
    static constexpr size_t Capacity = detail::next_power_of_two(RequestedCapacity);
    static constexpr size_t Mask = Capacity - 1;

    SpscQueue() noexcept = default;

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
      return true;
    }

    size_t size_approx() const noexcept {
      const auto head = atomic_load(omp_c, &_head);
      const auto tail = atomic_load(omp_c, &_tail);
      return (head - tail) & Mask;
    }

  private:
    struct Cell {
      alignas(alignof(T)) byte storage[sizeof(T)];
    };

    alignas(detail::cache_line_size) size_t _head{0};
    alignas(detail::cache_line_size) size_t _tail{0};
    Cell _cells[Capacity];
  };

}  // namespace zs