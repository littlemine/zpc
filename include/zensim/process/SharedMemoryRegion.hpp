#pragma once
/// @file SharedMemoryRegion.hpp
/// @brief Cross-process shared memory region.
///
/// Provides a platform-uniform API for creating, opening, and mapping
/// named shared memory regions.  The underlying implementation uses:
///   - Windows: CreateFileMappingA / OpenFileMappingA / MapViewOfFile
///   - POSIX:   shm_open / ftruncate / mmap
///
/// Memory ordering across processes still requires explicit fencing.
/// Use Futex or Atomic<u32> placed *inside* the shared region for
/// inter-process synchronisation.

#include <cstddef>

#include "zensim/TypeAlias.hpp"
#include "zensim/Platform.hpp"

#if defined(ZS_PLATFORM_WINDOWS)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace zs {

  /// @brief Named shared-memory region accessible from multiple OS processes.
  ///
  /// Typical workflow:
  /// @code
  ///   // Process A — creator
  ///   auto region = SharedMemoryRegion::create("my_buffer", 1 << 20);
  ///   float *ptr = region.as<float>();
  ///
  ///   // Process B — opener
  ///   auto region = SharedMemoryRegion::open("my_buffer", 1 << 20);
  ///   const float *ptr = region.as<const float>();
  ///
  ///   // When done (any process):
  ///   SharedMemoryRegion::unlink("my_buffer");
  /// @endcode
  class SharedMemoryRegion {
  public:
    SharedMemoryRegion() noexcept = default;

    SharedMemoryRegion(SharedMemoryRegion &&other) noexcept
        : _ptr{other._ptr}, _size{other._size}, _handle{other._handle}, _ownsName{other._ownsName} {
      other._ptr = nullptr;
      other._size = 0;
      other._handle = kInvalidHandle;
      other._ownsName = false;
    }

    SharedMemoryRegion &operator=(SharedMemoryRegion &&other) noexcept {
      if (this != &other) {
        destroy_();
        _ptr = other._ptr;
        _size = other._size;
        _handle = other._handle;
        _ownsName = other._ownsName;
        other._ptr = nullptr;
        other._size = 0;
        other._handle = kInvalidHandle;
        other._ownsName = false;
      }
      return *this;
    }

    ~SharedMemoryRegion() noexcept { destroy_(); }

    SharedMemoryRegion(const SharedMemoryRegion &) = delete;
    SharedMemoryRegion &operator=(const SharedMemoryRegion &) = delete;

    /// Create a new named region (or truncate an existing one).
    static SharedMemoryRegion create(const char *name, size_t sizeBytes) {
      SharedMemoryRegion region;
      region._size = sizeBytes;
      region._ownsName = true;

#if defined(ZS_PLATFORM_WINDOWS)
      region._handle = CreateFileMappingA(
          INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
          (DWORD)(sizeBytes >> 32), (DWORD)(sizeBytes & 0xFFFFFFFF), name);
      if (!region._handle) return {};
      region._ptr = MapViewOfFile(region._handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeBytes);
      if (!region._ptr) {
        CloseHandle(region._handle);
        region._handle = kInvalidHandle;
        return {};
      }
#else
      int fd = shm_open(name, O_CREAT | O_RDWR, 0666);
      if (fd < 0) return {};
      if (ftruncate(fd, (off_t)sizeBytes) != 0) {
        close(fd);
        shm_unlink(name);
        return {};
      }
      region._ptr = mmap(nullptr, sizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      region._handle = fd;
      if (region._ptr == MAP_FAILED) {
        region._ptr = nullptr;
        close(fd);
        shm_unlink(name);
        return {};
      }
      // fd can be closed after mmap; the mapping keeps the region alive
      close(fd);
      region._handle = kInvalidHandle;  // not needed after mmap
#endif
      return region;
    }

    /// Open an existing named region.
    static SharedMemoryRegion open(const char *name, size_t sizeBytes) {
      SharedMemoryRegion region;
      region._size = sizeBytes;
      region._ownsName = false;

#if defined(ZS_PLATFORM_WINDOWS)
      region._handle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
      if (!region._handle) return {};
      region._ptr = MapViewOfFile(region._handle, FILE_MAP_ALL_ACCESS, 0, 0, sizeBytes);
      if (!region._ptr) {
        CloseHandle(region._handle);
        region._handle = kInvalidHandle;
        return {};
      }
#else
      int fd = shm_open(name, O_RDWR, 0666);
      if (fd < 0) return {};
      region._ptr = mmap(nullptr, sizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      close(fd);
      if (region._ptr == MAP_FAILED) {
        region._ptr = nullptr;
        return {};
      }
#endif
      return region;
    }

    /// Remove the shared memory name from the OS namespace.
    /// Does NOT unmap existing views — those persist until destructed.
    static void unlink(const char *name) {
#if defined(ZS_PLATFORM_WINDOWS)
      // On Windows, the mapping is removed when the last handle is closed.
      (void)name;
#else
      shm_unlink(name);
#endif
    }

    bool valid() const noexcept { return _ptr != nullptr; }
    explicit operator bool() const noexcept { return valid(); }

    void *data() noexcept { return _ptr; }
    const void *data() const noexcept { return _ptr; }
    size_t size() const noexcept { return _size; }

    template <typename T> T *as() noexcept { return static_cast<T *>(_ptr); }
    template <typename T> const T *as() const noexcept { return static_cast<const T *>(_ptr); }

  private:
#if defined(ZS_PLATFORM_WINDOWS)
    using handle_t = HANDLE;
    static constexpr handle_t kInvalidHandle = nullptr;
#else
    using handle_t = int;
    static constexpr handle_t kInvalidHandle = -1;
#endif

    void destroy_() noexcept {
      if (!_ptr) return;
#if defined(ZS_PLATFORM_WINDOWS)
      UnmapViewOfFile(_ptr);
      if (_handle && _handle != INVALID_HANDLE_VALUE) CloseHandle(_handle);
#else
      munmap(_ptr, _size);
#endif
      _ptr = nullptr;
      _size = 0;
      _handle = kInvalidHandle;
    }

    void *_ptr{nullptr};
    size_t _size{0};
    handle_t _handle{kInvalidHandle};
    bool _ownsName{false};
  };

}  // namespace zs
