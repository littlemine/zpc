#pragma once

#include <vector>

#include "zensim/ZpcAsync.hpp"
#include "zensim/memory/Allocator.h"

namespace zs {
  namespace detail {
    class AsyncMemoryArena {
    public:
      AsyncMemoryArena() = default;

      static AsyncMemoryArena &instance() {
        static AsyncMemoryArena arena;
        return arena;
      }

      void *allocate(size_t bytes, size_t alignment) {
        _mutex.lock();
        void *ptr = zs::allocate(mem_host, bytes, alignment);
        _mutex.unlock();
        return ptr;
      }

      void deallocate(void *ptr, size_t bytes, size_t alignment) {
        _mutex.lock();
        zs::deallocate(mem_host, ptr, bytes, alignment);
        _mutex.unlock();
      }

    private:
      Mutex _mutex{};
    };

    template <typename T, size_t BlocksPerSlab = 256>
    class AsyncObjectPool {
    public:
      AsyncObjectPool() = default;

      ~AsyncObjectPool() {
        auto &cache = local_cache_();
        if (cache.owner == this) flush_local_cache_(cache);
        for (void *slab : _slabs)
          AsyncMemoryArena::instance().deallocate(slab, sizeof(Slot) * BlocksPerSlab,
                                                  alignof(Slot));
      }

      AsyncObjectPool(const AsyncObjectPool &) = delete;
      AsyncObjectPool &operator=(const AsyncObjectPool &) = delete;

      template <typename... Args>
      T *acquire(Args &&...args) {
        auto &cache = local_cache_();
        bind_local_cache_(cache);

        Slot *slot = local_pop_(cache);
        if (!slot) slot = refill_local_cache_(cache);
        if (!slot) {
          grow_();
          slot = refill_local_cache_(cache);
        }
        if (!slot) return nullptr;
        return ::new (static_cast<void *>(slot->storage)) T(zs::forward<Args>(args)...);
      }

      void release(T *ptr) noexcept {
        if (!ptr) return;
        ptr->~T();

        auto &cache = local_cache_();
        bind_local_cache_(cache);
        local_push_(cache, reinterpret_cast<Slot *>(ptr));

        if (cache.count >= kLocalCacheLimit) flush_local_cache_(cache, kLocalCacheLimit / 2);
      }

    private:
      union Slot {
        Slot *next;
        alignas(T) byte storage[sizeof(T)];
      };

      struct LocalCache {
        AsyncObjectPool *owner{nullptr};
        Slot *head{nullptr};
        Slot *tail{nullptr};
        size_t count{0};
      };

      static constexpr size_t kLocalCacheLimit = BlocksPerSlab < 32 ? BlocksPerSlab : 32;
      inline static thread_local LocalCache _localCache{};

      Slot *pop_() noexcept {
        Slot *head = _freeList.load();
        while (head) {
          Slot *next = head->next;
          if (_freeList.compare_exchange_weak(head, next)) return head;
        }
        return nullptr;
      }

      void push_(Slot *slot) noexcept {
        Slot *head = _freeList.load();
        do {
          slot->next = head;
        } while (!_freeList.compare_exchange_weak(head, slot));
      }

      static LocalCache &local_cache_() noexcept { return _localCache; }

      void bind_local_cache_(LocalCache &cache) noexcept {
        if (cache.owner == this) return;
        if (cache.owner && cache.head) cache.owner->flush_local_cache_(cache);
        cache.owner = this;
        cache.head = nullptr;
        cache.tail = nullptr;
        cache.count = 0;
      }

      Slot *local_pop_(LocalCache &cache) noexcept {
        Slot *slot = cache.head;
        if (!slot) return nullptr;
        cache.head = slot->next;
        if (!cache.head) cache.tail = nullptr;
        slot->next = nullptr;
        --cache.count;
        return slot;
      }

      void local_push_(LocalCache &cache, Slot *slot) noexcept {
        slot->next = cache.head;
        cache.head = slot;
        if (!cache.tail) cache.tail = slot;
        ++cache.count;
      }

      Slot *refill_local_cache_(LocalCache &cache) noexcept {
        while (cache.count < kLocalCacheLimit) {
          Slot *slot = pop_();
          if (!slot) break;
          local_push_(cache, slot);
        }
        return local_pop_(cache);
      }

      void flush_local_cache_(LocalCache &cache, size_t retain = 0) noexcept {
        while (cache.count > retain) {
          Slot *slot = local_pop_(cache);
          if (!slot) break;
          push_(slot);
        }
      }

      void grow_() {
        _slabMutex.lock();
        if (_freeList.load()) {
          _slabMutex.unlock();
          return;
        }

        void *slab = AsyncMemoryArena::instance().allocate(sizeof(Slot) * BlocksPerSlab, alignof(Slot));
        _slabs.push_back(slab);
        auto *slots = reinterpret_cast<Slot *>(slab);
        for (size_t i = 0; i < BlocksPerSlab; ++i) push_(&slots[i]);
        _slabMutex.unlock();
      }

      Atomic<Slot *> _freeList{nullptr};
      Mutex _slabMutex{};
      std::vector<void *> _slabs{};
    };

    template <typename T, size_t BlocksPerSlab = 256>
    AsyncObjectPool<T, BlocksPerSlab> &async_pool() {
      static AsyncObjectPool<T, BlocksPerSlab> pool;
      return pool;
    }
  }  // namespace detail
}  // namespace zs