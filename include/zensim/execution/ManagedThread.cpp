#include "ManagedThread.hpp"

#if defined(ZS_PLATFORM_WINDOWS)
#  include <process.h>
#  include <windows.h>
#else
#  include <pthread.h>
#endif

namespace zs {

  ManagedThread::~ManagedThread() noexcept {
    if (joinable()) detach();
    release_handle();
  }

  bool ManagedThread::start(entry_fn entry, SmallString label) {
    if (joinable() || !entry) return false;
    _label = label;
    _stop = AsyncStopSource{};
    auto *block = new StartBlock{this, zs::move(entry)};
#if defined(ZS_PLATFORM_WINDOWS)
    unsigned threadId = 0;
    const uintptr_t handle = _beginthreadex(nullptr, 0, &ManagedThread::thread_proc, block, 0,
                                            &threadId);
    if (!handle) {
      delete block;
      return false;
    }
    _handle = reinterpret_cast<void *>(handle);
    _id = static_cast<u64>(threadId);
#else
    auto *threadObj = new pthread_t{};
    if (pthread_create(threadObj, nullptr, &ManagedThread::thread_proc, block) != 0) {
      delete threadObj;
      delete block;
      return false;
    }
    _handle = threadObj;
    _id = static_cast<u64>(*threadObj);
#endif
    _joinable.store(1, std::memory_order_release);
    return true;
  }

  bool ManagedThread::join() noexcept {
    if (!joinable()) return false;
#if defined(ZS_PLATFORM_WINDOWS)
    WaitForSingleObject(reinterpret_cast<HANDLE>(_handle), INFINITE);
#else
    pthread_join(*reinterpret_cast<pthread_t *>(_handle), nullptr);
#endif
    _joinable.store(0, std::memory_order_release);
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
    _joinable.store(0, std::memory_order_release);
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

  void ManagedThread::run_entry(ManagedThread &self, entry_fn &entry) noexcept {
    self._running.store(1, std::memory_order_release);
    try {
      entry(self);
    } catch (...) {
    }
    self._running.store(0, std::memory_order_release);
  }

#if defined(ZS_PLATFORM_WINDOWS)
  unsigned __stdcall ManagedThread::thread_proc(void *arg) {
    StartBlock *block = reinterpret_cast<StartBlock *>(arg);
    run_entry(*block->self, block->entry);
    delete block;
    return 0;
  }
#else
  void *ManagedThread::thread_proc(void *arg) {
    StartBlock *block = reinterpret_cast<StartBlock *>(arg);
    run_entry(*block->self, block->entry);
    delete block;
    return nullptr;
  }
#endif

}  // namespace zs