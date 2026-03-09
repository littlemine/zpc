# Overview

Zensim is short for Zenus Simulation. It is an open-source C++ library designed for
high-performance physics-based simulation work, built on top of ZPC, Zenus Parallel
Compute. ZPC provides a C++ STL-like interface across multiple compute backends,
including OpenMP and CUDA, plus a set of efficient, customizable data structures.

Today ZPC mainly targets computations within a shared-memory heterogeneous
architecture. The broader direction is to extend that base into a more modular
platform that can support richer runtime, validation, tooling, and application
layers.

Zensim is developed and maintained by Zenus Tech Co. Ltd. for simulation-focused
research and engineering. Contact:
[wxlwxl1993@zju.edu.cn](mailto:wxlwxl1993@zju.edu.cn) or
[zhangshinshin@gmail.com](mailto:zhangshinshin@gmail.com).

This repository is moving quickly. ABI compatibility is not guaranteed from commit
to commit at the moment.

## Repository Browsing

The primary repository-friendly entry points are:

- [README.md](README.md) for Markdown-first browsing.
- [research_topics.md](research_topics.md) for topic-oriented contents.
- [portal.html](portal.html) for a standalone visual landing page.

Legacy generated docs still exist under the remaining `.rst` files, but the current
architecture notes are maintained in Markdown.

## How To Use This Docs Set

Use the docs in layers rather than reading them as one long sequence.

- Start with [research_topics.md](research_topics.md) if you want the whole map.
- Start with [roadmap.md](roadmap.md) if you want strategy and delivery direction.
- Start with [foundation_layer.md](foundation_layer.md) and
  [runtime_core_design.md](runtime_core_design.md) if you want the platform core.
- Start with [rendering_and_visualization.md](rendering_and_visualization.md),
  [physics_and_simulation.md](physics_and_simulation.md), and
  [gameplay_and_mechanics.md](gameplay_and_mechanics.md) if you care more about
  downstream product research than internal layering.

## Current Architecture Direction

The current direction is to turn the assembled product into a modular platform with:

- an explicit portable foundation
- a runtime control plane for async execution and deployment-facing surfaces
- optional backend and graphics modules
- higher-level application and frontend integration layers built on top

The most important design notes are:

- [research_topics.md](research_topics.md) for the full topic index and reading
  paths
- [foundation_layer.md](foundation_layer.md) for the portable always-on substrate
- [runtime_core_design.md](runtime_core_design.md) for the async and execution
  control plane
- [architecture_and_modularization.md](architecture_and_modularization.md) for the
  target graph and dependency direction
- [platform_and_build_profiles.md](platform_and_build_profiles.md) for the intended
  delivery-profile matrix

The major downstream research tracks also include:

- [rendering_and_visualization.md](rendering_and_visualization.md)
- [physics_and_simulation.md](physics_and_simulation.md)
- [gameplay_and_mechanics.md](gameplay_and_mechanics.md)
- [web_runtime_service_interface.md](web_runtime_service_interface.md)
- [cli_and_gui_interface_exposure.md](cli_and_gui_interface_exposure.md)
- [canary_gameplay_and_tuning.md](canary_gameplay_and_tuning.md)

The roadmap remains the strategic entry point, while those design pages carry the
more specific layering and sequencing guidance.
