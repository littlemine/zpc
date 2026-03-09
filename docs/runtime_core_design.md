# Runtime Core Design

ZPC is moving toward an explicit async control plane that can coordinate host
execution, backend-native queues, validation, and future deployment-facing surfaces
without freezing internal implementation details into the user-facing product
boundary.

## Why This Page Exists

This page defines the shared execution vocabulary that should sit between the
portable foundation and higher-level product surfaces. It is the control-plane page
for the docs set.

## Role Of The Runtime Core

The runtime core is the layer above the portable foundation and below
backend-specific adapters, tooling surfaces, and higher-level applications.

Its job is to provide a shared vocabulary for:

- task submission and lifecycle management
- executor registration and dispatch
- queue or stream endpoint description
- cancellation and suspension-aware execution
- validation-friendly task and backend metadata
- composition with native queue adapters and the stable deployable ABI

This is the layer that should make heterogeneous execution coherent before richer
product surfaces such as CLI, MCP, Python orchestration, or GUI shells are widened.

## Core Concepts

### Executors

Executors represent runtime-owned execution domains such as inline execution,
thread-pool-backed host execution, or future backend-backed dispatch surfaces. They
give the runtime a stable way to name and route work without encoding
backend-specific details into the submission surface.

### Endpoints

Endpoints describe where a submission actually lands. The intended model is
backend-agnostic:

- backend kind
- queue or stream class
- optional device and stream identity
- optional native handles
- human-meaningful labels for tooling and validation

This keeps execution planning transport-friendly while still allowing backend
adapters to bind onto real queues or streams.

### Submission State

Submission state should remain internal to the runtime. The public boundary should
reason about submission identifiers, task descriptors, lifecycle status, and
queried extensions, not internal C++ ownership or scheduler details.

## Scheduler Direction

The runtime layer is also the right home for scheduler and composition work that is
generic across backends.

That includes:

- host-side thread-pool scheduling
- task graph or coroutine-oriented composition
- wake, wait, and cancellation coordination
- dependency chaining before backend-specific execution begins

The important rule is that scheduling policy should stay separable from backend
ownership. A Vulkan queue adapter and a host thread-pool executor should be able to
participate in one control-plane model without forcing the same low-level
implementation strategy.

## Native Queue Relationship

The native queue adapter exists to connect runtime submissions to queue or
stream-backed backends without flattening those backends into the runtime core
itself.

The runtime core should therefore provide:

- the shared submission and dependency vocabulary
- endpoint and descriptor transport
- status tracking and lifecycle control

Queue-specific binding, signaling, and synchronization hooks remain optional,
adapter-owned extensions.

## Validation Relationship

Validation is not a bolt-on feature. The runtime control plane should carry enough
structured metadata that validation, reporting, and regression tooling can reason
about what was submitted, where it ran, and how it completed.

That is why runtime and validation work should remain adjacent:

- validation schemas should understand runtime tasks and backends
- runtime descriptors should stay machine-readable and append-only where possible
- deployable reporting should move through extension surfaces rather than ad hoc
  internal coupling

## Target Ownership

The long-term split implied by this design is:

- `zpc_async_runtime_core` for async runtime, scheduler, endpoint, awaitable,
  native queue, and runtime-side ABI helpers
- `zpc_validation_core` for validation schema, compare, and format layers consumed
  by the runtime, tooling, and upgrade gates

`zpccore` should become a compatibility facade over those narrower layers rather
than the place where control-plane and unrelated feature ownership remain mixed
together.

## Strategic Outcome

An explicit runtime core gives ZPC one coherent place to evolve:

- host and backend-native execution composition
- deployable runtime ABI implementation
- validation and benchmark reporting hooks
- future CLI and MCP integrations

without forcing higher-level products to depend on backend internals or monolithic
build assembly.

## Current Design Consequence

If new execution-facing work cannot be expressed as part of this shared control
plane, it is either too backend-specific and belongs in an adapter layer, or too
product-specific and belongs above the runtime core.

## Related Pages

- [async_runtime_abi.rst](async_runtime_abi.rst) for the current stable deployable
  boundary
- [plugin_and_abi_stability.md](plugin_and_abi_stability.md) for the broader
  binary-compatibility model
- [architecture_and_modularization.md](architecture_and_modularization.md) for the
  proposed target graph around the runtime core
