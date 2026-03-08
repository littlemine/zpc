#pragma once

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