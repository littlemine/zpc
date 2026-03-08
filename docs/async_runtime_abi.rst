Async Runtime ABI
=================

The deployable execution engine runtime needs a stable binary boundary for upgrades, plugins,
distributed deployment, and long-lived maintenance. The C++ async runtime types are useful for
in-tree development, but they are not suitable as the primary deployable ABI because they depend on
compiler, STL, and layout details that are not stable enough for plugin-style compatibility.

Stable Boundary
---------------

The ABI-stable boundary is now defined in
``include/zensim/execution/AsyncRuntimeAbi.hpp``.

The design rules are:

* fixed-width integer fields only
* opaque engine and submission handles
* explicit ``size`` and ABI version in every top-level structure
* append-only function tables and descriptors
* major-version gate for incompatible changes
* minor-version growth through larger structure sizes and reserved fields

This follows the same compatibility discipline expected by plugin systems.

Versioning Model
----------------

The current runtime ABI version is ``1.0.0``.

Compatibility rules:

* matching major version is required
* newer implementations may append fields and functions while preserving existing order
* callers must check structure size before accessing appended fields
* reserved slots exist to reduce pressure for interface reshaping

Current Surface
---------------

The initial ABI includes:

* engine descriptor query
* submission entry point
* host-event query
* cancellation entry point
* submission release entry point
* extension query entry point

The first concrete in-tree adapter now also exposes a discoverable host-submit extension,
``zpc.runtime.host_submit.v1``. That extension keeps the base engine table narrow while making the
current host-callback submission contract explicit.

This is intentionally narrow. It is enough to support runtime discovery, submission, lifecycle
control, and future extension lookup without freezing internal C++ implementation details.

Concrete Adapter
----------------

The current header now includes a first concrete bridge from the stable ABI into the in-tree
``AsyncRuntime``.

The adapter shape is:

* ``make_async_runtime_abi_engine()`` creates an engine handle backed by ``AsyncRuntime``
* ``async_runtime_abi_engine_table()`` returns the stable ``zpc_runtime_engine_v1_t`` table for
	that engine
* ``destroy_async_runtime_abi_engine()`` releases the engine handle

Host submission is intentionally modeled as an extension rather than a base-table expansion.

The ``zpc.runtime.host_submit.v1`` extension documents that:

* ``zpc_runtime_submission_desc_t::reserved[0]`` carries a
	``zpc_runtime_host_submit_payload_t`` pointer
* the payload contains a stable C callback and opaque user-data pointer
* the callback receives a ``zpc_runtime_host_task_context_t`` with submission id and current stop
	state so suspend or cancellation-aware host tasks can be expressed without leaking internal C++
	runtime types across the ABI boundary

This is a pragmatic first implementation step: it exercises the stable engine boundary with a real
runtime underneath it while keeping richer backend-native or validation transport surfaces available
for future queried extensions.

Upgrade Discipline
------------------

Future scalable compute expansion should happen behind this ABI rather than by changing the public
deployable interface directly.

That means:

* backend-specific growth should surface as capabilities or queried extensions
* new operations should be appended, not reordered
* engine internals may evolve as long as the ABI table and descriptor semantics remain compatible
* the C++ runtime API can keep improving internally while the deployable engine ABI stays stable

Parallel Development Discipline
-------------------------------

This ABI-stable boundary is also the prerequisite for coordinated parallel development.

Once the runtime, backend-adapter, validation, and tooling interfaces are consolidated enough,
multiple agents or workers should be able to develop different modules against the same stable
contracts without repeatedly changing the deployable surface.

That means the ABI and the surrounding interface contracts should be treated as the shared anchor
for parallel work across:

* deployable runtime implementation
* backend-specific execution bindings
* validation and upgrade-gating layers
* CLI, MCP, and Python-facing integration layers

If a proposed change would force unrelated modules to stop and renegotiate interfaces, it is a sign
that the surface is not yet consolidated enough for broad parallelization and should be stabilized
first.

Testing
-------

``test/async_runtime_abi.cpp`` now validates the version header, compatibility checks, engine
function-table contract, host-submit extension discovery, completed host submission, and suspended
task cancellation through the concrete ``AsyncRuntime`` adapter.