# Application Layer Design

ZPC is not only a low-level compute library. It is intended to support higher-level
tools and applications built on a stable platform. The application layer should
therefore sit above the modular runtime and backend graph rather than being mixed
into the core.

## Why This Page Exists

This page describes where native applications, editor shells, and shared UI-facing
services belong in the overall architecture. It is the bridge between platform
research and user-facing products.

## Priority Order

The intended order is:

- immediate-mode application shell first
- shared application context and service abstractions next
- retained-mode integration later

ImGui is the first priority because it is well suited to rapid tooling, editor,
profiling, and graph-oriented workflows without forcing a heavy retained-mode
framework decision too early.

## Immediate-Mode First

The first application-layer track should focus on an immediate-mode shell that can
host:

- viewports and render-loop integration
- property inspection and editing
- graph-oriented authoring tools
- validation and profiling overlays
- lightweight task or runtime control panels

This is the most practical way to expose runtime, backend, validation, and future
rendering systems to users without locking the project into a heavyweight
application contract too early.

## Shared Application Responsibilities

Regardless of UI framework, the application layer should own a small stable set of
shared concepts:

- application or scene context
- resource and asset registry
- input and event abstraction
- viewport lifecycle and presentation loop hooks
- property and graph editing support
- integration points for scripting or frontend orchestration

Those services should depend on the modular platform, not on the compatibility
facade targets by accident.

## Retained-Mode Later

Retained-mode integration, potentially through Qt or a similar framework, should be
deferred until the shared application contract is stable.

That sequencing matters because otherwise the retained-mode framework tends to
define the platform shape prematurely. The application contract should be
framework-aware, but not framework-owned.

## Relationship To Rendering And Tooling

The application layer is where runtime, validation, graphics, and frontend
workflows start to meet. It should eventually support:

- runtime inspection and submission tooling
- render and scene inspection
- graph and property authoring
- validation and benchmark result presentation
- editor-oriented frontend integration
- web application shells and WASM-facing UI layers where the platform contracts can
  survive browser sandbox limits

This makes it a major consumer of the modularization work, which is why it should
follow after the foundation and runtime layers are separated.

## Why This Layer Must Stay Above The Platform

If the application layer is allowed to pull graphics, Python, JIT, and backend
decisions back into the core, the modularization effort collapses. The correct
direction is the opposite:

- the platform defines stable lower-layer services
- the application layer composes them into user-facing tools
- specific UI technologies remain replaceable implementation choices

## Current Design Consequence

Application work should sharpen the shared service boundaries above the platform,
not pull graphics, runtime, or frontend choices back into lower-layer ownership.

## Related Pages

- [architecture_and_modularization.md](architecture_and_modularization.md) for the
  lower-layer graph this application layer should consume
- [lightweight_frontend_integration.md](lightweight_frontend_integration.md) for the
  Python or DSL-facing composition path adjacent to application tooling
- [cli_and_gui_interface_exposure.md](cli_and_gui_interface_exposure.md) for the
  shared operator-facing and user-facing interface model above the app layer
- [canary_gameplay_and_tuning.md](canary_gameplay_and_tuning.md) for the scenario
  authoring and tuning flows that should surface through these applications
- [roadmap.md](roadmap.md) for the broader product-surface direction
