# Roadmap

ZPC is evolving from a high-performance C++ compute framework into a broader
platform for computation, hardware orchestration, validation, and application
delivery. This roadmap captures the direction so interface and implementation work
can stay aligned.

## Why This Page Exists

This page is the strategic summary for the docs set. It does not replace the more
specific architecture pages; it gives the program-level direction they are meant to
support.

## Platform Direction

The target shape of ZPC is:

- a stable core for computation and hardware resource utilization
- a CLI for local development, automation, packaging, validation, benchmarking, and
  operations
- an MCP surface for IDE and agent integration
- cross-platform reach, including desktop, server, mobile, and web-facing
  deployments where the backend allows it
- explicit web-application and WASM delivery paths built on the same modular
  runtime contracts rather than one-off browser-only APIs
- decoupled interfaces and implementations so backends, schedulers, transports, and
  deployment modes can evolve without breaking user-facing workflows
- a base for higher-level applications instead of only a low-level library

## Design Principles

The next generations of ZPC should follow these principles:

- interface first: user-facing concepts must be stable, documented, and portable
  across backends
- implementation decoupling: transport, scheduling, execution, storage, and tooling
  should be independently replaceable modules
- performance visibility: execution, memory, transfer, and synchronization costs
  should be observable and benchmarkable
- deployability: workflows should scale from local development to packaged services
  and batch execution without requiring source-level integration every time
- progressive accessibility: advanced native backends remain first-class, while CLI,
  MCP, and web or mobile integration make the platform easier to adopt
- scalable development: once interfaces are consolidated, module work should split
  across coordinated agents or workers so backend, runtime, tooling, and validation
  layers can advance in parallel without interface churn

## Major Product Surfaces

### Core Runtime And Resource Layer

The core remains the foundation:

- execution and async runtime abstractions
- storage and memory placement abstractions
- hardware topology and resource scheduling
- backend adapters for host, OpenMP, CUDA, ROCm, SYCL, Vulkan, and future
  transports

### CLI

The CLI should become the standard operational entry point for ZPC:

- project inspection and environment diagnosis
- backend discovery and hardware capability reporting
- validation and benchmark execution
- packaging, deployment, and artifact management
- reproducible performance and regression workflows

### MCP Server

The MCP surface should expose stable tools for developer agents and IDE workflows:

- project and backend introspection
- execution planning and submission
- profiling and validation queries
- benchmark result retrieval
- resource and topology inspection

The MCP contract should stay thinner and more stable than the internal
implementation.

### Validation, Benchmarking, And Self-Upgrade

ZPC should be able to validate itself as it evolves:

- correctness tests across execution backends
- compatibility and migration checks for public interfaces
- benchmark suites for kernels, memory movement, queueing, and end-to-end workloads
- upgrade gates that compare current changes against stored baselines
- machine-readable reports for CLI and MCP consumers

### Higher-Level Applications

ZPC should support application layers built on top of the platform rather than
forcing every consumer to assemble infrastructure from scratch. Examples include
simulation tools, pipeline orchestration services, visualization-oriented compute
tools, and domain-specific authoring applications.

That includes web applications and WASM-oriented frontends where the runtime,
validation, asset, and packaging surfaces can be mapped cleanly into browser-safe
constraints.

## Execution Tracks

### Track 1: Unified Control Plane

Build a common control plane for:

- node, process, thread, device, and queue coordination
- async submission graphs and cancellation
- resource reservation and scheduling
- profiling, telemetry, and error reporting

### Track 2: Backend Adaptation

Backend work should converge on shared contracts rather than backend-specific user
flows:

- compute backends: host, OpenMP, CUDA, ROCm, SYCL
- graphics and heterogeneous queue backends: Vulkan
- transport backends: Asio-based inter-node communication and process orchestration

### Track 3: Tooling Surfaces

Turn core capabilities into product surfaces:

- a shared interface-services layer for runtime control, resources, validation,
  and scenario access
- a CLI with stable commands and machine-readable output
- native GUI tooling and application shells built on the same service model
- an MCP server with stable tool contracts
- packaging and deployment helpers for local, cluster, mobile, and web integration
  paths
