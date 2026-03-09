# Foundation Layer

The next durable step for ZPC is not to widen the top-level product surface. It is
to make the portable always-on substrate explicit, narrow, and safe to consume
without accidentally pulling in graphics, JIT, Python, or vendor SDK dependencies.

## Why This Page Exists

This page defines what must remain true of the lowest always-on layer in the
system. It is the portability and dependency-discipline page for the rest of the
architecture.

## Purpose

The foundation layer is the part of ZPC that should remain available across all
meaningful build profiles, including constrained environments.

It should cover:

- low-level type and compile contracts
- math and utility primitives
- memory and resource basics
- concurrency primitives and managed thread support
- filesystem and logging plumbing after platform-safe abstraction
- platform detection and portability boundaries

This layer should be strong enough to support higher runtime, backend, and
application layers while staying narrow enough that `minimal` and `runtime`
profiles can build without heavyweight toolchains.

## Design Rules

The portable foundation should obey a small set of hard rules:

- no dependency on Vulkan, CUDA, MUSA, SYCL, Python, LLVM, or JIT-specific SDKs
- no dependency on editor or application frameworks
- no assumption that executable paths, writable process-local files, or
  unrestricted dynamic loading are always available
- no platform-specific behavior exposed as the default public contract when a
  narrower abstraction can carry the same intent

The design target is a foundation that can compile cleanly on desktop, server, and
constrained profiles even when richer backends are disabled.

## Likely Ownership

The current codebase already contains much of this material. The main requirement is
to assign it clear target ownership.

The intended split is:

- `zpcbase` for header aggregation, compile features, and stable low-level compile
  contracts
- `zpccore_foundation` for compiled portable ownership

That compiled ownership should eventually include the platform-safe parts of:

- memory and allocator infrastructure
- common type and object utilities
- concurrency primitives and managed-thread support
- logging sinks and logging policy adapters
- filesystem context services
- general non-backend utility code

## Portability Boundaries

Several current seams need to be hardened before ZPC can claim a credible portable
minimal build.

### Platform Markers

Platform detection currently reflects a mostly desktop-centric world. The foundation
layer should grow explicit markers for mobile and constrained targets so higher
layers can disable or redirect behavior intentionally rather than through scattered
ad hoc checks.

### Filesystem Context

Executable-path and module-path assumptions do not translate cleanly to sandboxed
mobile targets, some console environments, or service-style deployments. Filesystem
helpers should move toward a context-driven abstraction where host applications or
embedding environments can provide the right storage roots and lookup rules.

### Logging Sinks

The current file-oriented logging behavior is useful for desktop development but
cannot be treated as the universal default. The foundation layer should define
pluggable sinks so desktop, mobile, headless service, and embedded deployments can
each select an appropriate logging transport.

### Dynamic Loading And Threading

JIT and plugin-oriented loading must remain optional, and unrestricted thread
creation must not be assumed to be available everywhere. Both behaviors should sit
behind explicit capability or policy boundaries rather than leaking into the
portable core.

## Why This Comes First

This foundation split is the prerequisite for most other strategic goals:

- the async runtime and stable ABI need a narrow portable substrate
- backend modules should compose on top of an explicit core instead of a monolith
- mobile and constrained profiles need a way to exclude heavyweight or unavailable
  services
- GUI, rendering, and Python-facing integrations should consume the platform rather
  than define it

In practical terms, foundation work reduces future churn. It makes every later layer
easier to compose, test, and ship.

## Current Design Consequence

Any capability that cannot justify living inside a portable, dependency-light,
profile-friendly substrate belongs above this layer. That rule is what keeps the
core usable across desktop, mobile, web, and future constrained targets.

## Related Pages

- [runtime_core_design.md](runtime_core_design.md) for the async control-plane layer
  built on top of the foundation
- [architecture_and_modularization.md](architecture_and_modularization.md) for the
  proposed target graph and dependency direction
- [platform_and_build_profiles.md](platform_and_build_profiles.md) for the
  delivery-profile matrix that depends on a portable foundation
