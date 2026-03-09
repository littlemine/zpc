# Rendering And Visualization

Rendering should be treated as a first-class but optional consumer of the ZPC
platform, not as something fused into the portable core. The research focus here is
how to support visualization, inspection, and rendering workflows without forcing
graphics ownership onto runtime-only or headless builds.

## Why This Topic Matters

If rendering is not placed cleanly, it tends to distort the rest of the system. It
can easily drag graphics SDK assumptions into the core, couple tool UIs to engine
internals, and make headless or web-facing delivery harder than necessary.

## Main Questions

- how graphics ownership should stay isolated from the portable foundation and
  runtime core
- how rendering data paths should interoperate with simulation and geometry data
  without excessive copies
- how editor, tooling, and inspection views should sit above the same modular
  services used by other product surfaces
- how native and web-facing rendering paths should share enough abstractions to
  avoid split-brain architecture

## Research Direction

### Optional Graphics Module

The graphics path should remain modular. A target such as `zpc_graphics_vulkan`
belongs above foundation and runtime layers, not inside them.

### Visualization As A Consumer Layer

Visualization should consume:

- geometry and simulation outputs
- runtime scheduling and validation metadata
- application-layer viewport and asset services

That keeps rendering a product surface instead of a hidden transitive dependency.

### Tooling And Inspection

Rendering is not only final-frame output. The same layer should support:

- scene and geometry inspection
- debug rendering and overlays
- validation and benchmark visualization
- graph-oriented authoring and editor views

### Web And WASM Considerations

Browser-facing visualization should be treated as an explicit delivery case. The
core question is not whether web rendering exists, but how it composes on top of
runtime, asset, and packaging contracts without desktop-only assumptions.

## Current Architectural Consequence

Rendering should stay above the runtime and domain layers as a consumer of them.
That is the only way to support rich visualization where available without turning
graphics ownership into a mandatory cost for every build.

## Relationship To Other Topics

- [application_layer_design.md](application_layer_design.md) for viewport and app
  shell concerns
- [architecture_and_modularization.md](architecture_and_modularization.md) for
  graphics module placement in the target graph
- [physics_and_simulation.md](physics_and_simulation.md) for the data-producing side
  that rendering often consumes
