Roadmap
========

ZPC is evolving from a high-performance C++ compute framework into a broader platform for
computation, hardware orchestration, validation, and application delivery. This roadmap
captures the direction so interface and implementation work can stay aligned.

Platform Direction
------------------

The target shape of ZPC is:

* a stable core for computation and hardware resource utilization
* a CLI for local development, automation, packaging, validation, benchmarking, and ops
* an MCP surface for IDE and agent integration
* cross-platform reach, including desktop, server, mobile, and web-facing deployments where
  the backend allows it
* decoupled interface and implementation so backends, schedulers, transports, and deployment
  modes can evolve without breaking user-facing workflows
* a base for higher-level applications instead of only a low-level library

Design Principles
-----------------

The next generations of ZPC should follow these principles:

* interface first: user-facing concepts must be stable, documented, and portable across backends
* implementation decoupling: transport, scheduling, execution, storage, and tooling should be
  independently replaceable modules
* performance visibility: execution, memory, transfer, and synchronization costs should be
  observable and benchmarkable
* deployability: workflows should scale from local development to packaged services and batch
  execution without requiring source-level integration every time
* progressive accessibility: advanced native backends remain first-class, while CLI, MCP, and
  web or mobile integration make the platform easier to adopt
* scalable development: once interfaces are consolidated, module work should split across
  coordinated agents or workers so backend, runtime, tooling, and validation layers can advance in
  parallel without interface churn

Major Product Surfaces
----------------------

Core Runtime and Resource Layer
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The core remains the foundation:

* execution and async runtime abstractions
* storage and memory placement abstractions
* hardware topology and resource scheduling
* backend adapters for host, OpenMP, CUDA, ROCm, SYCL, Vulkan, and future transports

CLI
^^^

The CLI should become the standard operational entry point for ZPC:

* project inspection and environment diagnosis
* backend discovery and hardware capability reporting
* validation and benchmark execution
* packaging, deployment, and artifact management
* reproducible performance and regression workflows

MCP Server
^^^^^^^^^^

The MCP surface should expose stable tools for developer agents and IDE workflows:

* project and backend introspection
* execution planning and submission
* profiling and validation queries
* benchmark result retrieval
* resource and topology inspection

The MCP contract should stay thinner and more stable than the internal implementation.

Validation, Benchmarking, and Self-Upgrade
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

ZPC should be able to validate itself as it evolves:

* correctness tests across execution backends
* compatibility and migration checks for public interfaces
* benchmark suites for kernels, memory movement, queueing, and end-to-end workloads
* upgrade gates that compare current changes against stored baselines
* machine-readable reports for CLI and MCP consumers

Higher-Level Applications
^^^^^^^^^^^^^^^^^^^^^^^^^

ZPC should support application layers built on top of the platform rather than forcing every
consumer to assemble infrastructure from scratch. Examples include simulation tools, pipeline
orchestration services, visualization-oriented compute tools, and domain-specific authoring
applications.

Execution Tracks
----------------

Track 1: Unified Control Plane
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Build a common control plane for:

* node, process, thread, device, and queue coordination
* async submission graphs and cancellation
* resource reservation and scheduling
* profiling, telemetry, and error reporting

Track 2: Backend Adaptation
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Backend work should converge on shared contracts rather than backend-specific user flows:

* compute backends: host, OpenMP, CUDA, ROCm, SYCL
* graphics and heterogeneous queue backends: Vulkan
* transport backends: Asio-based inter-node communication and process orchestration

Track 3: Tooling Surfaces
^^^^^^^^^^^^^^^^^^^^^^^^^

Turn core capabilities into product surfaces:

* a CLI with stable commands and machine-readable output
* an MCP server with stable tool contracts
* packaging and deployment helpers for local, cluster, mobile, and web integration paths

Track 5: Parallel Development
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

After interface boundaries are stable enough, development should shift from mostly serialized work
to coordinated parallel work across modules and layers:

* runtime and scheduler internals
* backend adapters and transport bindings
* validation and benchmark pipelines
* CLI, MCP, and Python-facing surfaces
* higher-level application layers

This only works once the shared contracts are explicit enough that multiple agents or workers can
develop against them without forcing repeated interface renegotiation.

Track 4: Quality Gates
^^^^^^^^^^^^^^^^^^^^^^

Every core upgrade should be measured and validated:

* backend conformance tests
* microbenchmarks and scenario benchmarks
* upgrade regression dashboards
* baseline comparison and acceptance thresholds

Suggested Delivery Phases
-------------------------

Phase 1: Foundation
^^^^^^^^^^^^^^^^^^^

* finish the unified async and resource control plane
* add first-class validation and benchmark targets
* define stable internal contracts for CLI and MCP layers

Phase 2: Platform Surface
^^^^^^^^^^^^^^^^^^^^^^^^^

* introduce the first CLI workflows
* introduce the first MCP tools
* expose hardware discovery, validation, and benchmark reporting
* begin coordinated multi-agent or multi-worker development once the interface contracts are stable

Phase 3: Deployment and Reach
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

* improve packaging and deployment workflows
* formalize server, desktop, mobile, and web integration targets
* stabilize public automation interfaces

Phase 4: Applications
^^^^^^^^^^^^^^^^^^^^^

* build higher-level applications on top of the stabilized platform
* reuse the same validation and benchmark pipelines for application upgrades
* keep product-facing interfaces decoupled from backend-specific implementation churn

Near-Term Priorities
--------------------

The most immediate work that best supports this roadmap is:

* complete the async runtime control plane for heterogeneous execution
* add concrete backend adapters beginning with the strongest existing seams
* define the first validation and benchmark schema that both CLI and MCP can consume
* keep interface boundaries explicit so future CLI, MCP, and applications do not depend on
  backend internals
* consolidate enough runtime and tooling interfaces that work can be distributed across multiple
  agents or workers by module or layer