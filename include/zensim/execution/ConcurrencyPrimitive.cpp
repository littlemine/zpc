#include "ConcurrencyPrimitive.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <limits>
#include <mutex>
#include "zensim/execution/Atomics.hpp"
#include "zensim/execution/Intrinsics.hpp"

#if defined(_WIN32)
#  include <windows.h>
// #  include <synchapi.h>
#elif defined(__linux__)
// #  include <immintrin.h>
#  include <linux/futex.h>
#  include <sys/syscall.h>
#  include <unistd.h>
#elif defined(__APPLE__)
#  include <sys/syscall.h>
#  include <unistd.h>
#endif

#define ZS_USE_NATIVE_LINUX_FUTEX 0

namespace zs {

  namespace detail {
    constexpr u64 twang_mix64(u64 key) noexcept {
      key = (~key) + (key << 21);
      key = key ^ (key >> 24);
      key = key + (key << 3) + (key << 8);
      key = key ^ (key >> 14);
      key = key + (key << 2) + (key << 4);
      key = key ^ (key >> 28);
      key = key + (key << 31);
      return key;
    }

    constexpr u64 twang_unmix64(u64 key) noexcept {
      key *= 4611686016279904257U;
      key ^= (key >> 28) ^ (key >> 56);
      key *= 14933078535860113213U;
      key ^= (key >> 14) ^ (key >> 28) ^ (key >> 42) ^ (key >> 56);
      key *= 15244667743933553977U;
      key ^= (key >> 24) ^ (key >> 48);
      key = (key + 1) * 9223367638806167551U;
      return key;
    }

    struct WaitNodeBase {
      const u64 _key;
      const u64 _lotid;
      std::mutex _mtx;
      std::condition_variable _cv;
      bool _signaled;

      WaitNodeBase(u64 key, u64 lotid) : _key{key}, _lotid{lotid}, _signaled{false} {}

      std::cv_status waitFor(i64 waitMs = -1) noexcept {
        using namespace std::chrono_literals;
        std::cv_status status = std::cv_status::no_timeout;
        std::unique_lock<std::mutex> lk(_mtx);
        while (!_signaled && status != std::cv_status::timeout) {
          if (waitMs > -1)
            status = _cv.wait_for(lk, waitMs * 1ms);
          else
            _cv.wait(lk);
        }
        return status;
      }

      void wake() noexcept {
        std::lock_guard<std::mutex> lk(_mtx);
        _signaled = true;
        _cv.notify_one();
      }

      bool signaled() const noexcept { return _signaled; }
    };

    struct WaitNode final : WaitNodeBase {
      WaitNode *_next;
      const u32 _data;

      WaitNode(u64 key, u64 lotid, u32 data) noexcept
          : WaitNodeBase{key, lotid}, _next{nullptr}, _data{data} {}
    };

    struct WaitQueue {
      std::mutex _mtx{};
      WaitNode *_list{nullptr};
      std::atomic<u64> _count{0};

      static WaitQueue *get_queue(u64 key) {
        static constexpr size_t num_queues = 4096;
        static WaitQueue queues[num_queues];
        return &queues[key & (num_queues - 1)];
      }

      WaitQueue() = default;
      ~WaitQueue() {
        WaitNode *node = _list;
        while (node != nullptr) {
          auto *current = node;
          node = node->_next;
          delete current;
        }
      }

      [[nodiscard]] WaitNode *insertHead(const WaitNode &newNode_) {
        auto *newNode = new WaitNode{newNode_._key, newNode_._lotid, newNode_._data};
        newNode->_next = _list;
        _list = newNode;
        return _list;
      }

      void erase(WaitNode *node) {
        if (_list == node) {
          auto *next = _list->_next;
          delete _list;
          _list = next;
          return;
        }
        WaitNode *current = _list ? _list->_next : nullptr;
        WaitNode *previous = _list;
        while (current != nullptr) {
          if (node == current) {
            previous->_next = current->_next;
            delete current;
            return;
          }
          previous = current;
          current = current->_next;
        }
      }
    };

