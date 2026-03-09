# Canary Gameplay And Tuning

ZPC needs a small, explicit layer for canary gameplay or mechanism setup logic if
humans are expected to tune and evaluate higher-level systems on top of it. The
important point is not to turn ZPC into a game engine core by accident, but to make
scenario logic, parameter tuning, and evaluation workflows first-class where they
matter.

## Why This Page Exists

This page defines a canary-oriented gameplay or mechanics layer for human tuning,
evaluation, and regression checks, implemented on top of ZPC systems rather than in
parallel to them.

## Core Design Goal

The canary layer should provide an easy-to-use interface for authored scenarios,
parameter changes, evaluation, and comparison without forcing users to script raw
runtime calls.

It should make it easy to:

- declare a scenario or mechanic setup
- bind it to underlying simulation, ECS, or runtime-owned systems
- expose editable parameters with ranges and defaults
- run evaluation sessions
- compare current outcomes against expected canary baselines

## Unconstrained Research Space

If this layer is considered without assuming only one product style, there are
multiple useful interpretations of a canary system.

### Interpretation 1: Regression Canary

The scenario exists primarily to detect behavioral or performance drift.

Strengths:

- strongest validation value
- easiest to integrate with CI, baselines, and upgrade gating
- clearest pass or fail semantics

Weaknesses:

- can become too narrow for real tuning or authoring workflows

### Interpretation 2: Tuning Sandbox

The scenario exists primarily so humans can explore parameter changes.

Strengths:

- strong product and design value
- best support for human-in-the-loop iteration
- naturally aligns with GUI and web tooling

Weaknesses:

- easy to lose reproducibility if versioning and reporting are weak

### Interpretation 3: Representative Product Slice

The scenario exists as a small but real vertical slice of gameplay or mechanism
behavior that exercises the platform end to end.

Strengths:

- strongest architectural feedback
- reveals missing seams between runtime, app, simulation, rendering, and tooling
- useful both for validation and for demos

Weaknesses:

- higher scope and risk if attempted too early

For ZPC, the best long-term answer is to combine all three, but the safest first
implementation is regression canary plus limited tuning sandbox.

## Scope

This layer is intentionally higher than the runtime core and higher than the main
simulation or geometry domain layers.

It belongs alongside:

- application-layer tooling
- CLI scenario commands
- GUI tuning panels
- web-facing evaluation tools

It does not belong inside the portable foundation or base runtime ABI table.

## Recommended Concept Model

### Scenario Definition

A canary scenario should be a named, inspectable object with:

- stable identifier
- human-readable label and description
- parameter schema
- referenced systems or mechanic groups
- expected outputs or evaluation metrics

### Parameter Schema

Parameters should not be ad hoc maps of strings. The schema should model:

- name
- type
- unit
- allowed range or enum values
- default value
- tuning intent or notes

That schema is what CLI, GUI, and web UIs should render.

### Evaluation Result

Each run should produce a structured result containing:

- scenario id and version
- parameter values used
- timing and runtime context
- outcome metrics
- canary comparison status
- links to validation or benchmark artifacts if available

### Baseline Model

A canary system also needs a baseline concept.

The baseline may be:

- golden metrics checked into lightweight manifests
- larger stored artifacts on a separate storage branch or service
- environment-specific baselines for backend or platform families
- multiple accepted envelopes rather than a single exact answer

The correct baseline model depends on whether the scenario is deterministic,
stochastic, visual, performance-sensitive, or all of the above.

## Scenario Taxonomy

Different kinds of canary scenarios should be modeled explicitly.

### Deterministic Logic Scenarios

Best for:

- rule validation
- authored mechanic correctness
- exact state comparison

### Statistical Or Performance Scenarios

Best for:

- throughput or latency checks
- noisy systems with expected ranges instead of exact values
- regression envelopes instead of exact equality

### Visual Or Presentation Scenarios

Best for:

- render or simulation inspection
- human review workflows
- screenshot, graph, or dashboard-based comparison

### Authoring Scenarios

Best for:

- exposing editable systems to designers or technical users
- teaching or exploration workflows
- interactive tuning sessions

One canary framework should support all of these, but it should not force them into
one comparison rule.

## ECS-Oriented Implementation Direction

If an ECS-style interface is used, it should serve authored mechanics and
evaluation, not redefine the lower platform.

### ECS Role

An ECS-style layer can help with:

- clean system grouping
- scenario-state composition
- inspectable component data for tuning
- deterministic evaluation ordering where feasible

### ECS Limits

It should not:

- bypass runtime or validation contracts
- define the portable substrate
- become the hidden owner of rendering, simulation, and application boundaries

The ECS layer is a product and authoring convenience, not the architecture root.

## Alternative Structural Models

The canary layer can also be structured in several different ways.

### Model A: Scenario Script Over Existing Systems

The scenario is a thin orchestration layer over pre-existing systems.

Best for:

- low implementation cost
- validating current platform seams

Risk:

- scenario definitions can become scattered and non-uniform

### Model B: Scenario Schema Plus Executor

The scenario is a typed description interpreted by one executor layer.

Best for:

- consistent tooling and serialization
- CLI, GUI, and web parity
- clearer baseline comparison

Risk:

- schema design can become overgeneralized if it chases every future use case

### Model C: Mini Application Slices

