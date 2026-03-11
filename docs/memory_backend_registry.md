# Memory Backend Registry

The memory subsystem now uses a runtime registry to decouple the portable core from
backend-specific allocator and memory-operation implementations. This removes the
compile-time `#if ZS_ENABLE_CUDA` / `ZS_ENABLE_MUSA` / `ZS_ENABLE_SYCL` chains
that previously wired backend allocators directly into `Resource.h`.

## Why This Page Exists

This page documents the `MemoryBackendRegistry` design introduced in
`include/zensim/memory/MemoryBackend.h`. It is meant to be read alongside
[foundation_layer.md](foundation_layer.md) for the portable substrate rules and
[architecture_and_modularization.md](architecture_and_modularization.md) for
the backend-module layering that this registry enables.

## Problem Statement

Before this change the memory layer had several compile-time coupling problems:

- `Resource.h` conditionally included backend-specific allocator headers
  (`cuda/memory/Allocator.h`, `musa/memory/Allocator.h`, etc.) through
  preprocessor guards.
- `get_memory_source` and `get_virtual_memory_source` were `inline` functions
  in the header, using `match` and `is_memory_source_available` to dispatch
  through a compile-time variant. Every translation unit that touched a
  container or allocator pulled in the full backend allocator type hierarchy.
- `valid_memspace_for_execution` used `#if` chains to decide whether a given
  execution space was compatible with a given memory space. Adding or removing
  a backend required editing the core header.
- `Resource::copy` and `Resource::memset` used `constexpr if` on
  `is_memory_source_available(mem_device)` in the header, so the portable core
  could not compile a copy path without knowing at compile time which device
  backend was active.

These patterns meant the portable foundation could not exist without heavyweight
backend SDKs being visible at compile time, violating the dependency rules
described in [foundation_layer.md](foundation_layer.md).

## Design

### MemoryBackendRegistry

`MemoryBackendRegistry` is a process-lifetime singleton that holds runtime
factory functions and operation callbacks for each memory source (`memsrc_e`).

Its public surface is:

- **Registration** ！ backends register factory callables for default, advisor,
  temporary, stack-virtual, and arena-virtual memory resources. They also
  register `MemoryOpsBackend` structs carrying copy, memset, and transfer
  callbacks, and execution-space-to-memory-space compatibility entries.
- **Factory access** ！ `create_default_resource`, `create_advisor_resource`,
  `create_temporary_resource`, `create_stack_virtual_resource`,
  `create_arena_virtual_resource` return `UniquePtr<mr_t>` or
  `UniquePtr<vmr_t>` through the registered factories.
- **Queries** ！ `is_available(memsrc_e)` and
  `valid_memspace_for_execution(execspace_e, memsrc_e)` answer at runtime what
  the old preprocessor guards answered at compile time.
- **Memory operations** ！ `get_memory_ops(memsrc_e)` returns a pointer to the
  registered `MemoryOpsBackend` for a given memory source.

### Host Backend Auto-Registration

The host backend is always available. `MemoryBackendRegistry`'s constructor
registers:

- `default_memory_resource<host_mem_tag>`
- `advisor_memory_resource<host_mem_tag>`
- `stack_virtual_memory_resource<host_mem_tag>`
- `arena_virtual_memory_resource<host_mem_tag>` (on Windows and Unix)
- host copy and memset callbacks
- `execspace_e::host` and `execspace_e::openmp` compatibility with
  `memsrc_e::host`

No backend SDK is required for this registration.

### Device Backend Registration

Device backends (CUDA, MUSA, SYCL, ROCm) register themselves from their own
compilation units at static-init time or during explicit initialization. They
call `mark_available`, `register_default_resource`, `register_memory_ops`,
`register_exec_mem_compatibility`, and other registration methods on the
singleton.

This means the portable core never needs to `#include` a backend allocator
header. Backend registration is an additive, link-time concern.

### ZSPmrAllocator Changes

`ZSPmrAllocator` gains a new `init` method:

```cpp
void init(UniquePtr<resource_type> resource, MemoryLocation loc,
          function<UniquePtr<resource_type>()> clonerFn);
```

This replaces the template-heavy `setOwningUpstream` pattern for new code.
`setOwningUpstream` is preserved but marked `@deprecated` for backward
compatibility.

### get_memory_source / get_virtual_memory_source

These are no longer `inline` in the header. They are declared `ZPC_API` and
defined in `Resource.cpp`, dispatching through the registry. This removes the
last header-level dependency on backend allocator types.

### Resource::copy / Resource::memset

These are no longer `inline` in the header. They dispatch through
`MemoryBackendRegistry::get_memory_ops` at runtime, looking up the appropriate
copy or memset callback for the device memory source involved.

### valid_memspace_for_execution

This template function now delegates to the registry:

```cpp
template <typename Policy, bool is_virtual, typename T>
bool valid_memspace_for_execution(const Policy &,
                                  const ZSPmrAllocator<is_virtual, T> &allocator) {
  constexpr execspace_e space = Policy::exec_tag::value;
  return MemoryBackendRegistry::instance().valid_memspace_for_execution(
      space, allocator.location.memspace());
}
```

No `#if` chains remain in the header.

### is_memory_source_available

The compile-time template overload is preserved for backward compatibility in
template code. A new runtime overload is added:

```cpp
inline bool is_memory_source_available(memsrc_e mre) noexcept {
  return MemoryBackendRegistry::instance().is_available(mre);
}
```

## Concurrency

The registry is protected by `zs::Mutex` (the repo-native futex-based mutex
from `ConcurrencyPrimitive.hpp`), following the same pattern used in
`AsyncRuntime`. Registration is expected during static initialization or early
startup; factory access is read-heavy and protected by the same lock.

## File Layout

| File | Role |
|------|------|
| `memory/MemoryBackend.h` | Registry declaration, factory type aliases, `MemoryOpsBackend` |
| `memory/MemoryBackend.cpp` | Registry implementation, host backend auto-registration |
| `resource/Resource.h` | `ZSPmrAllocator` with `init()`, declared `get_memory_source` / `get_virtual_memory_source` |
| `resource/Resource.cpp` | `get_memory_source` / `get_virtual_memory_source` / `Resource::copy` / `Resource::memset` implementations |

## Relationship To Architecture Layers

This design directly supports the layering described in
[architecture_and_modularization.md](architecture_and_modularization.md):

- **Layer 1 (Portable Foundation)** ！ `MemoryBackend.h` and the host
  registration live here. No backend SDK is required.
- **Layer 3 (Backend Modules)** ！ each backend registers its own factories and
  ops from its own compilation unit, adding capability without modifying the
  core.
- **Layer 7 (Compatibility Facades)** ！ `Resource.h` and `ZSPmrAllocator`
  preserve backward compatibility through `setOwningUpstream` and the existing
  header surface.

## Migration Notes

- Existing code using `setOwningUpstream` continues to compile unchanged.
- New code should prefer `get_memory_source` / `get_virtual_memory_source` or
  the `init()` method on `ZSPmrAllocator`.
- Backend authors adding a new memory backend should register with
  `MemoryBackendRegistry::instance()` from a `.cpp` file linked into their
  backend module.

## Related Pages

- [foundation_layer.md](foundation_layer.md) for the portable substrate rules
  this design obeys
- [architecture_and_modularization.md](architecture_and_modularization.md) for
  the layer graph this design fits into
- [implementation_roadmap.md](implementation_roadmap.md) for the milestone
  sequence this design advances
- [runtime_core_design.md](runtime_core_design.md) for the parallel async
  runtime control-plane design