    static std::atomic<u32> g_idCache = 0;

    template <typename Data>
    struct ParkingLot {
      static_assert(std::is_trivially_destructible_v<Data>,
                    "Data type here should be both trivially and nothrow destructible!");

      const u32 _lotid;
      ParkingLot() noexcept : _lotid{g_idCache++} {}
      ParkingLot(const ParkingLot &) = delete;

      template <typename Key, typename D, typename ParkCondition, typename PreWait>
      ParkResult parkFor(const Key bits, D &&data, ParkCondition &&parkCondition,
                         PreWait &&preWait, i64 timeoutMs) noexcept {
        u64 key = twang_mix64((u64)bits);
        WaitQueue *queue = WaitQueue::get_queue(key);
        WaitNode node{key, _lotid, (u32)FWD(data)};
        WaitNode *pnode = nullptr;
        {
          queue->_count.fetch_add(1, std::memory_order_seq_cst);
          std::unique_lock queueLock{queue->_mtx};
          if (!FWD(parkCondition)()) {
            queueLock.unlock();
            queue->_count.fetch_sub(1, std::memory_order_relaxed);
            return ParkResult::Skip;
          }

          pnode = queue->insertHead(node);
        }
        FWD(preWait)();

        const auto status = pnode->waitFor(timeoutMs);
        if (status == std::cv_status::timeout) {
          std::lock_guard queueLock{queue->_mtx};
          if (!pnode->signaled()) {
            queue->erase(pnode);
            queue->_count.fetch_sub(1, std::memory_order_relaxed);
            return ParkResult::Timeout;
          }
        }
        return ParkResult::Unpark;
      }

      template <typename Key, typename Unparker>
      void unpark(const Key bits, Unparker &&func) {
        u64 key = twang_mix64((u64)bits);
        WaitQueue *queue = WaitQueue::get_queue(key);
        if (queue->_count.load(std::memory_order_seq_cst) == 0) return;

        std::lock_guard queueLock(queue->_mtx);
        for (WaitNode *node = queue->_list; node != nullptr;) {
          WaitNode *next = node->_next;
          if (node->_key == key && node->_lotid == _lotid) {
            const auto result = FWD(func)(node->_data);
            if (result == UnparkControl::RemoveBreak || result == UnparkControl::RemoveContinue) {
              node->wake();
              queue->erase(node);
              queue->_count.fetch_sub(1, std::memory_order_relaxed);
            }
            if (result == UnparkControl::RemoveBreak || result == UnparkControl::RetainBreak)
              return;
          }
          node = next;
        }
      }
    };
  }  // namespace detail

  static detail::ParkingLot<u32> g_lot;

#if defined(_WIN32)
  // Native Windows futex via WaitOnAddress / WakeByAddress (Windows 8+)
  static FutexResult native_futex_wait_for(void *addr, u32 expected, i64 duration) {
    BOOL ok;
    if (duration < 0) {
      ok = WaitOnAddress(addr, &expected, sizeof(u32), INFINITE);
    } else {
      ok = WaitOnAddress(addr, &expected, sizeof(u32), static_cast<DWORD>(duration));
    }
    if (ok) return FutexResult::awoken;
    DWORD err = GetLastError();
    if (err == ERROR_TIMEOUT) return FutexResult::timedout;
    return FutexResult::value_changed;
  }

  static int native_futex_wake(void *addr, int count) {
    if (count <= 1)
      WakeByAddressSingle(addr);
    else
      WakeByAddressAll(addr);
    return count;
  }
#endif

  static int emulated_futex_wake(void *addr, int count = detail::deduce_numeric_max<int>(),
                                 u32 wakeMask = 0xffffffff) {
    int woken = 0;
    g_lot.unpark(addr, [&count, &woken, wakeMask](u32 const &mask) {
      if ((mask & wakeMask) == 0) return detail::UnparkControl::RetainContinue;
      count--;
      woken++;
      return count > 0 ? detail::UnparkControl::RemoveContinue
                       : detail::UnparkControl::RemoveBreak;
    });
    return woken;
  }

