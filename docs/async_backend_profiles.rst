Async Backend Profiles
======================

ZPC now has a small backend-profile layer for the async runtime so Vulkan and CUDA can be
reasoned about through the same utility vocabulary before concrete device adapters are attached.

Shared Utility Surface
----------------------

The shared utility mask models the common async control-plane capabilities that both Vulkan and
CUDA can provide:

* dispatch
* async copy
* barrier synchronization
* event-style synchronization
* staging workflows
* profiling hooks

These utilities are intentionally backend-agnostic. They describe what higher layers need from a
queue or stream-oriented backend rather than exposing API-specific details.

Backend Differences
-------------------

Vulkan is modeled as the stronger deployment and interaction backend:

* better cross-platform reach, including desktop and mobile deployment paths
* native graphics and present queues
* stronger fit for interactive tools and presentation-driven applications

CUDA is modeled as the stronger peak-performance backend:

* stronger throughput-oriented compute score
* collective-oriented extension path for multi-device compute
* better fit for pure compute or HPC-style execution pipelines

This matches the intended platform split:

* Vulkan first for portable, user-facing, interactive deployment
* CUDA first for state-of-the-art performance on supported NVIDIA systems

Human-Interactive Reflection
----------------------------

The user-facing profile and request structs in
``include/zensim/execution/AsyncBackendProfile.hpp`` are annotated with the zpc reflection macros.
They are suitable for editor forms, CLI configuration, and Python-side orchestration because their
reflected fields are primitive transport-friendly values.

When JIT reflection is enabled, the root CMake configuration now wires this header into the
reflection generator for ``zpc_py_interop`` with Python bindings enabled. That gives the async
backend profile layer a direct path to generated ctypes wrappers without introducing a separate
manual binding surface.