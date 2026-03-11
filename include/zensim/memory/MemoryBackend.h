#pragma once

#include <string_view>
#include <unordered_map>
#include <vector>

#include "MemoryResource.h"
#include "zensim/ZpcFunction.hpp"
#include "zensim/ZpcResource.hpp"
#include "zensim/execution/ConcurrencyPrimitive.hpp"

namespace zs {

  /// @brief Describes a memory resource factory for a given (memsrc_e, resource_kind) pair.
  /// Backends register these at static-init time so the core never needs
  /// compile-time knowledge of backend-specific allocator headers.
  enum class resource_kind_e : unsigned char {
    default_resource,
    advisor_resource,
    temporary_resource,
    stack_virtual_resource,
    arena_virtual_resource,
  };

  /// @brief Factory signature: (ProcID, extra args packed in a type-erased blob) -> UniquePtr<mr_t>
  using mr_factory_fn = function<UniquePtr<mr_t>(ProcID devid)>;
  using mr_advisor_factory_fn = function<UniquePtr<mr_t>(ProcID devid, std::string_view advice)>;
  using vmr_factory_fn = function<UniquePtr<vmr_t>(ProcID devid, size_t bytes)>;

  /// @brief Callbacks for backend-specific copy / memset operations.
  struct MemoryOpsBackend {
    function<void(void *dst, void *src, size_t bytes)> copy{};
    function<void(void *dst, void *src, size_t bytes)> copyHtoD{};
    function<void(void *dst, void *src, size_t bytes)> copyDtoH{};
    function<void(void *dst, void *src, size_t bytes)> copyDtoD{};
    function<void(void *ptr, int ch, size_t bytes)> memset{};
  };

  /// @brief Central runtime registry for memory backends.
  /// Intrinsic backends (host, cuda, musa, etc.) self-register at static-init time.
  /// Custom backends can be registered at any point before first use.
  class ZPC_CORE_API MemoryBackendRegistry {
  public:
    static MemoryBackendRegistry &instance();

    /// --- registration ---
    void register_default_resource(memsrc_e mre, mr_factory_fn factory);
    void register_advisor_resource(memsrc_e mre, mr_advisor_factory_fn factory);
    void register_temporary_resource(memsrc_e mre, mr_factory_fn factory);
    void register_stack_virtual_resource(memsrc_e mre, vmr_factory_fn factory);
    void register_arena_virtual_resource(memsrc_e mre, vmr_factory_fn factory);
    void register_memory_ops(memsrc_e mre, MemoryOpsBackend ops);

    /// Mark a memory source as available (called during backend registration).
    void mark_available(memsrc_e mre);

    /// Runtime query: is a particular memory source available?
    bool is_available(memsrc_e mre) const;

    /// Runtime query: is a particular execution space valid for a memory source?
    bool valid_memspace_for_execution(execspace_e space, memsrc_e mre) const;

    /// --- factory access ---
    UniquePtr<mr_t> create_default_resource(memsrc_e mre, ProcID devid) const;
    UniquePtr<mr_t> create_advisor_resource(memsrc_e mre, ProcID devid,
                                            std::string_view advice) const;
    UniquePtr<mr_t> create_temporary_resource(memsrc_e mre, ProcID devid) const;
    UniquePtr<vmr_t> create_stack_virtual_resource(memsrc_e mre, ProcID devid,
                                                   size_t bytes) const;
    UniquePtr<vmr_t> create_arena_virtual_resource(memsrc_e mre, ProcID devid,
                                                   size_t bytes) const;

    /// --- memory ops ---
    const MemoryOpsBackend *get_memory_ops(memsrc_e mre) const;

    /// --- execution-space ? memory-space compatibility ---
    void register_exec_mem_compatibility(execspace_e space, memsrc_e mre);

  private:
    MemoryBackendRegistry();

    mutable Mutex _mutex;

    std::unordered_map<unsigned char, mr_factory_fn> _defaultFactories;
    std::unordered_map<unsigned char, mr_advisor_factory_fn> _advisorFactories;
    std::unordered_map<unsigned char, mr_factory_fn> _temporaryFactories;
    std::unordered_map<unsigned char, vmr_factory_fn> _stackVmrFactories;
    std::unordered_map<unsigned char, vmr_factory_fn> _arenaVmrFactories;
    std::unordered_map<unsigned char, MemoryOpsBackend> _memOps;

    bool _available[5]{};  // indexed by memsrc_e (host, device, um, file_mapped, shared_ipc)

    /// exec-space -> set of compatible mem-spaces
    std::unordered_map<unsigned char, std::vector<memsrc_e>> _execMemCompat;
  };

}  // namespace zs
