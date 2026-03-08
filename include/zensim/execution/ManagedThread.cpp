#include "ManagedThread.hpp"

#if defined(ZS_PLATFORM_WINDOWS)
#  include <process.h>
#  include <windows.h>
#else
#  include <pthread.h>
#  include <sched.h>
#  include <unistd.h>
#endif

namespace zs {

#if defined(ZS_PLATFORM_WINDOWS)
  static unsigned __stdcall win32_thread_proc(void *arg) {
    auto *self = reinterpret_cast<ManagedThread *>(arg);
    ManagedThread::thread_entry_(self);
    return 0;
  }
#else
  static void *posix_thread_proc(void *arg) {
    auto *self = reinterpret_cast<ManagedThread *>(arg);
    ManagedThread::thread_entry_(self);
    return nullptr;
  }
#endif

  ManagedThread::~ManagedThread() noexcept {
    if (joinable()) detach();
    release_handle();
  }

  void ManagedThread::yield_current() noexcept {
#if defined(ZS_PLATFORM_WINDOWS)
    SwitchToThread();
#else
    sched_yield();
#endif
  }

  u32 ManagedThread::hardware_concurrency() noexcept {
#if defined(ZS_PLATFORM_WINDOWS)
    const auto count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return count ? static_cast<u32>(count) : 1u;
#else
    const long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? static_cast<u32>(count) : 1u;
#endif
  }

  bool ManagedThread::start(entry_fn entry, SmallString label) {
    if (joinable() || !entry) return false;
    _entry = zs::move(entry);
    _label = label;
    _stop = AsyncStopSource{};
#if defined(ZS_PLATFORM_WINDOWS)
    unsigned threadId = 0;
  const uintptr_t handle = _beginthreadex(nullptr, 0, &win32_thread_proc, this, 0,
                                            &threadId);
    if (!handle) {
      _entry = {};
      return false;
    }
    _handle = reinterpret_cast<void *>(handle);
    _id = static_cast<u64>(threadId);
#else
    auto *threadObj = new pthread_t{};
  if (pthread_create(threadObj, nullptr, &posix_thread_proc, this) != 0) {
      delete threadObj;
      _entry = {};
      return false;
    }
    _handle = threadObj;
    _id = static_cast<u64>(*threadObj);
#endif
    _joinable.store(1);
    return true;
  }

  bool ManagedThread::join() noexcept {
    if (!joinable()) return false;
#if defined(ZS_PLATFORM_WINDOWS)
    WaitForSingleObject(reinterpret_cast<HANDLE>(_handle), INFINITE);
#else
    pthread_join(*reinterpret_cast<pthread_t *>(_handle), nullptr);
#endif
    _joinable.store(0);
    release_handle();
    return true;
  }

  void ManagedThread::detach() noexcept {
    if (!joinable()) return;
#if defined(ZS_PLATFORM_WINDOWS)
    CloseHandle(reinterpret_cast<HANDLE>(_handle));
#else
    pthread_detach(*reinterpret_cast<pthread_t *>(_handle));
    delete reinterpret_cast<pthread_t *>(_handle);
#endif
    _handle = nullptr;
    _joinable.store(0);
  }

  void ManagedThread::release_handle() noexcept {
    if (!_handle) return;
#if defined(ZS_PLATFORM_WINDOWS)
    CloseHandle(reinterpret_cast<HANDLE>(_handle));
#else
    delete reinterpret_cast<pthread_t *>(_handle);
#endif
    _handle = nullptr;
  }

  void ManagedThread::run_entry(ManagedThread &self) noexcept {
    self._running.store(1);
    try {
      if (self._entry) self._entry(self);
    } catch (...) {
    }
    self._running.store(0);
  }

  void ManagedThread::thread_entry_(ManagedThread *self) noexcept {
    if (!self) return;
    run_entry(*self);
  }

}  // namespace zs