  static FutexResult emulated_futex_wait_for(std::atomic<u32> *addr, u32 expected,
                                             i64 duration = -1, u32 waitMask = 0xffffffff) {
    const auto res = g_lot.parkFor(
        addr, waitMask, [&]() -> bool { return addr->load(std::memory_order_seq_cst) == expected; },
        []() {}, duration);
    switch (res) {
      case detail::ParkResult::Skip:
        return FutexResult::value_changed;
      case detail::ParkResult::Unpark:
        return FutexResult::awoken;
      case detail::ParkResult::Timeout:
        return FutexResult::timedout;
    }
    return FutexResult::interrupted;
  }

  static FutexResult emulated_futex_wait_for(u32 *addr, u32 expected, i64 duration = -1,
                                             u32 waitMask = 0xffffffff) {
    const auto res = g_lot.parkFor(addr, waitMask,
                                   [&]() -> bool { return atomic_load(omp_c, addr) == expected; },
                                   []() {}, duration);
    switch (res) {
      case detail::ParkResult::Skip:
        return FutexResult::value_changed;
      case detail::ParkResult::Unpark:
        return FutexResult::awoken;
      case detail::ParkResult::Timeout:
        return FutexResult::timedout;
    }
    return FutexResult::interrupted;
  }

  void await_change(std::atomic<u32> &v, u32 cur) {
    while (true) {
      for (int i = 0; i != 1024; ++i) {
        if (v.load(std::memory_order_relaxed) != cur) return;
        pause_cpu();
      }
      Futex::wait(&v, cur);
    }
  }

  void await_equal(std::atomic<u32> &v, u32 desired) {
    u32 cur{};
    while (true) {
      for (int i = 0; i != 1024; ++i) {
        cur = v.load(std::memory_order_relaxed);
        if (cur == desired) return;
        pause_cpu();
      }
      Futex::wait(&v, cur, (u32)1 << (desired & (u32)0x1f));
    }
  }

  FutexResult Futex::wait(std::atomic<u32> *v, u32 expected, u32 waitMask) {
    return wait_for(v, expected, (i64)-1, waitMask);
  }

  FutexResult Futex::wait(u32 *v, u32 expected, u32 waitMask) {
    return wait_for(v, expected, (i64)-1, waitMask);
  }

  FutexResult Futex::wait(Atomic<u32> *v, u32 expected, u32 waitMask) {
    return wait_for(v, expected, (i64)-1, waitMask);
  }

  FutexResult Futex::wait_for(std::atomic<u32> *v, u32 expected, i64 duration, u32 waitMask) {
#if defined(ZS_PLATFORM_LINUX) && ZS_USE_NATIVE_LINUX_FUTEX
    struct timespec tm {};
    struct timespec *timeout = nullptr;
    if (duration > -1) {
      struct timespec offset {duration / 1000, (duration % 1000) * 1000000};
      clock_gettime(CLOCK_MONOTONIC, &tm);
      tm.tv_sec += offset.tv_sec;
      tm.tv_nsec += offset.tv_nsec;
      tm.tv_sec += tm.tv_nsec / 1000000000;
      tm.tv_nsec %= 1000000000;
      timeout = &tm;
    }
    int const op = FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG;
    long rc = syscall(SYS_futex, reinterpret_cast<u32 *>(v), op, expected, timeout, nullptr,
                      waitMask);
    if (rc == 0) return FutexResult::awoken;
    switch (rc) {
      case ETIMEDOUT:
        return FutexResult::timedout;
      case EINTR:
        return FutexResult::interrupted;
      case EWOULDBLOCK:
        return FutexResult::value_changed;
      default:
        return FutexResult::value_changed;
    }
#elif defined(_WIN32)
    // Use native WaitOnAddress on Windows (available since Windows 8).
    // WaitOnAddress does not support bit masks so we ignore waitMask here;
    // callers that need bit-mask semantics still go through the parking lot.
    if (waitMask == 0xffffffff) {
      return native_futex_wait_for(reinterpret_cast<void *>(v), expected, duration);
    }
    return emulated_futex_wait_for(v, expected, duration, waitMask);
#else
    return emulated_futex_wait_for(v, expected, duration, waitMask);
#endif
  }

