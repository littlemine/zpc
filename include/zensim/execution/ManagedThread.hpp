#pragma once

#include <atomic>

#include "zensim/Platform.hpp"
#include "zensim/ZpcAsync.hpp"
#include "zensim/ZpcFunction.hpp"
#include "zensim/types/SmallVector.hpp"

namespace zs {

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
    bool joinable() const noexcept { return _joinable.load(std::memory_order_acquire) != 0; }
    bool running() const noexcept { return _running.load(std::memory_order_acquire) != 0; }
    u64 id() const noexcept { return _id; }
    SmallString label() const noexcept { return _label; }
    void *native_handle() const noexcept { return _handle; }

  private:
    struct StartBlock {
      ManagedThread *self;
      entry_fn entry;
    };

    static void run_entry(ManagedThread &self, entry_fn &entry) noexcept;

#if defined(ZS_PLATFORM_WINDOWS)
    static unsigned __stdcall thread_proc(void *arg);
#else
    static void *thread_proc(void *arg);
#endif

    void release_handle() noexcept;

    void *_handle{nullptr};
    u64 _id{0};
    SmallString _label{};
    AsyncStopSource _stop{};
    std::atomic<u32> _joinable{0};
    std::atomic<u32> _running{0};
  };

}  // namespace zs