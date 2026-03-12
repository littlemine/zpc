# Gameplay Mechanics Implementation Roadmap

This document defines the implementation stages, deliverables, and build targets for
the gameplay mechanics system. It translates research findings into concrete
engineering work inside the ZPC codebase.

## Purpose

This roadmap ensures that implementation proceeds in a disciplined order: foundation
first, then core mechanics, then extension points, then production hardening. Each
stage must produce testable, benchmarkable, and documentable artifacts before the
next stage begins.

## Guiding Constraints

- all gameplay code must build on existing ZPC modules: validation, canary,
  interface services, async runtime
- new general-purpose capabilities missing in ZPC must be added to ZPC itself, not
  worked around in gameplay-layer code
- only project-specific content (class rosters, skill tables, enemy rules, numeric
  formulas) stays outside the framework core
- every implementation stage must have tests, validation artifacts, and benchmark
  coverage before promotion

## Stage 1: Gameplay Core Foundation

### 1.1: Entity And Component Model

Deliverables:

- `GameplayEntity` identity and lifecycle model
- component storage and query interface
- entity creation, destruction, and ownership semantics
- integration with ZPC validation schemas for entity state serialization

Build target: `zpc_gameplay_core`

Dependencies: `zpc_validation_core`

### 1.2: Tag And State System

Deliverables:

- `GameplayTag` type with hierarchical namespace support
- tag container with efficient query operations
- tag-based condition and prerequisite evaluation
- serialization and validation integration

Build target: `zpc_gameplay_core`

Dependencies: `zpc_validation_core`

### 1.3: Event System

Deliverables:

- `GameplayEvent` type with typed payload
- event dispatcher with subscription and priority ordering
- event history buffer for replay and debugging
- integration with ZPC async runtime for deferred event delivery

Build target: `zpc_gameplay_core`

Dependencies: `zpc_async_runtime_core`, `zpc_validation_core`

### 1.4: Data-Driven Mechanics Loading

Deliverables:

- mechanics definition schema using ZPC `ValidationSchema`
- JSON-based mechanics data loading with schema validation
- hot-reload support for mechanics definitions during development
- content reference validation at load time

Build target: `zpc_gameplay_core`

Dependencies: `zpc_validation_core`

### Stage 1 Verification

- unit tests for entity lifecycle, tag queries, event dispatch, and data loading
- validation schema round-trip tests
- first canary scenario: a minimal entity with tags, receiving events, driven by
  loaded data
- benchmark: entity creation and tag query throughput

## Stage 2: Ability And Effect Pipeline

### 2.1: Ability Definition And Scheduling

Deliverables:

- `GameplayAbility` base model with activation, cost, cooldown, and targeting
- ability scheduling and execution pipeline
- ability state machine: inactive, activating, active, cooldown, blocked
- parameter schema for ability tuning

Build target: `zpc_gameplay_abilities`

Dependencies: `zpc_gameplay_core`

### 2.2: Effect System

Deliverables:

- `GameplayEffect` model with instant, duration, periodic, and conditional variants
- effect application, stacking, and removal pipeline
- stat modification chain: base, additive, multiplicative, override
- effect interaction rules: immunity, cancellation, extension

Build target: `zpc_gameplay_abilities`

Dependencies: `zpc_gameplay_core`

### 2.3: Combat Resolution Pipeline

Deliverables:

- damage calculation pipeline with configurable stages
- healing and mitigation resolution
- status effect application and interaction
- combat event emission for telemetry and UI consumption

Build target: `zpc_gameplay_combat`

Dependencies: `zpc_gameplay_abilities`

### Stage 2 Verification

- unit tests for ability lifecycle, effect application, stat modification, and
  combat resolution
- canary scenario: two entities with abilities, effects, and combat interaction
- benchmark: ability activation and effect processing throughput
- validation: combat outcome comparison against expected baselines

## Stage 3: Extension Systems

### 3.1: Inventory And Equipment Hooks

Deliverables:

- inventory container interface with slot and capacity semantics
- equipment interface with stat modification hooks
- item definition schema compatible with mechanics data loading
- integration with effect system for equipment-granted effects

Build target: `zpc_gameplay_extensions`

Dependencies: `zpc_gameplay_abilities`

### 3.2: Progression Hooks

