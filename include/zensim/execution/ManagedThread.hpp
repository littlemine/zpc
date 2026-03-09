#pragma once
<<<<<<< HEAD
=======
#include <atomic>
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)

#include "zensim/Platform.hpp"
#include "zensim/ZpcAsync.hpp"
#include "zensim/ZpcFunction.hpp"
#include "zensim/types/SmallVector.hpp"

namespace zs {

<<<<<<< HEAD
  class ZPC_CORE_API ManagedThread {
  public:
    using entry_fn = function<void(ManagedThread &)>;

    ManagedThread() noexcept = default;
    ~ManagedThread() noexcept;

    ManagedThread(const ManagedThread &) = delete;
    ManagedThread &operator=(const ManagedThread &) = delete;

    ManagedThread(ManagedThread &&o) = delete;
    ManagedThread &operator=(ManagedThread &&o) = delete;

    bool start(entry_fn entry, SmallString label = {});
    bool join() noexcept;
    void detach() noexcept;
    void request_stop() noexcept { _stop.request_stop(); }
    void request_interrupt() noexcept { _stop.request_interrupt(); }
    bool stop_requested() const noexcept { return _stop.stop_requested(); }
    AsyncStopToken stop_token() const noexcept { return _stop.token(); }
    bool joinable() const noexcept { return _joinable.load() != 0; }
    bool running() const noexcept { return _running.load() != 0; }
    u64 id() const noexcept { return _id; }
    SmallString label() const noexcept { return _label; }
    void *native_handle() const noexcept { return _handle; }
    static void yield_current() noexcept;
    static u32 hardware_concurrency() noexcept;
    static void thread_entry_(ManagedThread *self) noexcept;

  private:
    static void run_entry(ManagedThread &self) noexcept;

    void release_handle() noexcept;

    entry_fn _entry{};
    void *_handle{nullptr};
    u64 _id{0};
    SmallString _label{};
    AsyncStopSource _stop{};
    Atomic<u32> _joinable{0};
    Atomic<u32> _running{0};
  };

}  // namespace zs
=======
  /// ManagedThread — cross-platform RAII thread wrapper with cooperative stop/interrupt.
  /// Replaces std::jthread with std-free internals.
  ///
  /// Usage:
  ///   ManagedThread t;
  ///   t.start([](ManagedThread& self) {
  ///     while (!self.stop_requested()) { /* work */ }
  ///   }, "worker");
  ///   t.request_stop();
  ///   t.join();
  ///
  struct ManagedThread {
    using entry_fn = function<void(ManagedThread&)>;
    using SmallString = BasicSmallString<>;

    ManagedThread() noexcept = default;
    ~ManagedThread();

    ManagedThread(const ManagedThread&) = delete;
    ManagedThread& operator=(const ManagedThread&) = delete;
    ManagedThread(ManagedThread&&) noexcept;
    ManagedThread& operator=(ManagedThread&&) noexcept;

    /// Launch the thread. Returns false if already running.
    ZPC_CORE_API bool start(entry_fn entry, SmallString threadLabel = {});

    /// Block until thread exits. Returns false if not joinable.
    ZPC_CORE_API bool join();

    /// Detach the thread (fire-and-forget). Returns false if not joinable.
    ZPC_CORE_API bool detach();

    /// Request cooperative stop. Thread should poll stop_requested().
    void request_stop() noexcept {
      if (_stopState) _stopState->requestStop();
    }

    /// Request interrupt (for yielding/preemption hints).
    void request_interrupt() noexcept {
      if (_stopState) _stopState->requestInterrupt();
    }

    /// Check stop state.
    bool stop_requested() const noexcept {
      return _stopState && _stopState->stopRequested();
    }

    /// Get a token for external code to observe stop state.
    AsyncStopToken stop_token() const noexcept { return AsyncStopToken{_stopState}; }

    bool joinable() const noexcept {
      return _state.load(std::memory_order_acquire) == state_e::running;
    }
    bool running() const noexcept {
      return _state.load(std::memory_order_acquire) == state_e::running;
    }

    u64 id() const noexcept { return _threadId; }
    SmallString label() const noexcept { return _label; }
    void* native_handle() const noexcept { return _nativeHandle; }

    // Called by platform thread proc — public for access from free functions in .cpp
    static void thread_entry_(ManagedThread* self);

  private:
    enum class state_e : u8 { idle = 0, running, joined, detached };

    entry_fn _entry{};
    AsyncStopState* _stopState{nullptr};
    void* _nativeHandle{nullptr};
    u64 _threadId{0};
    SmallString _label{};
    std::atomic<state_e> _state{state_e::idle};
  };

}  // namespace zs
>>>>>>> 27142866 (async execution framework: coroutines, scheduler, runtime)