  FutexResult Futex::wait_for(u32 *v, u32 expected, i64 duration, u32 waitMask) {
#if defined(ZS_PLATFORM_LINUX) && ZS_USE_NATIVE_LINUX_FUTEX
    struct timespec tm {};
    struct timespec *timeout = nullptr;
    if (duration > -1) {
      struct timespec offset {duration / 1000, (duration % 1000) * 1000000};
      clock_gettime(CLOCK_MONOTONIC, &tm);
      tm.tv_sec += offset.tv_sec;
      tm.tv_nsec += offset.tv_nsec;
      tm.tv_sec += tm.tv_nsec / 1000000000;
      tm.tv_nsec %= 1000000000;
      timeout = &tm;
    }
    int const op = FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG;
    long rc = syscall(SYS_futex, reinterpret_cast<u32 *>(v), op, expected, timeout, nullptr,
                      waitMask);
    if (rc == 0) return FutexResult::awoken;
    switch (rc) {
      case ETIMEDOUT:
        return FutexResult::timedout;
      case EINTR:
        return FutexResult::interrupted;
      case EWOULDBLOCK:
        return FutexResult::value_changed;
      default:
        return FutexResult::value_changed;
    }
#elif defined(_WIN32)
    if (waitMask == 0xffffffff) {
      return native_futex_wait_for(reinterpret_cast<void *>(v), expected, duration);
    }
    return emulated_futex_wait_for(v, expected, duration, waitMask);
#else
    return emulated_futex_wait_for(v, expected, duration, waitMask);
#endif
  }

  FutexResult Futex::wait_for(Atomic<u32> *v, u32 expected, i64 duration, u32 waitMask) {
    return wait_for(v->native_handle(), expected, duration, waitMask);
  }

  int Futex::wake(std::atomic<u32> *v, int count, u32 wakeMask) {
#if defined(ZS_PLATFORM_LINUX) && ZS_USE_NATIVE_LINUX_FUTEX
    int const op = FUTEX_WAKE_BITSET | FUTEX_PRIVATE_FLAG;
    long rc = syscall(SYS_futex, reinterpret_cast<u32 *>(v), op, count, nullptr, nullptr,
                      wakeMask);
    if (rc < 0) return 0;
    return static_cast<int>(rc);
#elif defined(_WIN32)
    if (wakeMask == 0xffffffff) {
      return native_futex_wake(reinterpret_cast<void *>(v), count);
    }
    return emulated_futex_wake(v, count, wakeMask);
#else
    return emulated_futex_wake(v, count, wakeMask);
#endif
  }

  int Futex::wake(u32 *v, int count, u32 wakeMask) {
#if defined(ZS_PLATFORM_LINUX) && ZS_USE_NATIVE_LINUX_FUTEX
    int const op = FUTEX_WAKE_BITSET | FUTEX_PRIVATE_FLAG;
    long rc = syscall(SYS_futex, reinterpret_cast<u32 *>(v), op, count, nullptr, nullptr,
                      wakeMask);
    if (rc < 0) return 0;
    return static_cast<int>(rc);
#elif defined(_WIN32)
    if (wakeMask == 0xffffffff) {
      return native_futex_wake(reinterpret_cast<void *>(v), count);
    }
    return emulated_futex_wake(v, count, wakeMask);
#else
    return emulated_futex_wake(v, count, wakeMask);
#endif
  }

