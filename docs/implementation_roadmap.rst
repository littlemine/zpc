Implementation Roadmap
======================

This roadmap translates the architecture direction into an implementation sequence that minimizes
breakage while improving modularity, portability, and deployability.

Execution Principle
-------------------

The core rule is to preserve current downstream behavior while making narrower ownership explicit.
Compatibility facades stay in place during the transition. Internal targets become more meaningful
first.

Milestone 1: Extract Foundation And Runtime Core
------------------------------------------------

Primary goal:

* split portable foundation and async or validation ownership out of the current broader core

Target outcome:

* ``zpccore_foundation`` becomes explicit
* ``zpc_async_runtime_core`` becomes explicit
* ``zpc_validation_core`` becomes explicit where the current source layout supports it
* ``zpccore`` remains a compatibility facade over those narrower layers

Rules for this milestone:

* keep public headers and main compatibility targets stable
* avoid forcing test rewrites up front
* prefer CMake ownership cleanup before aggressive source reshuffling

Why first:

* lowest-risk cut
* enables ``minimal`` and ``runtime`` profile work
* provides the substrate needed by backend, ABI, and application layers

Milestone 2: Separate Backend Modules From Product Assembly
-----------------------------------------------------------

Primary goal:

* make host and optional compute backends independently selectable composition units

Target outcome:

* explicit always-present host or serial backend
* clearer OpenMP, CUDA, MUSA, SYCL, and related backend ownership
* backend-specific code owned by backend modules instead of by the assembled product
* ``zpc`` preserved as a compatibility assembly target

Rules for this milestone:

* preserve behavior when a backend is enabled
* do not break header compatibility
* avoid hidden cross-backend coupling

Why second:

* unlocks meaningful profile-based builds
* makes downstream packaging narrower
* reduces the chance that backend-specific work leaks into runtime-only or constrained builds

Milestone 3: Extract Graphics And Formalize Profiles
----------------------------------------------------

Primary goal:

* move graphics ownership out of the portable core and make delivery profiles concrete

Target outcome:

* explicit graphics module such as ``zpc_graphics_vulkan``
* clear headless and non-graphics build path
* runtime-facing ABI and profile wiring aligned with modular targets
* ``zpc`` redefined as an assembled desktop or full product rather than the universal downstream
  dependency

Rules for this milestone:

* keep headless builds first-class
* keep base ABI narrow and extension-driven
* do not force graphics ownership into runtime-only or constrained profiles

Validation Gates
----------------

Each milestone should be considered complete only when all relevant gates are satisfied:

* ``minimal`` configures and builds without heavyweight optional dependencies
* runtime and ABI smoke paths build without graphics or Python or JIT layers
* existing async-runtime, ABI, and validation tests still pass in normal desktop development
  profiles
* new targets do not introduce unwanted transitive dependencies into narrower builds

Migration Safety Rules
----------------------

The migration should keep a few rules explicit:

* prefer additive facade targets over abrupt renames
* move ownership only after the destination layer is conceptually clear
* when in doubt, preserve current downstream behavior and narrow internal ownership first
* do not widen the stable deployable ABI merely to mirror internal refactors

What This Enables Later
-----------------------

Once these milestones land, the project is in a better position to support:

* mobile and constrained-platform profile work
* GUI and application-layer composition
* lightweight frontend packaging such as ``zpc::lite``-style integration
* cleaner CLI, MCP, and validation-facing surfaces
* broader feature work in rendering, geometry, simulation, and tooling without re-entangling the
  base build

Related Pages
-------------

See also:

* :doc:`architecture_and_modularization` for the target graph this roadmap is implementing
* :doc:`platform_and_build_profiles` for the profile matrix the milestones are intended to unlock
* :doc:`application_layer_design` for one of the higher-level consumers that should follow after
  the lower layers are stabilized