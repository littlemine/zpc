#pragma once
/// @file MappedFile.hpp
/// @brief Memory-mapped file abstraction built on top of vmr_t.
///
/// MappedFile exposes a file's contents as a contiguous virtual-memory region.
/// The OS lazily pages data in/out, giving near-zero-copy I/O for large
/// datasets (particle caches, volume grids, neural-network weights, etc.).
///
/// The interface inherits from vmr_t so the same commit/evict/protect API
/// used for anonymous virtual memory works transparently on file-backed
/// mappings.
///
/// Platform mapping:
///   Windows: CreateFileMapping + MapViewOfFile + VirtualProtect
///   POSIX:   open + mmap + mprotect

#include <cstddef>
#include <string>

#include "zensim/Platform.hpp"
#include "zensim/memory/MemoryResource.h"

namespace zs {

  /// How a MappedFile should be opened.
  enum class MappedFileAccess : unsigned char {
    read_only,         ///< GENERIC_READ / O_RDONLY
    read_write,        ///< GENERIC_READ|GENERIC_WRITE / O_RDWR
    copy_on_write,     ///< MAP_PRIVATE semantics — writes don't reach disk
  };

  /// @brief Memory-mapped file as a virtual memory resource.
  ///
  /// Construction opens and maps the file.  Destruction unmaps and closes.
  /// The mapped region is page-aligned and may exceed the logical file size
  /// (trailing bytes are zero-filled by the OS).
  ///
  /// Usage:
  /// @code
  ///   MappedFile mf("data/particles.bin", MappedFileAccess::read_only);
  ///   if (!mf.is_mapped()) { /* handle error */ }
  ///
  ///   auto *data = static_cast<const float *>(mf.address(0));
  ///   size_t numFloats = mf.file_size() / sizeof(float);
  ///
  ///   // Advise sequential access (madvise / PrefetchVirtualMemory):
  ///   mf.commit(0, mf.file_size());
  /// @endcode
  ///
  /// For writable mappings that should grow the file:
  /// @code
  ///   MappedFile mf("output.bin", MappedFileAccess::read_write, 1 << 30);
  ///   // Now 1 GiB of address space is mapped.
  ///   mf.commit(0, vmr_t::s_chunk_granularity);
  ///   auto *out = static_cast<char *>(mf.address(0));
  ///   // ...write data...
  ///   mf.flush(0, bytesWritten);  // ensure durability
  /// @endcode
  class ZPC_CORE_API MappedFile : public vmr_t {
  public:
    /// Open and map a file.
    /// @param path       Filesystem path (UTF-8).
    /// @param access     Desired access mode.
    /// @param mapSize    If > 0, map this many bytes (file is extended if
    ///                   necessary for read_write mode).  If 0, map the
    ///                   entire existing file.
    MappedFile(const std::string &path,
               MappedFileAccess access = MappedFileAccess::read_only,
               size_t mapSize = 0);

    /// Unmap and close.
    ~MappedFile() override;

    // Non-copyable, movable.
    MappedFile(const MappedFile &) = delete;
    MappedFile &operator=(const MappedFile &) = delete;
    MappedFile(MappedFile &&o) noexcept;
    MappedFile &operator=(MappedFile &&o) noexcept;

    /// Whether the file was successfully mapped.
    bool is_mapped() const noexcept { return _addr != nullptr; }

    /// Logical file size (bytes).  May be smaller than the mapped region
    /// (the region is rounded up to page granularity).
    size_t file_size() const noexcept { return _fileSize; }

    /// Total mapped region size (bytes, page-aligned).
    size_t mapped_size() const noexcept { return _mappedSize; }

    /// Filesystem path this object was opened with.
    const std::string &path() const noexcept { return _path; }

    /// Flush dirty pages to the underlying file.
    /// @param offset  Byte offset within the mapping.
    /// @param bytes   Number of bytes to flush (0 = entire mapping).
    /// @return true on success.
    bool flush(size_t offset = 0, size_t bytes = 0);

    // ── vmr_t interface ────────────────────────────────────────────────

    /// commit() on a file mapping acts as an advisory prefetch hint
    /// (PrefetchVirtualMemory / madvise MADV_WILLNEED).
    bool do_commit(size_t offset, size_t bytes) override;

    /// evict() advises the OS that pages are no longer needed
    /// (madvise MADV_DONTNEED / DiscardVirtualMemory).
    bool do_evict(size_t offset, size_t bytes) override;

    /// File-backed pages are always "resident" if the mapping is valid.
    /// Returns true when the mapping covers the requested range.
    bool do_check_residency(size_t offset, size_t bytes) const override;

    void *do_address(size_t offset) const override;

    bool do_protect(size_t offset, size_t bytes, PageAccess access) override;

    size_t do_reserved_bytes() const noexcept override { return _mappedSize; }

  private:
    void close_mapping() noexcept;

    std::string _path{};
    MappedFileAccess _access{MappedFileAccess::read_only};
    void *_addr{nullptr};
    size_t _fileSize{0};
    size_t _mappedSize{0};

#ifdef ZS_PLATFORM_WINDOWS
    void *_fileHandle{nullptr};     // HANDLE (INVALID_HANDLE_VALUE when closed)
    void *_mappingHandle{nullptr};  // HANDLE from CreateFileMapping
#else
    int _fd{-1};                    // POSIX file descriptor
#endif
  };

}  // namespace zs
