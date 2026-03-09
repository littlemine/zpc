# Physics And Simulation

Physics and simulation are core downstream reasons for the platform to exist, but
they should not define the platform shape prematurely. The research topic here is
how simulation-facing capabilities should build on top of the modular runtime,
memory, backend, and domain layers.

## Why This Topic Matters

Simulation is the main pressure test for whether the platform layering is real. If
simulation needs force data movement, runtime coordination, validation, geometry,
and product surfaces to cooperate, then weak architectural seams show up here first.

## Main Questions

- how simulation data structures should map onto the platform's portable memory and
  execution model
- how geometry, particles, sparse grids, BVHs, and solver-oriented structures
  should be organized as domain layers instead of leaking across the whole build
- how heterogeneous execution and validation should support simulation workflows
  cleanly
- how simulation outputs should connect to rendering, tooling, CLI, and web-facing
  consumers without each consumer inventing its own data path

## Research Direction

### Domain Modules Above The Platform

Simulation-facing structures and algorithms should live above the foundation and
runtime layers, in explicit domain modules such as:

- geometry
- simulation
- IO
- solver or pipeline-oriented packages as they become conceptually clear

### Data And Execution Alignment

The platform should remain strong at:

- memory placement and migration
- backend-agnostic execution patterns
- validation and benchmark reporting
- runtime coordination across host and accelerator backends

Simulation code should consume those services rather than rebuilding them.

### Validation As A First-Class Requirement

For simulation work, correctness and regression visibility matter as much as raw
throughput. That makes validation schemas, comparison reports, and benchmark
baselines part of the simulation research topic, not a detached tooling afterthought.

### Delivery Surfaces

Simulation should remain usable across:

- native tools and editor workflows
- batch and automation paths
- packaged runtime or ABI-driven delivery
- web-facing and WASM-oriented consumers where the execution model allows it

## Current Architectural Consequence

Simulation should sit above the platform but below many product-facing consumers.
That makes it a central domain layer, not the definition of the entire core.

## Relationship To Other Topics

- [foundation_layer.md](foundation_layer.md) for the portable substrate
- [runtime_core_design.md](runtime_core_design.md) for execution control and runtime
  composition
- [rendering_and_visualization.md](rendering_and_visualization.md) for downstream
  presentation and inspection
- [gameplay_and_mechanics.md](gameplay_and_mechanics.md) for higher-level behavior
  and interaction systems built on top of simulation and application layers