  int Futex::wake(Atomic<u32> *v, int count, u32 wakeMask) {
    return wake(v->native_handle(), count, wakeMask);
  }

  void Mutex::lock() noexcept {
    u32 oldState = this->load(std::memory_order_relaxed);
    if (oldState == 0
        && this->compare_exchange_weak(oldState, oldState | _kMask, std::memory_order_acquire,
                                       std::memory_order_relaxed))
      return;

    u32 spinCount = 0;
    constexpr u32 spin_limit = 64;
    constexpr u32 yield_limit = 128;
    u32 newState;
  mutex_lock_retry:
    if ((oldState & _kMask) != 0) {
      ++spinCount;
      if (spinCount <= spin_limit) {
        pause_cpu();
      } else if (spinCount <= yield_limit) {
        yield_cpu();
      } else {
        newState = oldState | _kMask;
        if (newState != oldState) {
          if (!this->compare_exchange_weak(oldState, newState, std::memory_order_relaxed,
                                           std::memory_order_relaxed))
            goto mutex_lock_retry;
        }
        g_lot.parkFor(this, _kMask, [this, newState]() { return this->load() == newState; }, []() {},
                      -1);
        spinCount = 0;
      }
      oldState = this->load(std::memory_order_relaxed);
      goto mutex_lock_retry;
    }

    newState = oldState | _kMask;
    if (!this->compare_exchange_weak(oldState, newState, std::memory_order_acquire,
                                     std::memory_order_relaxed))
      goto mutex_lock_retry;
  }

  void Mutex::unlock() noexcept {
    u32 oldState = this->load(std::memory_order_relaxed);
    u32 newState;
    do {
      newState = oldState & ~_kMask;
    } while (!this->compare_exchange_weak(oldState, newState, std::memory_order_release,
                                          std::memory_order_relaxed));

    if (oldState & _kMask) {
      g_lot.unpark(this, [](const u32 &) { return detail::UnparkControl::RemoveBreak; });
    }
  }

  bool Mutex::try_lock() noexcept {
    u32 state = this->load(std::memory_order_relaxed);
    do {
      if (state) return false;
    } while (!this->compare_exchange_weak(state, state | _kMask, std::memory_order_acquire,
                                          std::memory_order_relaxed));
    return true;
  }

  /// Epoch-based ConditionVariable implementation.
  ///
  /// The `seq` member is a monotonically increasing epoch counter.
  ///
  /// wait():  snapshot the current epoch, unlock the mutex, then sleep
  ///          via Futex::wait until the epoch differs from the snapshot.
  ///          Re-lock the mutex before returning.  Spurious wakeups are
  ///          harmless because callers always re-check their predicate.
  ///
  /// notify_one/all():  increment the epoch and wake sleeping threads
  ///          via Futex::wake.  Because the epoch strictly increases,
  ///          a notify that races with a wait can never be lost -- the
  ///          waiter will either see the new epoch before sleeping
  ///          (Futex::wait returns value_changed immediately) or be
  ///          woken by the Futex::wake call.

  void ConditionVariable::wait(Mutex &lk) noexcept {
    const u32 epoch = seq.load(std::memory_order_acquire);
    lk.unlock();
    Futex::wait(&seq, epoch);
    lk.lock();
  }

  CvStatus ConditionVariable::wait_for(Mutex &lk, i64 duration) noexcept {
    const u32 epoch = seq.load(std::memory_order_acquire);
    lk.unlock();
    const auto res = Futex::wait_for(&seq, epoch, duration);
    lk.lock();
    return res == FutexResult::timedout ? CvStatus::timeout : CvStatus::no_timeout;
  }

  void ConditionVariable::notify_one() noexcept {
    seq.fetch_add(1, std::memory_order_release);
    Futex::wake(&seq, 1);
  }

  void ConditionVariable::notify_all() noexcept {
    seq.fetch_add(1, std::memory_order_release);
    Futex::wake(&seq);
  }

}  // namespace zs