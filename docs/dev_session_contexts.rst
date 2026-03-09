Development Session Contexts
============================

ZPC now keeps a small, restart-friendly split of the active development environment on disk so a
new session does not need to reconstruct all state from chat history.

Why This Exists
---------------

The current async runtime and ABI work benefits from isolated build trees. Reusing one mixed build
directory for every task increases the chance of configuration drift, stale targets, and noisy
validation. The tracked session contexts below keep focused work separated by concern.

Tracked Session Contexts
------------------------

``runtime-abi``
  Script: ``tools/dev/windows/session_runtime_abi.cmd``
  Build dir: ``build-session-runtime-abi``
  Purpose: stable ABI engine work, ABI test target, and demo artifact.

``validation``
  Script: ``tools/dev/windows/session_validation.cmd``
  Build dir: ``build-session-validation``
  Purpose: validation schema, formatting, and comparison utilities.

``native-queue``
  Script: ``tools/dev/windows/session_native_queue.cmd``
  Build dir: ``build-session-native-queue``
  Purpose: backend-profile, native queue adapter, and ABI-native queue bridge.

Shared Toolchain Entry
----------------------

All tracked Windows session scripts call:

``tools/dev/windows/_vsdevcmd_x64.cmd``

That helper resolves the latest MSVC x64 developer environment through ``vswhere`` and initializes
the toolchain before configure or build steps run. It also exports the Visual Studio bundled
``ninja.exe`` into ``PATH`` so the tracked session scripts can use the Ninja generator
unconditionally.

Restart Snapshot
----------------

Current branch for this tracked line of work:

* ``copilot/parallel-20260308``

Current deployable ABI extensions already in flight:

* ``zpc.runtime.host_submit.v1``
* ``zpc.runtime.validation_report.v1``
* ``zpc.runtime.native_queue.v1``

Current human-observable artifact:

* demo executable target: ``asyncruntimeabidemo``
* demo source: ``test/async_runtime_abi_demo.cpp``
* demo notes: ``docs/async_runtime_abi_demo.rst``

Current filtered restart facts worth preserving:

* the ABI work is validated in host-only mode with CUDA, Vulkan, and JIT disabled in these session
  build trees unless a task explicitly needs a richer backend configuration
* commit and push still happen after each completed module iteration before the next validation
  cycle
* the current ABI growth rule remains append-only and extension-first; avoid widening the base
  engine table unless an extension boundary clearly fails

Suggested Restart Flow
----------------------

When resuming work:

1. Choose the narrowest session script that matches the task.
2. Read ``docs/async_runtime_abi.rst`` and this file instead of replaying the whole chat log.
3. Use the demo artifact when a human-readable sanity check is more useful than reading tests.
4. Persist only filtered, durable facts in repo docs or memory; avoid storing transient terminal
   noise as restart context.