- web-facing packaging and browser-runtime integration points that reuse the core
  validation and reporting model
- schema-driven canary scenario workflows for human tuning and evaluation across
  CLI, GUI, and web consumers

### Track 4: Quality Gates

Every core upgrade should be measured and validated:

- backend conformance tests
- microbenchmarks and scenario benchmarks
- upgrade regression dashboards
- baseline comparison and acceptance thresholds

### Track 5: Parallel Development

After interface boundaries are stable enough, development should shift from mostly
serialized work to coordinated parallel work across modules and layers:

- runtime and scheduler internals
- backend adapters and transport bindings
- validation and benchmark pipelines
- CLI, MCP, and Python-facing surfaces
- higher-level application layers

This only works once the shared contracts are explicit enough that multiple agents
or workers can develop against them without forcing repeated interface
renegotiation.

## Suggested Delivery Phases

### Phase 1: Foundation

- finish the unified async and resource control plane
- add first-class validation and benchmark targets
- define stable internal contracts for CLI and MCP layers

The memory subsystem has completed its foundation work: `MemoryBackendRegistry`
decouples the portable core from backend-specific allocator headers and provides
an always-present host backend. See
[memory_backend_registry.md](memory_backend_registry.md).

### Phase 2: Platform Surface

- introduce the shared interface-services layer above runtime and validation
- introduce the first CLI workflows
- introduce the first MCP tools
- expose hardware discovery, validation, and benchmark reporting
- begin the first scenario schema and canary evaluation conventions
- begin coordinated multi-agent or multi-worker development once the interface
  contracts are stable

### Phase 3: Deployment And Reach

- improve packaging and deployment workflows
- formalize server, desktop, mobile, and web integration targets
- stabilize public automation interfaces
- add the first web runtime-service and browser-facing adapters on top of the
  shared service model

### Phase 4: Applications

- build higher-level applications on top of the stabilized platform
- add native GUI and product-facing canary tuning flows on top of shared services
- reuse the same validation and benchmark pipelines for application upgrades
- keep product-facing interfaces decoupled from backend-specific implementation
  churn

## Near-Term Priorities

The most immediate work that best supports this roadmap is:

- complete the async runtime control plane for heterogeneous execution
- add concrete backend adapters beginning with the strongest existing seams
- define the first validation and benchmark schema that both CLI and MCP can
  consume
- define the shared interface-services vocabulary so CLI, GUI, web, and canary
  modules do not fork the runtime-control model
- keep interface boundaries explicit so future CLI, MCP, and applications do not
  depend on backend internals
- consolidate enough runtime and tooling interfaces that work can be distributed
  across multiple agents or workers by module or layer
- extend the deployable runtime ABI through queried extensions for validation
  export first, then native backend submission and signaling, instead of widening
  the base engine table prematurely
- after the first native queue ABI bridge is stable, extend it toward richer
  dependency and signal propagation rather than duplicating backend submission
  concepts in parallel ad hoc APIs
- add one CLI-first canary scenario path before building richer GUI or web tuning
  experiences

## Architecture References

This roadmap stays intentionally strategic. The more specific design notes for the
current foundation-first direction are:

- [architecture_and_modularization.md](architecture_and_modularization.md)
- [implementation_roadmap.md](implementation_roadmap.md)
- [platform_and_build_profiles.md](platform_and_build_profiles.md)
- [application_layer_design.md](application_layer_design.md)
- [web_runtime_service_interface.md](web_runtime_service_interface.md)
- [cli_and_gui_interface_exposure.md](cli_and_gui_interface_exposure.md)
- [canary_gameplay_and_tuning.md](canary_gameplay_and_tuning.md)

Those pages are the working architectural baseline for modularization and platform
surface evolution.

## Current Design Consequence

The roadmap should stay broad, but not vague. Whenever a new idea appears, it needs
to fit one of these product surfaces, delivery tracks, or quality gates rather than
living as an isolated branch-only experiment with no place in the overall plan.
