# Platform And Build Profiles

ZPC needs profile-driven builds that reflect delivery surfaces rather than a single
monolithic desktop-development assumption. The purpose of profiles is to make
dependency expectations, validation scope, and portability rules explicit.

## Why This Page Exists

This page answers a practical question: what build shapes should ZPC be able to
ship and validate, and what assumptions are allowed inside each one?

It is best read after [architecture_and_modularization.md](architecture_and_modularization.md),
because profiles only make sense once the target graph is explicit enough to support
them.

## Profile Set

### `minimal`

Portable core only.

Expected contents:

- compile contract and portable foundation
- optional validation primitives
- optional runtime core where needed for smoke coverage
- always-present host or serial execution baseline

Must exclude heavyweight SDK-driven layers by default.

### `runtime`

Portable core plus runtime control-plane and deployable ABI surfaces.

Expected contents:

- foundation
- validation core
- async runtime core
- stable runtime ABI
- selected non-graphics backend hooks where safe

This is the profile that should support deployable execution-engine work without
forcing editor, graphics, Python, or JIT assembly.

### `desktop`

Full interactive developer or end-user desktop composition.

Expected contents:

- runtime profile
- selected compute backends
- optional graphics module
- optional application and tooling layers
- optional Python or JIT layers

### `mobile`

Runtime-oriented profile for sandboxed or constrained mobile deployments.

Expected contents:

- portable core and runtime layers
- only mobile-safe services and explicitly supported backends
- no mandatory JIT or unrestricted desktop-style filesystem behavior

Graphics and backend selection should remain platform-specific and optional.

### `web`

Browser-facing and WASM-oriented profile.

Expected contents:

- portable core and selected runtime surfaces
- explicit sandbox-safe filesystem, threading, and loading assumptions
- validation and reporting paths that work without desktop-style process control
- optional web application packaging layers on top of the same runtime contracts

This profile should treat web applications as a real delivery target, not just as a
future note attached to desktop or mobile work.

### `console`

Entry profile for vendor-toolchain-driven work.

Expected contents:

- portable core and runtime entry points
- explicit stubs, guards, or vendor-owned integrations for unavailable services
- profile-level hooks for future SDK-backed implementations

This profile should initially be treated as abstraction-entry and preset-entry work,
not as a claim of full console runtime support.

### `full-dev`

Broad local development configuration.

Expected contents:

- assembled compatibility products
- broad backend coverage
- tooling, editor, Python, JIT, validation, and experiments

This should remain a composition of modular targets, not the only meaningful build
shape.

## Platform Matrix

### Windows, Linux, And macOS

These remain the main development platforms and should support the broadest profile
range, from `minimal` through `full-dev`.

### Android, iOS, And iPadOS

These should initially be treated as compile-first targets.

Practical implications:

- no assumption of unrestricted writable local files
- no assumption of unrestricted dynamic loading or JIT support
- thread, queue, and graphics behavior must be capability-driven
- validation may begin with configure and compile coverage before device-runtime
  validation exists

### Switch, Xbox, And PlayStation

These should enter the graph as explicit profile and abstraction targets before
vendor SDK-backed runtime support is available.

That means:

- preset and toolchain entry points
- guarded or deferred features for unavailable runtime services
- no silent leakage of desktop-only assumptions into the portable core

### Web-Facing Or Wasm-Style Targets

These should be treated as explicit compile-first and packaging-first targets. The
same design rules apply: runtime and profile selection must be explicit, and the
portable core must not assume services that browser-hosted deployments cannot
provide.

## Allowed And Forbidden Dependencies

For `minimal` and `mobile` especially, the critical rule is that the project must
be able to configure and compile without requiring:

- CUDA or MUSA SDKs
- LLVM or Clang JIT toolchains
- Python runtime integration
- Vulkan SDKs
- heavy optional tooling such as OpenVDB-driven features

Those layers can remain available, but they must stay optional and profile-scoped.

## Validation Strategy

Profile support should not depend exclusively on having every runtime environment
locally.

The initial validation strategy should therefore include:

- configure and compile coverage for every profile and target graph
- symbol or linkage audits that catch forbidden transitive dependencies
- smoke tests for `minimal` and `runtime` profiles on ordinary developer systems
- richer runtime validation on platforms and devices that are actually available

Compile-first validation is not the final goal, but it is the right first gate for
mobile, console-entry, and other constrained targets.

## Why Profiles Matter

Explicit profiles prevent accidental coupling. They make it clear which layers are
always-on, which are optional, and which cannot be assumed on constrained
platforms.

That clarity is required for:

- credible pay-for-what-you-use builds
- deployable runtime packaging
- future frontend integration
- scalable modular development across multiple layers

## Current Design Consequence

Profiles are not packaging decoration. They are architectural constraints that keep
desktop, mobile, web, and future console-entry work from silently inheriting each
other's assumptions.

## Related Pages

- [foundation_layer.md](foundation_layer.md) for the portable base these profiles
  depend on
- [architecture_and_modularization.md](architecture_and_modularization.md) for the
  target graph behind the profiles
- [implementation_roadmap.md](implementation_roadmap.md) for the recommended
  sequencing to make the profiles real
