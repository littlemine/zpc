#include "MemoryBackend.h"

#include <mutex>
#include <sstream>
#include <stdexcept>

#include "Allocator.h"
#include "MemOps.hpp"

namespace zs {

  MemoryBackendRegistry &MemoryBackendRegistry::instance() {
    static MemoryBackendRegistry *s_instance = new MemoryBackendRegistry();
    return *s_instance;
  }

  MemoryBackendRegistry::MemoryBackendRegistry() {
    _available[0] = _available[1] = _available[2] = false;

    // --- host backend is always available ---
    mark_available(memsrc_e::host);

    register_default_resource(memsrc_e::host, [](ProcID devid) -> UniquePtr<mr_t> {
      return zs::make_unique<default_memory_resource<host_mem_tag>>(devid);
    });

    register_advisor_resource(
        memsrc_e::host,
        [](ProcID devid, std::string_view advice) -> UniquePtr<mr_t> {
          return zs::make_unique<advisor_memory_resource<host_mem_tag>>(devid, advice);
        });

    register_temporary_resource(memsrc_e::host, [](ProcID devid) -> UniquePtr<mr_t> {
      return zs::make_unique<default_memory_resource<host_mem_tag>>(devid);
    });

    register_stack_virtual_resource(
        memsrc_e::host, [](ProcID devid, size_t bytes) -> UniquePtr<vmr_t> {
          return zs::make_unique<stack_virtual_memory_resource<host_mem_tag>>(devid, bytes);
        });

#if defined(ZS_PLATFORM_WINDOWS) || defined(ZS_PLATFORM_UNIX)
    register_arena_virtual_resource(
        memsrc_e::host, [](ProcID devid, size_t bytes) -> UniquePtr<vmr_t> {
          return zs::make_unique<arena_virtual_memory_resource<host_mem_tag>>(devid, bytes);
        });
#endif

    MemoryOpsBackend hostOps;
    hostOps.copy = [](void *dst, void *src, size_t bytes) {
      zs::copy(mem_host, dst, src, bytes);
    };
    hostOps.memset = [](void *ptr, int ch, size_t bytes) {
      zs::memset(mem_host, ptr, ch, bytes);
    };
    register_memory_ops(memsrc_e::host, zs::move(hostOps));

    // host execution spaces are compatible with host memory
    register_exec_mem_compatibility(execspace_e::host, memsrc_e::host);
    register_exec_mem_compatibility(execspace_e::openmp, memsrc_e::host);
  }

  void MemoryBackendRegistry::mark_available(memsrc_e mre) {
    std::lock_guard<Mutex> lock(_mutex);
    _available[static_cast<unsigned char>(mre)] = true;
  }

  bool MemoryBackendRegistry::is_available(memsrc_e mre) const {
    std::lock_guard<Mutex> lock(_mutex);
    return _available[static_cast<unsigned char>(mre)];
  }

  bool MemoryBackendRegistry::valid_memspace_for_execution(execspace_e space,
                                                           memsrc_e mre) const {
    std::lock_guard<Mutex> lock(_mutex);
    auto it = _execMemCompat.find(static_cast<unsigned char>(space));
    if (it == _execMemCompat.end()) {
      // fallback: sequential is always compatible with host
      return space == execspace_e::host && mre == memsrc_e::host;
    }
    for (auto m : it->second)
      if (m == mre) return true;
    return false;
  }

  // --- registration helpers ---

  void MemoryBackendRegistry::register_default_resource(memsrc_e mre, mr_factory_fn factory) {
    std::lock_guard<Mutex> lock(_mutex);
    _defaultFactories[static_cast<unsigned char>(mre)] = zs::move(factory);
  }

  void MemoryBackendRegistry::register_advisor_resource(memsrc_e mre,
                                                        mr_advisor_factory_fn factory) {
    std::lock_guard<Mutex> lock(_mutex);
    _advisorFactories[static_cast<unsigned char>(mre)] = zs::move(factory);
  }

  void MemoryBackendRegistry::register_temporary_resource(memsrc_e mre, mr_factory_fn factory) {
    std::lock_guard<Mutex> lock(_mutex);
    _temporaryFactories[static_cast<unsigned char>(mre)] = zs::move(factory);
  }

  void MemoryBackendRegistry::register_stack_virtual_resource(memsrc_e mre,
                                                              vmr_factory_fn factory) {
    std::lock_guard<Mutex> lock(_mutex);
    _stackVmrFactories[static_cast<unsigned char>(mre)] = zs::move(factory);
  }

