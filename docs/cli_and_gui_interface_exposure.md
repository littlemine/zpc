# CLI And GUI Interface Exposure

ZPC needs explicit operator-facing and human-facing product surfaces. A CLI and a
GUI are not just two skins over the same code; they are two different interface
styles that should sit on top of shared runtime, validation, resource, and scenario
services.

## Why This Page Exists

This page defines how CLI and GUI exposure should interact with a ZPC-based app or
runtime without duplicating ownership or inventing separate execution models.

## Core Design Goal

The CLI and GUI should be peers built on shared lower-layer services.

That means:

- both surfaces consume the same runtime and validation contracts
- neither surface should own backend-specific behavior directly
- automation and human interaction both remain first-class
- GUI convenience must not replace inspectable CLI and machine-readable output

## Interface Split

### CLI Role

The CLI is the operational and automation surface.

It should excel at:

- repeatable workflows
- scripting and CI integration
- environment and capability diagnosis
- scenario launch and batch evaluation
- validation or benchmark export in deterministic formats

### GUI Role

The GUI is the interactive inspection and authoring surface.

It should excel at:

- visualization and state inspection
- graph and property editing
- interactive scenario configuration
- runtime monitoring panels
- tuning and exploratory evaluation

Both roles are necessary. One should not replace the other.

## Unconstrained Research Space

If the interface surface were designed from first principles rather than inherited
from a single app style, there are several viable product models.

### Model 1: CLI-First Platform

The CLI is the primary product surface and the GUI is optional.

Strengths:

- easiest automation story
- strongest reproducibility and testability
- tends to force explicit contracts early

Weaknesses:

- weaker discovery for interactive tuning and inspection
- user-facing workflows often become less approachable

### Model 2: GUI-First Tooling Platform

The GUI drives most workflows and the CLI exists for automation or export.

Strengths:

- best for authoring, inspection, and teaching
- easiest to expose complex state visually

Weaknesses:

- high risk of hidden behavior and weak automation
- business logic often leaks into widget code

### Model 3: Service-First With Multiple Adapters

CLI, GUI, web, and MCP all sit over the same service vocabulary.

Strengths:

- best long-term maintainability
- strongest consistency across surfaces
- easier to distribute development across multiple product adapters

Weaknesses:

- requires real discipline on contract design
- more upfront design work than a single CLI or GUI shortcut

For ZPC, the service-first model is clearly the best fit. The real question is how
strictly to preserve parity between adapters.

## Shared Service Boundary

The CLI and GUI should both operate through shared service contracts such as:

- runtime control
- resource inspection and maintenance
- scenario or canary configuration
- validation and benchmark retrieval
- artifact and package lifecycle

That shared service boundary may be in-process for native apps or service-mediated
for remote and web-facing deployments.

## CLI Design Requirements

### Stable Command Groups

The CLI should converge on a few durable groups:

- `runtime` for capability inspection and lifecycle control
- `resource` for listing, inspecting, touching, and maintaining resources
- `validation` for publishing, comparing, exporting, and gating results
- `benchmark` for scenario execution and baseline management
- `scenario` for canary or authored setup management
- `package` for deployable and frontend-facing artifact workflows

### Output Discipline

The CLI must support:

- human-readable summaries
- deterministic JSON or structured text output
- stable exit codes for automation
- explicit error categories instead of ambiguous console logs

### Batch And Canary Workflows

The CLI is the natural place for:

- replaying known scenarios
- running canary mechanics or balance checks
- comparing a current run against stored expectations
- exporting results for GUI or web consumption

### CLI Research Direction

The strongest CLI design is not a Unix-only text interface and not a shell around
random internal APIs. It is a typed operational surface.

That suggests:

- stable noun-verb groupings rather than growth by ad hoc commands
- output optimized for both humans and machines
- explicit identifiers for sessions, scenarios, artifacts, and runs
- idempotent inspection commands where practical

The CLI should also be treated as the first place where new service semantics are
proven before they are wrapped in richer UI.

## GUI Design Requirements

### Shared App Services

The GUI should build on shared services for:

- viewport and render-loop management
- property and graph editing
- runtime state inspection
- validation and benchmark presentation
- scenario authoring and canary tuning

### Interaction Model

The GUI should support:

- inspect current runtime state
- edit parameters with schema-aware controls
- launch or stop evaluations
- compare current output against canary expectations
- surface validation regressions without forcing raw log reading

### Native And Web GUI Variants

The GUI contract should allow:

- a native immediate-mode tooling shell
- a richer retained-mode shell later if justified
- web-facing UIs that consume the same service model where practical

