# Implementation Roadmap

This roadmap translates the architecture direction into an implementation sequence
that minimizes breakage while improving modularity, portability, and deployability.

## Why This Page Exists

The architecture pages describe the target state. This page describes the safest
order to get there without breaking current downstream use.

It is best read after [architecture_and_modularization.md](architecture_and_modularization.md)
and [platform_and_build_profiles.md](platform_and_build_profiles.md).

## Execution Principle

The core rule is to preserve current downstream behavior while making narrower
ownership explicit. Compatibility facades stay in place during the transition.
Internal targets become more meaningful first.

## Milestone 1: Extract Foundation And Runtime Core

Primary goal:

- split portable foundation and async or validation ownership out of the current
  broader core

Target outcome:

- `zpccore_foundation` becomes explicit
- `zpc_async_runtime_core` becomes explicit
- `zpc_validation_core` becomes explicit where the current source layout supports
  it
- `zpccore` remains a compatibility facade over those narrower layers

Rules for this milestone:

- keep public headers and main compatibility targets stable
- avoid forcing test rewrites up front
- prefer CMake ownership cleanup before aggressive source reshuffling

Why first:

- lowest-risk cut
- enables `minimal` and `runtime` profile work
- provides the substrate needed by backend, ABI, and application layers

## Milestone 2: Separate Backend Modules From Product Assembly

Primary goal:

- make host and optional compute backends independently selectable composition
  units

Target outcome:

- explicit always-present host or serial backend
- clearer OpenMP, CUDA, MUSA, SYCL, and related backend ownership
- backend-specific code owned by backend modules instead of by the assembled
  product
- `zpc` preserved as a compatibility assembly target

Progress:

- The memory subsystem has completed this milestone for allocators and memory
  operations. `MemoryBackendRegistry` provides an always-present host backend,
  `Resource.h` no longer includes backend-specific allocator headers, and device
  backends register additively at link time. See
  [memory_backend_registry.md](memory_backend_registry.md).

Rules for this milestone:

- preserve behavior when a backend is enabled
- do not break header compatibility
- avoid hidden cross-backend coupling

Why second:

- unlocks meaningful profile-based builds
- makes downstream packaging narrower
- reduces the chance that backend-specific work leaks into runtime-only or
  constrained builds

## Milestone 3: Extract Graphics And Formalize Profiles

Primary goal:

- move graphics ownership out of the portable core and make delivery profiles
  concrete

Target outcome:

- explicit graphics module such as `zpc_graphics_vulkan`
- clear headless and non-graphics build path
- runtime-facing ABI and profile wiring aligned with modular targets
- `zpc` redefined as an assembled desktop or full product rather than the
  universal downstream dependency

Rules for this milestone:

- keep headless builds first-class
- keep base ABI narrow and extension-driven
- do not force graphics ownership into runtime-only or constrained profiles

Why third:

- locks in the profile matrix before higher-level surfaces are added
- prevents desktop and graphics assumptions from leaking upward into every
  interface or application layer

## Milestone 4: Introduce Shared Interface Services

Primary goal:

- define one shared service layer for runtime control, resource inspection,
  validation reporting, and scenario access

Target outcome:

- explicit interface-services target such as `zpc_interface_services`
- shared session and capability model above the runtime ABI
- shared validation and benchmark retrieval path for automation and UI consumers
- scenario and parameter-schema conventions ready for CLI, GUI, and web adapters

Rules for this milestone:

- do not let CLI, GUI, or web layers each invent their own runtime-control API
- keep transport and presentation concerns above the shared service model
- preserve ABI discipline by extending through queried contracts where needed

Why fourth:

- CLI, GUI, web, and canary surfaces all need the same service vocabulary
- this is the narrowest place to stop interface drift before product layers appear
- it turns the current runtime and validation work into reusable user-facing
  contracts

## Milestone 5: Add CLI, GUI, And Web Adapters

Primary goal:

- expose the shared interface-services layer through concrete operator-facing and
  user-facing surfaces

Target outcome:

- first CLI command groups for runtime, validation, benchmark, resource, and
  scenario workflows
- native GUI shell consuming the same service contracts for inspection and tuning
- web runtime or service adapter that reuses session, capability, and reporting
  concepts instead of inventing browser-only APIs

Rules for this milestone:

- CLI output must stay machine-readable and automation-safe
- GUI and web layers must stay above interface services instead of depending on
  backend internals
- native and browser-facing delivery should differ by transport and packaging, not
  by core semantic model

Why fifth:

- product surfaces should only appear after the shared service layer is stable
- once the first adapter exists, the others become mapping work rather than API
  redesign

## Milestone 6: Add Canary Scenarios And Human Tuning Flows

Primary goal:

- formalize schema-driven canary scenarios for human tuning, evaluation, and
  regression checks

Target outcome:

- explicit canary core target such as `zpc_canary_core`
- scenario identifiers, parameter schemas, defaults, ranges, and evaluation
  metrics defined in shared form
- CLI, GUI, and web exposure for running and comparing canary scenarios
- validation-aligned canary reporting rather than a separate ad hoc result format

Rules for this milestone:

- keep canary logic above runtime, simulation, and validation ownership
- do not smuggle product-specific mechanics back into the portable core
- prefer CLI-first scenario exposure before richer GUI or web tooling

Why sixth:

- canary workflows depend on runtime, validation, and interface services already
  being stable
- this keeps the tuning layer explicit instead of letting it emerge as scattered
  test-only or editor-only logic

## Validation Gates

Each milestone should be considered complete only when all relevant gates are
satisfied:

- `minimal` configures and builds without heavyweight optional dependencies
- runtime and ABI smoke paths build without graphics or Python or JIT layers
- existing async-runtime, ABI, and validation tests still pass in normal desktop
  development profiles
- new targets do not introduce unwanted transitive dependencies into narrower
  builds
- shared interface-services contracts remain usable from CLI, GUI, and web adapters
  without semantic drift
- canary scenario schemas and reports remain machine-readable and comparable over
  time

## Migration Safety Rules

The migration should keep a few rules explicit:

- prefer additive facade targets over abrupt renames
- move ownership only after the destination layer is conceptually clear
- when in doubt, preserve current downstream behavior and narrow internal ownership
  first
- do not widen the stable deployable ABI merely to mirror internal refactors

## What This Enables Later

Once these milestones land, the project is in a better position to support:

- mobile and constrained-platform profile work
- GUI and application-layer composition
- browser-facing runtime services and thin web application shells
- lightweight frontend packaging such as `zpc::lite`-style integration
- cleaner CLI, GUI, web, MCP, and validation-facing surfaces
- explicit canary scenario and tuning workflows for human evaluation
- broader feature work in rendering, geometry, simulation, and tooling without
  re-entangling the base build

## Current Design Consequence

The migration order is itself part of the design. If ownership moves in the wrong
sequence, compatibility facades harden around the wrong seams and future modular
work gets harder, not easier.

## Related Pages

- [architecture_and_modularization.md](architecture_and_modularization.md) for the
  target graph this roadmap is implementing
- [platform_and_build_profiles.md](platform_and_build_profiles.md) for the profile
  matrix the milestones are intended to unlock
- [application_layer_design.md](application_layer_design.md) for one of the
  higher-level consumers that should follow after the lower layers are stabilized
- [web_runtime_service_interface.md](web_runtime_service_interface.md) for the
  service and browser-facing interface model these milestones should enable
- [canary_gameplay_and_tuning.md](canary_gameplay_and_tuning.md) for the scenario
  and tuning layer that should follow after interface services are explicit