  void MemoryBackendRegistry::register_arena_virtual_resource(memsrc_e mre,
                                                              vmr_factory_fn factory) {
    std::lock_guard<Mutex> lock(_mutex);
    _arenaVmrFactories[static_cast<unsigned char>(mre)] = zs::move(factory);
  }

  void MemoryBackendRegistry::register_memory_ops(memsrc_e mre, MemoryOpsBackend ops) {
    std::lock_guard<Mutex> lock(_mutex);
    _memOps[static_cast<unsigned char>(mre)] = zs::move(ops);
  }

  void MemoryBackendRegistry::register_exec_mem_compatibility(execspace_e space, memsrc_e mre) {
    std::lock_guard<Mutex> lock(_mutex);
    auto &vec = _execMemCompat[static_cast<unsigned char>(space)];
    for (auto m : vec)
      if (m == mre) return;
    vec.push_back(mre);
  }

  // --- factory access ---

  UniquePtr<mr_t> MemoryBackendRegistry::create_default_resource(memsrc_e mre,
                                                                 ProcID devid) const {
    std::lock_guard<Mutex> lock(_mutex);
    auto it = _defaultFactories.find(static_cast<unsigned char>(mre));
    if (it == _defaultFactories.end()) {
      std::ostringstream oss;
      oss << "no default memory resource registered for memsrc_e("
          << static_cast<int>(mre) << ")";
      throw std::runtime_error(oss.str());
    }
    return it->second(devid);
  }

  UniquePtr<mr_t> MemoryBackendRegistry::create_advisor_resource(memsrc_e mre, ProcID devid,
                                                                 std::string_view advice) const {
    std::lock_guard<Mutex> lock(_mutex);
    auto it = _advisorFactories.find(static_cast<unsigned char>(mre));
    if (it == _advisorFactories.end()) {
      std::ostringstream oss;
      oss << "no advisor memory resource registered for memsrc_e("
          << static_cast<int>(mre) << ")";
      throw std::runtime_error(oss.str());
    }
    return it->second(devid, advice);
  }

  UniquePtr<mr_t> MemoryBackendRegistry::create_temporary_resource(memsrc_e mre,
                                                                   ProcID devid) const {
    std::lock_guard<Mutex> lock(_mutex);
    auto it = _temporaryFactories.find(static_cast<unsigned char>(mre));
    if (it != _temporaryFactories.end()) return it->second(devid);
    // fall back to default
    auto dit = _defaultFactories.find(static_cast<unsigned char>(mre));
    if (dit == _defaultFactories.end()) {
      std::ostringstream oss;
      oss << "no temporary/default memory resource registered for memsrc_e("
          << static_cast<int>(mre) << ")";
      throw std::runtime_error(oss.str());
    }
    return dit->second(devid);
  }

  UniquePtr<vmr_t> MemoryBackendRegistry::create_stack_virtual_resource(memsrc_e mre,
                                                                        ProcID devid,
                                                                        size_t bytes) const {
    std::lock_guard<Mutex> lock(_mutex);
    auto it = _stackVmrFactories.find(static_cast<unsigned char>(mre));
    if (it == _stackVmrFactories.end()) {
      std::ostringstream oss;
      oss << "no stack virtual memory resource registered for memsrc_e("
          << static_cast<int>(mre) << ")";
      throw std::runtime_error(oss.str());
    }
    return it->second(devid, bytes);
  }

  UniquePtr<vmr_t> MemoryBackendRegistry::create_arena_virtual_resource(memsrc_e mre,
                                                                        ProcID devid,
                                                                        size_t bytes) const {
    std::lock_guard<Mutex> lock(_mutex);
    auto it = _arenaVmrFactories.find(static_cast<unsigned char>(mre));
    if (it == _arenaVmrFactories.end()) {
      std::ostringstream oss;
      oss << "no arena virtual memory resource registered for memsrc_e("
          << static_cast<int>(mre) << ")";
      throw std::runtime_error(oss.str());
    }
    return it->second(devid, bytes);
  }

  const MemoryOpsBackend *MemoryBackendRegistry::get_memory_ops(memsrc_e mre) const {
    std::lock_guard<Mutex> lock(_mutex);
    auto it = _memOps.find(static_cast<unsigned char>(mre));
    if (it == _memOps.end()) return nullptr;
    return &it->second;
  }

}  // namespace zs