That requires the GUI data model to stay above the platform core.

### GUI Research Direction

The main GUI design question is not immediate mode versus retained mode by itself.
It is whether the GUI is:

- a direct view over live runtime state
- an editor for durable authored state
- or a hybrid product mixing authored and live execution state

The hybrid model is the most realistic, but it requires sharper distinctions:

- authored data should be versionable and saveable
- live runtime state should be observable and disposable
- validation and benchmark output should be presented as typed results, not just
  logs or transient overlays

## Parity And Non-Parity Rules

CLI and GUI should not be identical, but they should agree on core semantics.

### Semantic Parity Required

- session and scenario identifiers
- parameter schemas and defaulting rules
- validation and comparison status
- resource ownership and lifecycle terms
- artifact naming and packaging vocabulary

### Interaction Parity Not Required

- GUI can show richer visualization and exploratory navigation
- CLI can expose more direct scripting and batch-oriented composition
- GUI can stage edits interactively before apply
- CLI can emphasize reproducible command history and explicit overrides

This is the right balance: same model, different ergonomics.

## Candidate Surface Boundaries

The user-facing surfaces can be cut in several different ways.

### Boundary A: Thin Adapters Over Shared Service Calls

The adapters mostly translate input and output.

Best for:

- consistency
- maintainability
- early versions

Risk:

- adapters can feel too generic if higher-level workflows are not modeled well

### Boundary B: Shared Service Core Plus Workflow Helpers

The shared layer handles domain objects and adapters add workflow-specific helper
operations.

Best for:

- practical product ergonomics
- complex authoring or packaging flows

Risk:

- helper layers can silently fork semantics if not kept disciplined

ZPC should probably use Boundary B, but only after the shared service core exists
and the helpers are clearly expressed as product workflows rather than replacement
runtime APIs.

## Anti-Patterns

The CLI and GUI work should explicitly avoid:

- burying real behavior in GUI-only logic with no machine-readable equivalent
- treating the CLI as a debugging fallback instead of a first-class operational
  product surface
- adding command groups that directly mirror internal C++ classes rather than user
  concepts
- making the GUI a stateful authority while the CLI remains stateless and
  semantically different
- using logs as the main cross-surface contract instead of typed results and
  identifiers

## Candidate First Products

The highest-value early surfaces are likely:

- a runtime inspection CLI with validation and comparison export
- a GUI validation and benchmark viewer
- a scenario editor or runner for canary-style evaluation
- a resource-inspection panel with matching CLI inspection commands

## Research Recommendation

The unconstrained research answer is:

- design one typed service vocabulary first
- prove new semantics through CLI before adding rich GUI flows
- build the first GUI around inspection, validation presentation, and parameter
  editing rather than broad app-framework ambition
- preserve semantic parity across CLI, GUI, web, and MCP while accepting that the
  interaction style should differ

## Recommended Architectural Shape

### Layer 1: Runtime And Validation Core

Lower-layer execution, resources, and validation remain in the shared platform.

### Layer 2: Interface Services

A narrow interface-services layer exposes:

- scenario list and parameter schema
- runtime state queries
- validation summaries and reports
- benchmark and canary results
- resource management operations

### Layer 3: CLI And GUI Adapters

The CLI and GUI each map those services into their own interaction style.

This prevents business logic from being duplicated across command handlers and UI
widgets.

## Human Tuning Implications

If people are expected to tune systems, then both CLI and GUI must be able to work
with:

- named scenarios
- typed parameter schemas
- defaults and ranges
- canary expectations
- comparison against previous runs

Without that layer, both interfaces become thin wrappers over raw engine calls and
are much harder to use reliably.

## Current Design Consequence

CLI and GUI exposure should be designed as parallel consumers of the same service
contracts. The CLI carries automation discipline; the GUI carries interactive
inspection and tuning. Neither should be the hidden implementation detail of the
other.

## Current Research Direction

The strongest development path is service-first, CLI-proven, GUI-second. That is
not because GUI matters less, but because CLI is usually the fastest way to expose
semantic holes in sessions, identifiers, parameters, and result contracts.

## Related Pages

- [application_layer_design.md](application_layer_design.md) for native application
  shell placement
- [web_runtime_service_interface.md](web_runtime_service_interface.md) for the
  browser and service-mediated variant of the same interface problem
- [gameplay_and_mechanics.md](gameplay_and_mechanics.md) for scenario and tuning
  workflows
- [roadmap.md](roadmap.md) for how CLI, GUI, and MCP fit into the broader product
  direction
