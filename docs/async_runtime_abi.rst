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

It also exposes a validation-report extension, ``zpc.runtime.validation_report.v1``, so
machine-readable validation payloads can move across the deployable ABI without forcing validation
transport details into the base engine table.

It now also exposes a resource-manager extension, ``zpc.runtime.resource_manager.v1``, so
runtime-owned resource registration, maintenance, and retirement control can cross the deployable
boundary without flattening ``AsyncResourceManager`` directly into the base engine table.

The next queried extension layer now also begins exposing backend-native queue submission through
``zpc.runtime.native_queue.v1`` so queue or stream-oriented backends can reuse the deployable ABI
without flattening backend-specific signaling hooks into the base engine table.

Submission descriptors now also reserve ``reserved[1]`` for an append-only
``zpc_runtime_dependency_list_v1_t`` payload so dependency propagation can grow without widening
the base engine table.

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
* ``zpc_runtime_submission_desc_t::reserved[1]`` may carry a
	``zpc_runtime_dependency_list_v1_t`` pointer describing prerequisite submission events
* the payload contains a stable C callback and opaque user-data pointer
* the callback receives a ``zpc_runtime_host_task_context_t`` with submission id and current stop
	state so suspend or cancellation-aware host tasks can be expressed without leaking internal C++
	runtime types across the ABI boundary

The dependency list is append-only and currently supports two token kinds:

* ``ZPC_RUNTIME_DEPENDENCY_SUBMISSION_EVENT`` for runtime-managed prerequisite submissions
* ``ZPC_RUNTIME_DEPENDENCY_NATIVE_SIGNAL`` for foreign backend signal handles

Plain host submission currently accepts only runtime submission-event dependencies. Foreign native
signals remain unsupported on that path because there is no native queue binding available to wait
on them.

This is a pragmatic first implementation step: it exercises the stable engine boundary with a real
runtime underneath it while keeping richer backend-native or validation transport surfaces available
for future queried extensions.

Validation Report Extension
---------------------------

The ``zpc.runtime.validation_report.v1`` extension now defines the first stable validation export
surface on the deployable runtime ABI.

It exposes three narrow operations:

* query the latest published validation summary through ``zpc_runtime_validation_summary_v1_t``
* query the latest deterministic JSON report as a ``zpc_runtime_string_view_t``
* query the latest CLI-oriented text summary as a ``zpc_runtime_string_view_t``

The adapter-side helpers ``publish_async_runtime_validation_report()`` and
``clear_async_runtime_validation_report()`` let in-tree runtime integrations publish the existing
``ValidationSuiteReport`` model without freezing the internal schema structures directly into the C
ABI. That keeps the deployable surface transport-friendly while reusing the already established
validation schema and formatting stack.

Resource Manager Extension
--------------------------

The ``zpc.runtime.resource_manager.v1`` extension is the first deployable ABI bridge for the
runtime-side resource control plane.

The current host-validated shape exposes:

* stable registration through ``zpc_runtime_resource_desc_v1_t`` with opaque payload storage,
	maintenance callback, and destroy callback hooks
* stable inspection through ``zpc_runtime_resource_info_v1_t`` so hosts can query resource label,
	executor, queue or backend metadata, dirty or busy or retired or stale flags, lease count, and
	last-access epoch without taking ownership of the payload
* explicit lease acquire or release through an opaque ``zpc_runtime_resource_lease_handle_t`` so
	payload access does not leak C++ ownership types across the ABI boundary
* ``touch``, ``mark_dirty``, ``advance_epoch``, and ``query_stats`` operations so external tools
	can drive the same dirty or stale lifecycle model used by the in-tree ``AsyncResourceManager``
* explicit maintenance scheduling through ``zpc_runtime_resource_maintenance_request_v1_t`` with a
	returned submission handle and disposition code
* dependency-aware explicit maintenance scheduling through
	``schedule_maintenance_with_dependencies`` so runtime submission-event prerequisites can gate
	resource maintenance with the same token model already used by host-submit and native-queue
	paths
* stale-sweep scheduling and retired-resource collection entry points so long-lived hosts can keep
	resource maintenance and reclamation outside the base engine table

The adapter currently owns one ``AsyncResourceManager`` instance per ABI engine. Resource
maintenance callbacks receive a stable
``zpc_runtime_resource_maintenance_context_v1_t`` describing the resource handle, label, executor,
payload pointer, maintenance kind, epoch, lease count, bytes, and current stop flags. That keeps
the queried extension useful for plugin-style hosts while preserving the internal C++ resource
types as an implementation detail.

The extension now reports minor version ``2``. Minor version ``1`` added
``query_resource_info`` beyond the original mutation-only surface; minor version ``2`` appends a
dependency-aware maintenance scheduling entry point. Like plain host submission, this current
maintenance path accepts runtime submission-event prerequisites but rejects foreign native-signal
tokens because there is no native queue binding on the resource-maintenance path.

Native Queue Extension
----------------------

The ``zpc.runtime.native_queue.v1`` extension is the first deployable ABI bridge for queue or
stream-backed backends.

The current host-validated shape exposes:

* a stable ``zpc_runtime_native_queue_desc_t`` mirroring the queue descriptor fields already used
	by the in-tree native queue adapter
* a stable ``zpc_runtime_native_queue_payload_t`` hook table for queue handle lookup, signal handle
	lookup, synchronization, record, and wait hooks
* a native submit entry point that reuses the existing host task payload for the task body while
	routing execution through ``AsyncNativeQueueExecutor``
* a native wait entry point that consumes a foreign signal token through the same stable payload
	hook table, so signaling is both observable and consumable without widening the base engine
	table

The native queue extension now uses minor version ``1`` to reflect that submissions may also carry
``zpc_runtime_dependency_list_v1_t`` through ``reserved[1]``. On this path:

* ``ZPC_RUNTIME_DEPENDENCY_SUBMISSION_EVENT`` tokens are translated into ``AsyncRuntime``
	prerequisite events
* ``ZPC_RUNTIME_DEPENDENCY_NATIVE_SIGNAL`` tokens are passed to the stable queue-binding wait hook
	before the submitted task body runs

This lets runtime-managed dependency chaining and backend-native pre-wait signaling coexist on one
append-only ABI contract.

This keeps task definition and queue binding separate: host-callable task logic remains on the
existing host-submit payload contract, while backend-native synchronization and signaling stay on
their own queried extension surface.

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
function-table contract, host-submit extension discovery, completed host submission, suspended task
cancellation, validation extension discovery, validation summary or JSON or text export, and a
host-only fake native queue submission and wait-signal path through the concrete ``AsyncRuntime``
adapter. It also validates dependency-list propagation for runtime submission-event prerequisites,
rejection of unsupported native-signal dependencies on plain host submission, mixed runtime or
native prerequisite handling on the native queue path, and the resource-manager extension's
registration, lease acquisition, dirty tracking, epoch advance, explicit maintenance scheduling,
stale sweep, and retirement collection path.

For the broader deployable-boundary rationale, see :doc:`plugin_and_abi_stability`.