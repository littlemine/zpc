#pragma once

#include "zensim/execution/Atomics.hpp"
#include "zensim/ZpcMeta.hpp"
#include "zensim/zpc_tpls/moodycamel/concurrent_queue/concurrentqueue.h"

namespace zs {

  namespace detail {
    constexpr size_t cache_line_size = 64;
    constexpr size_t min_chunk_capacity = 64;
    constexpr size_t max_chunk_capacity = 1024;

    constexpr size_t next_power_of_two(size_t value) {
      if (value <= 1) return 1;
      --value;
      value |= value >> 1;
      value |= value >> 2;
      value |= value >> 4;
      value |= value >> 8;
      value |= value >> 16;
      if constexpr (sizeof(size_t) > 4) value |= value >> 32;
      return value + 1;
    }

    constexpr size_t log2_power_of_two(size_t value) {
      size_t shift = 0;
      while (value > 1) {
        value >>= 1;
        ++shift;
      }
      return shift;
    }

    constexpr size_t balanced_chunk_capacity(size_t capacity, size_t requestedChunkCapacity = 0) {
      if (capacity == 0) return 1;

      if (requestedChunkCapacity != 0) {
        const auto requested = next_power_of_two(requestedChunkCapacity);
        return requested > capacity ? capacity : requested;
      }

      size_t chunkCapacity = 1;
      while (chunkCapacity < capacity / chunkCapacity) chunkCapacity <<= 1;

      if (chunkCapacity < min_chunk_capacity)
        chunkCapacity = capacity < min_chunk_capacity ? capacity : min_chunk_capacity;
      if (chunkCapacity > max_chunk_capacity)
        chunkCapacity = max_chunk_capacity;
      if (chunkCapacity > capacity)
        chunkCapacity = capacity;
      return next_power_of_two(chunkCapacity);
    }
  }  // namespace detail

  template <typename T, size_t RequestedCapacity = 1024>
  struct ConcurrentQueue {
    static constexpr size_t Capacity = detail::next_power_of_two(RequestedCapacity);
    static constexpr size_t DefaultCapacity = Capacity;

    explicit ConcurrentQueue(size_t capacity = DefaultCapacity)
        : _queue{detail::next_power_of_two(capacity != 0 ? capacity : DefaultCapacity)} {}

    ~ConcurrentQueue() = default;

    ConcurrentQueue(const ConcurrentQueue &) = delete;
    ConcurrentQueue &operator=(const ConcurrentQueue &) = delete;

    template <typename U>
    bool try_enqueue(U &&item) {
      return _queue.try_enqueue(zs::forward<U>(item));
    }

    bool try_dequeue(T &item) {
      return _queue.try_dequeue(item);
    }

    size_t size_approx() const noexcept { return _queue.size_approx(); }

    bool empty_approx() const noexcept { return size_approx() == 0; }

  private:
    moodycamel::ConcurrentQueue<T> _queue;
  };

  template <typename T, size_t RequestedCapacity = 1024>
  struct SpscQueue {
    static constexpr size_t DefaultCapacity = detail::next_power_of_two(RequestedCapacity);

    explicit SpscQueue(size_t capacity = DefaultCapacity, size_t chunkCapacity = 0)
        : _capacity{detail::next_power_of_two(capacity != 0 ? capacity : DefaultCapacity)},
          _mask{_capacity - 1},
          _chunkCapacity{detail::balanced_chunk_capacity(_capacity, chunkCapacity)},
          _chunkMask{_chunkCapacity - 1},
          _chunkShift{detail::log2_power_of_two(_chunkCapacity)},
          _chunkCount{_capacity / _chunkCapacity},
          _chunks{new Cell *[_chunkCount]} {
      for (size_t chunkIndex = 0; chunkIndex < _chunkCount; ++chunkIndex)
        _chunks[chunkIndex] = new Cell[_chunkCapacity];
    }

    ~SpscQueue() {
      T item{};
      while (try_dequeue(item)) {}
      for (size_t chunkIndex = 0; chunkIndex < _chunkCount; ++chunkIndex)
        delete[] _chunks[chunkIndex];
      delete[] _chunks;
    }

    SpscQueue(const SpscQueue &) = delete;
    SpscQueue &operator=(const SpscQueue &) = delete;

    template <typename U>
    bool try_enqueue(U &&item) {
      const size_t head = _head.load(memory_order_relaxed);
      const size_t tail = _tail.load(memory_order_acquire);
      if (head - tail >= _capacity) return false;

      auto &cell = cell_at_(head);
      ::new (static_cast<void *>(&cell.storage)) T(zs::forward<U>(item));
      _head.store(head + 1, memory_order_release);
      return true;
    }

    bool try_dequeue(T &item) {
      const size_t tail = _tail.load(memory_order_relaxed);
      const size_t head = _head.load(memory_order_acquire);
      if (tail == head) return false;

      auto &cell = cell_at_(tail);
      item = zs::move(*reinterpret_cast<T *>(&cell.storage));
      reinterpret_cast<T *>(&cell.storage)->~T();
      _tail.store(tail + 1, memory_order_release);
      return true;
    }

    size_t size_approx() const noexcept {
      const auto head = _head.load(memory_order_acquire);
      const auto tail = _tail.load(memory_order_acquire);
      return head - tail;
    }

    size_t capacity() const noexcept { return _capacity; }

  private:
    struct Cell {
      alignas(alignof(T)) byte storage[sizeof(T)];
    };

    Cell &cell_at_(size_t position) const noexcept {
      const size_t index = position & _mask;
      return _chunks[index >> _chunkShift][index & _chunkMask];
    }

    const size_t _capacity;
    const size_t _mask;
    const size_t _chunkCapacity;
    const size_t _chunkMask;
    const size_t _chunkShift;
    const size_t _chunkCount;
    Cell **_chunks;
    alignas(detail::cache_line_size) Atomic<size_t> _head{0};
    alignas(detail::cache_line_size) Atomic<size_t> _tail{0};
  };

}  // namespace zs