Deliverables:

- experience and level interface
- stat growth and scaling hooks
- skill tree and unlock interface
- progression event emission

Build target: `zpc_gameplay_extensions`

Dependencies: `zpc_gameplay_core`

### 3.3: AI-Facing Interface

Deliverables:

- gameplay state query interface for AI consumers
- action enumeration and scoring interface
- ability and interaction request interface for AI actors
- debug and visualization data export for AI decision inspection

Build target: `zpc_gameplay_ai_interface`

Dependencies: `zpc_gameplay_abilities`

### Stage 3 Verification

- unit tests for inventory operations, progression events, and AI queries
- canary scenario: an entity with inventory, equipment, progression, and AI-driven
  ability usage
- benchmark: AI query throughput with increasing entity counts
- validation: progression and inventory state serialization round-trips

## Stage 4: Production Hardening

### 4.1: Save And Load Boundaries

Deliverables:

- gameplay state snapshot and restore interface
- selective serialization for save-relevant versus transient state
- version migration support for evolved gameplay schemas

Build target: `zpc_gameplay_core` (extension)

Dependencies: `zpc_validation_core`

### 4.2: Determinism And Replay

Deliverables:

- deterministic execution mode for gameplay systems
- input recording and replay interface
- state comparison and divergence detection
- replay-based regression testing support

Build target: `zpc_gameplay_replay`

Dependencies: `zpc_gameplay_core`, `zpc_validation_core`

### 4.3: Performance Optimization

Deliverables:

- entity and component memory layout optimization
- batch processing for bulk entity operations
- system scheduling optimization for gameplay update loops
- profiling integration with ZPC telemetry

Build target: all gameplay targets

Dependencies: `zpc_async_runtime_core`

### 4.4: Telemetry And Balance Analysis

Deliverables:

- gameplay metric collection framework
- balance analysis data export
- statistical aggregation for combat and progression metrics
- integration with validation reporting for balance regression detection

Build target: `zpc_gameplay_telemetry`

Dependencies: `zpc_gameplay_core`, `zpc_validation_core`

### Stage 4 Verification

- save/load round-trip tests with version migration
- replay determinism tests
- performance benchmarks against Stage 1 and Stage 2 baselines
- telemetry data validation tests

## CMake Target Structure

```
zpc_gameplay_core          <- entity, tag, event, data loading
zpc_gameplay_abilities     <- ability, effect, stat modification
zpc_gameplay_combat        <- combat resolution pipeline
zpc_gameplay_extensions    <- inventory, equipment, progression hooks
zpc_gameplay_ai_interface  <- AI-facing query and action interface
zpc_gameplay_replay        <- determinism, replay, regression testing
zpc_gameplay_telemetry     <- metrics, balance analysis, reporting
```

All targets link against `zpc_validation_core` and `zpc_canary_core`. Higher-level
targets link against `zpc_gameplay_core`.

## CLI Integration

Each stage should expose its canary scenarios and benchmarks through the CLI using
the existing `zpc_interface_services` and `LocalCanaryService` infrastructure.

Stage 1: `zpc canary run gameplay-entity-basic`
Stage 2: `zpc canary run gameplay-combat-basic`
Stage 3: `zpc canary run gameplay-progression-basic`
Stage 4: `zpc canary run gameplay-replay-regression`

## Documentation Requirements

Each stage must produce:

- API documentation for new public interfaces
- a section update in this roadmap marking the stage as complete
- updated benchmark baselines in the benchmark plan
- updated milestone status in the milestones document

## Related Documents

- [gameplay_mechanics_agent_prompt.md](gameplay_mechanics_agent_prompt.md)
- [gameplay_mechanics_research_roadmap.md](gameplay_mechanics_research_roadmap.md)
- [gameplay_mechanics_benchmark_plan.md](gameplay_mechanics_benchmark_plan.md)
- [gameplay_mechanics_milestones.md](gameplay_mechanics_milestones.md)
- [gameplay_mechanics_risks_and_operating_loop.md](gameplay_mechanics_risks_and_operating_loop.md)
- [canary_gameplay_and_tuning.md](canary_gameplay_and_tuning.md)
- [implementation_roadmap.md](implementation_roadmap.md)
- [roadmap.md](roadmap.md)
