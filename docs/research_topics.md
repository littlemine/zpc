# Research Topics

This page is the topic-oriented contents view for the current architecture and
platform research. Use it when you want to find the substance of the design work
without guessing file names.

## Quick Orientation

- If you care about platform internals, start with Core Platform.
- If you care about build targets and shipping shapes, go to Target Graph And
  Delivery Profiles.
- If you care about rendering, physics, gameplay, apps, or frontends, go to Product
  Surfaces And Frontends.
- If you care about concrete runtime-reporting contracts, go to Runtime And
  Validation Reference Pages.

## Core Platform

- [foundation_layer.md](foundation_layer.md)
  Portable always-on substrate: compile-safe utilities, memory, threading,
  filesystem, logging, and portability boundaries.
- [runtime_core_design.md](runtime_core_design.md)
  Async control plane: executors, endpoints, scheduler direction, native queue
  integration, and validation-aware runtime ownership.
- [plugin_and_abi_stability.md](plugin_and_abi_stability.md)
  Stable deployable boundary: opaque handles, append-only ABI growth, queried
  extensions, and plugin-compatible evolution rules.

## Target Graph And Delivery Profiles

- [architecture_and_modularization.md](architecture_and_modularization.md)
  Layered target graph: base contract, foundation, runtime and validation,
  backends, domain modules, graphics, frontend surfaces, and compatibility
  facades.
- [platform_and_build_profiles.md](platform_and_build_profiles.md)
  Delivery profiles: minimal, runtime, desktop, mobile, web, console-entry, and
  full-dev expectations.
- [implementation_roadmap.md](implementation_roadmap.md)
  Migration sequence: low-risk extraction order, validation gates, and profile
  enablement milestones.

## Product Surfaces And Frontends

- [application_layer_design.md](application_layer_design.md)
  Native application shell direction: immediate-mode tooling first, shared app
  services next, retained-mode later.
- [rendering_and_visualization.md](rendering_and_visualization.md)
  Rendering research: optional graphics ownership, visualization workflows,
  inspection tooling, and web-facing presentation paths.
- [physics_and_simulation.md](physics_and_simulation.md)
  Simulation research: geometry, particles, sparse structures, solver-facing data,
  heterogeneous execution, and validation-aware simulation delivery.
- [gameplay_and_mechanics.md](gameplay_and_mechanics.md)
  Higher-level mechanics research: authored behaviors, interaction logic, graph or
  scripting layers, and native or web application consumers.
- [lightweight_frontend_integration.md](lightweight_frontend_integration.md)
  Frontend composition strategy: Python, DSL, web-facing, and lightweight package
  families on top of the modular runtime.
- [web_runtime_service_interface.md](web_runtime_service_interface.md)
  Web-facing runtime interaction: browser clients, service sessions, runtime
  capabilities, event streams, and WASM or service-mediated delivery.
- [cli_and_gui_interface_exposure.md](cli_and_gui_interface_exposure.md)
  Operator and user-facing interfaces: shared service model for automation,
  inspection, authoring, and runtime control across CLI and GUI surfaces.
- [canary_gameplay_and_tuning.md](canary_gameplay_and_tuning.md)
  Scenario and tuning layer: canary mechanics, ECS-style setup logic, human-edited
  parameters, and evaluation workflows.
- [roadmap.md](roadmap.md)
  Strategic program view: product surfaces, delivery phases, and how CLI, MCP,
  native apps, and web applications fit together.

## Runtime And Validation Reference Pages

- [async_runtime_abi.rst](async_runtime_abi.rst)
  Current deployable engine ABI details and extension surface.
- [async_backend_profiles.rst](async_backend_profiles.rst)
  Backend profile behavior and async execution context.
- [async_native_queue_adapter.rst](async_native_queue_adapter.rst)
  Native queue bridging concepts and adapter responsibilities.
- [validation_schema.rst](validation_schema.rst)
  Validation result schema and report structure.

## Suggested Reading Paths

- Platform-first path:
  [foundation_layer.md](foundation_layer.md) ->
  [runtime_core_design.md](runtime_core_design.md) ->
  [architecture_and_modularization.md](architecture_and_modularization.md) ->
  [implementation_roadmap.md](implementation_roadmap.md)
- Delivery-first path:
  [roadmap.md](roadmap.md) ->
  [platform_and_build_profiles.md](platform_and_build_profiles.md) ->
  [application_layer_design.md](application_layer_design.md) ->
  [lightweight_frontend_integration.md](lightweight_frontend_integration.md)
- Interface-surface path:
  [plugin_and_abi_stability.md](plugin_and_abi_stability.md) ->
  [web_runtime_service_interface.md](web_runtime_service_interface.md) ->
  [cli_and_gui_interface_exposure.md](cli_and_gui_interface_exposure.md) ->
  [canary_gameplay_and_tuning.md](canary_gameplay_and_tuning.md)
- Product-and-content path:
  [physics_and_simulation.md](physics_and_simulation.md) ->
  [rendering_and_visualization.md](rendering_and_visualization.md) ->
  [gameplay_and_mechanics.md](gameplay_and_mechanics.md) ->
  [application_layer_design.md](application_layer_design.md)
- ABI and deployment path:
  [runtime_core_design.md](runtime_core_design.md) ->
  [plugin_and_abi_stability.md](plugin_and_abi_stability.md) ->
  [async_runtime_abi.rst](async_runtime_abi.rst)

## What This Page Covers

This topic map is intentionally broader than low-level platform architecture. It is
meant to expose the whole current research surface, including:

- platform layering and ABI stability
- delivery profiles and migration order
- rendering and visualization
- physics and simulation
- gameplay, authored mechanics, and application logic
- native, Python, and web-facing frontend composition
- web-runtime service interaction, CLI or GUI exposure, and canary-tuning flows
