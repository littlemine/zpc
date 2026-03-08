#include "zensim/execution/ManagedThread.hpp"

#if defined(ZS_PLATFORM_WINDOWS)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <process.h>
#else
#  include <pthread.h>
#  include <unistd.h>
#endif

namespace zs {

  ManagedThread::~ManagedThread() {
    if (joinable()) {
      request_stop();
      join();
    }
    if (_stopState) {
      _stopState->release();
      _stopState = nullptr;
    }
  }

  ManagedThread::ManagedThread(ManagedThread&& o) noexcept
      : _entry{zs::move(o._entry)},
        _stopState{zs::exchange(o._stopState, nullptr)},
        _nativeHandle{zs::exchange(o._nativeHandle, nullptr)},
        _threadId{zs::exchange(o._threadId, u64{0})},
        _label{o._label},
        _state{o._state.load(std::memory_order_relaxed)} {
    o._state.store(state_e::idle, std::memory_order_relaxed);
  }

  ManagedThread& ManagedThread::operator=(ManagedThread&& o) noexcept {
    if (this != &o) {
      if (joinable()) {
        request_stop();
        join();
      }
      if (_stopState) _stopState->release();

      _entry = zs::move(o._entry);
      _stopState = zs::exchange(o._stopState, nullptr);
      _nativeHandle = zs::exchange(o._nativeHandle, nullptr);
      _threadId = zs::exchange(o._threadId, u64{0});
      _label = o._label;
      _state.store(o._state.load(std::memory_order_relaxed), std::memory_order_relaxed);
      o._state.store(state_e::idle, std::memory_order_relaxed);
    }
    return *this;
  }

  void ManagedThread::thread_entry_(ManagedThread* self) {
    if (self && self->_entry) self->_entry(*self);
  }

#if defined(ZS_PLATFORM_WINDOWS)

  static unsigned __stdcall win32_thread_proc(void* arg) {
    auto* self = static_cast<ManagedThread*>(arg);
    ManagedThread::thread_entry_(self);
    return 0;
  }

  bool ManagedThread::start(entry_fn entry, SmallString threadLabel) {
    auto expected = state_e::idle;
    if (!_state.compare_exchange_strong(expected, state_e::running)) return false;

    _entry = zs::move(entry);
    _label = threadLabel;

    if (_stopState) _stopState->release();
    _stopState = ::new AsyncStopState{};

    auto handle
        = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, win32_thread_proc, this, 0, nullptr));
    if (!handle) {
      _state.store(state_e::idle, std::memory_order_release);
      return false;
    }
    _nativeHandle = static_cast<void*>(handle);
    _threadId = static_cast<u64>(GetThreadId(handle));
    return true;
  }

  bool ManagedThread::join() {
    auto expected = state_e::running;
    if (!_state.compare_exchange_strong(expected, state_e::joined)) return false;

    if (_nativeHandle) {
      WaitForSingleObject(static_cast<HANDLE>(_nativeHandle), INFINITE);
      CloseHandle(static_cast<HANDLE>(_nativeHandle));
      _nativeHandle = nullptr;
    }
    return true;
  }

  bool ManagedThread::detach() {
    auto expected = state_e::running;
    if (!_state.compare_exchange_strong(expected, state_e::detached)) return false;

    if (_nativeHandle) {
      CloseHandle(static_cast<HANDLE>(_nativeHandle));
      _nativeHandle = nullptr;
    }
    return true;
  }

#else  // POSIX

  static void* posix_thread_proc(void* arg) {
    auto* self = static_cast<ManagedThread*>(arg);
    ManagedThread::thread_entry_(self);
    return nullptr;
  }

  bool ManagedThread::start(entry_fn entry, SmallString threadLabel) {
    auto expected = state_e::idle;
    if (!_state.compare_exchange_strong(expected, state_e::running)) return false;

    _entry = zs::move(entry);
    _label = threadLabel;

    if (_stopState) _stopState->release();
    _stopState = ::new AsyncStopState{};

    pthread_t pt;
    int rc = pthread_create(&pt, nullptr, posix_thread_proc, this);
    if (rc != 0) {
      _state.store(state_e::idle, std::memory_order_release);
      return false;
    }
    _nativeHandle = reinterpret_cast<void*>(pt);
    _threadId = static_cast<u64>(pt);

#  if defined(ZS_PLATFORM_LINUX) && threadLabel.asChars()[0] != '\0'
    pthread_setname_np(pt, threadLabel.asChars());
#  endif

    return true;
  }

  bool ManagedThread::join() {
    auto expected = state_e::running;
    if (!_state.compare_exchange_strong(expected, state_e::joined)) return false;

    if (_nativeHandle) {
      pthread_join(reinterpret_cast<pthread_t>(_nativeHandle), nullptr);
      _nativeHandle = nullptr;
    }
    return true;
  }

  bool ManagedThread::detach() {
    auto expected = state_e::running;
    if (!_state.compare_exchange_strong(expected, state_e::detached)) return false;

    if (_nativeHandle) {
      pthread_detach(reinterpret_cast<pthread_t>(_nativeHandle));
      _nativeHandle = nullptr;
    }
    return true;
  }

#endif

}  // namespace zs