Each canary is almost a small standalone app slice.

Best for:

- realistic vertical-slice evaluation
- rich demonstration value

Risk:

- tends to duplicate infrastructure and blur ownership boundaries

For ZPC, Model B is the right long-term target, but Model A may be the most direct
first implementation. That is consistent with the rule to avoid premature
abstraction.

## Interface Surfaces

### CLI Surface

The CLI should support:

- list scenarios
- print parameter schemas
- run a scenario with overrides
- export results
- compare current output against canary baselines

### GUI Surface

The GUI should support:

- property editing for scenario parameters
- live run or replay controls
- graph or system inspection views
- comparison panels against canary expectations
- quick reset to baseline values

### Web Surface

The web surface should support:

- remote tuning sessions
- scenario selection and parameter adjustment
- result dashboards and validation status
- collaborative or shareable evaluation reports where needed

## Human-In-The-Loop Research Questions

If real people are expected to tune these systems, the design has to answer:

- which parameters are safe to expose directly
- which parameters require grouped presets rather than raw numbers
- whether edits apply live, staged, or transactionally
- how much provenance is captured for a result
- whether a user can promote a run into a new accepted baseline

These are product questions, not just schema questions.

## Comparison Strategies

A canary result can be compared in multiple ways.

### Exact Equality

Useful for:

- deterministic authored state
- discrete outputs
- schema validation

### Threshold Or Envelope Comparison

Useful for:

- timing and throughput
- floating-point sensitive systems
- backend-dependent output variation

### Distribution Or Trend Comparison

Useful for:

- stochastic simulations
- repeated-run quality gates
- gameplay or balancing signals where a single number is too weak

### Human Review Assisted Comparison

Useful for:

- visual behavior
- subjective balance or interaction quality
- demos and exploratory tuning

The framework should not pretend all canaries are exact-equality checks.

## Relationship To Validation

Canary logic should lean on the validation stack rather than inventing its own
reporting language.

That means:

- canary results should map onto validation or benchmark-friendly schemas
- regressions should be comparable over time
- reports should be machine-readable for CLI, GUI, and web use

It should be acceptable for canary scenarios to reuse the validation schema while
adding scenario-specific metadata rather than inventing an unrelated reporting
stack.

## Relationship To Simulation And Rendering

The canary layer should sit above domain and presentation layers.

- simulation produces the state evolution and metrics
- rendering or visualization shows the behavior
- the canary layer describes the scenario, parameters, and evaluation contract

That keeps authored mechanics from becoming the hidden definition of simulation or
rendering ownership.

## Recommended Build And Ownership Shape

Representative target direction:

- `zpc_canary_core` for scenario and parameter schema
- `zpc_canary_cli` for command-line interaction
- `zpc_canary_gui` for native app tooling exposure
- `zpc_canary_web` for service or web-app-facing tuning surfaces

Those targets should consume existing runtime, simulation, validation, and app
services rather than rebuilding them.

## Anti-Patterns

The canary layer should explicitly avoid:

- turning every experimental mechanic into part of the platform core
- encoding scenario semantics only in GUI widgets or scripts with no shared schema
- forcing exact deterministic comparison on inherently noisy systems
- making baseline promotion informal or manual-only when results are meant to gate
  upgrades
- hiding authored parameter meaning behind anonymous numeric blobs

## Candidate First Canary Families

The most valuable early canaries are likely:

- one deterministic authored-mechanic scenario
- one performance-sensitive scenario with threshold-based comparison
- one interactive tuning scenario with a small visible parameter surface

That mix would test the platform more honestly than three exact-equality unit-style
scenarios.

## Research Recommendation

The unconstrained research answer is:

- treat canaries as both upgrade gates and human-tuning tools
- begin with direct scenario definitions over existing systems before investing in a
  very general scenario framework
- move toward a schema-plus-executor model once two or three real scenario families
  exist
- keep comparison strategy flexible: exact, threshold, statistical, and human-aided
- make provenance, baseline identity, and parameter meaning first-class from the
  beginning

## Sequencing

The safe order is:

1. stabilize runtime, validation, and app-facing shared services
2. define scenario and parameter schemas
3. add one or two canary scenarios with clear metrics
4. expose those scenarios through CLI first, then GUI and web interfaces

CLI-first is usually the fastest way to force clarity in scenario schema and result
contracts before building richer UIs.

## Current Research Direction

The strongest immediate direction is one direct CLI-driven canary path over existing
systems, with explicit parameter descriptors and validation-backed reports. That is
the fastest way to learn what the canary schema actually needs before building a
larger tuning platform.

## Current Design Consequence

The canary layer should be explicit, schema-driven, and evaluation-oriented. It
should help humans tune systems built on ZPC without smuggling product-specific
gameplay logic back into the platform core.

## Related Pages

- [gameplay_and_mechanics.md](gameplay_and_mechanics.md) for the higher-level
  research framing around mechanics and authored behavior
- [physics_and_simulation.md](physics_and_simulation.md) for the simulation layer a
  canary setup often drives
- [cli_and_gui_interface_exposure.md](cli_and_gui_interface_exposure.md) for how
  human operators should interact with canary scenarios
- [web_runtime_service_interface.md](web_runtime_service_interface.md) for the web
  and service-mediated evaluation path
