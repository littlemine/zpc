# Gameplay Mechanics Milestones

This document defines the concrete milestones for the gameplay mechanics system.
Each milestone has clear acceptance criteria, deliverables, and verification
requirements. Milestones are ordered by dependency and should be completed
sequentially.

## Purpose

Milestones convert the implementation roadmap into discrete checkpoints with
pass/fail acceptance. A milestone is not complete until all its acceptance criteria
are met, all tests pass, all benchmarks have baselines, and documentation is
updated.

## Milestone Format

Each milestone specifies:

- name and description
- prerequisite milestones
- deliverables
- acceptance criteria
- verification method
- status

## Milestones

### M1: Gameplay Entity Foundation

Description: establish the core entity and component model with tag support,
integrated with ZPC validation schemas.

Prerequisites: none (first milestone)

Deliverables:

- `GameplayEntity` type with identity, lifecycle, and ownership
- component storage and query interface
- `GameplayTag` type with hierarchical namespace
- tag container with efficient membership queries
- validation schema integration for entity and tag serialization
- `zpc_gameplay_core` CMake target building and linking against `zpc_validation_core`

Acceptance criteria:

- entity creation, destruction, and component access work correctly
- tag assignment, removal, and hierarchical query work correctly
- entity and tag state round-trips through validation serialization
- unit tests pass for all entity and tag operations
- benchmark baseline established for entity throughput (Category 1)

Verification: `ctest` for unit tests, `zpc canary run gameplay-entity-basic` for
canary scenario.

Status: not started

### M2: Event System

Description: establish the gameplay event system with typed dispatch, subscription,
priority ordering, and history.

Prerequisites: M1

Deliverables:

- `GameplayEvent` type with typed payload
- event dispatcher with subscribe, unsubscribe, and priority ordering
- event history buffer with configurable retention
- integration with ZPC async runtime for deferred delivery
- unit tests and benchmark baselines

Acceptance criteria:

- events dispatch to correct subscribers in priority order
- event history captures and replays correctly
- deferred event delivery works through ZPC async runtime
- unit tests pass for all event operations
- benchmark baseline established for event performance (Category 2)

Verification: `ctest` for unit tests, event dispatch benchmark scenarios.

Status: not started

### M3: Data-Driven Mechanics Loading

Description: establish the data loading pipeline for mechanics definitions using
ZPC validation schemas.

Prerequisites: M1

Deliverables:

- mechanics definition schema using `ValidationSchema`
- JSON-based data loading with schema validation at load time
- content reference validation
- hot-reload support for development iteration
- unit tests for loading, validation, and error reporting

Acceptance criteria:

- valid mechanics data loads correctly
- invalid mechanics data produces clear error messages at load time
- schema changes are detected and reported
- hot-reload updates running mechanics definitions
- unit tests pass for all loading scenarios

Verification: `ctest` for unit tests, data loading validation scenarios.

Status: not started

### M4: Ability System

Description: establish the ability definition, scheduling, and execution pipeline.

Prerequisites: M1, M2, M3

Deliverables:

- `GameplayAbility` model with activation, cost, cooldown, and targeting
- ability state machine: inactive, activating, active, cooldown, blocked
- ability scheduling and execution pipeline
- parameter schema for ability tuning
- data-driven ability definition loading
- unit tests and benchmark baselines

Acceptance criteria:

- abilities activate, execute, and enter cooldown correctly
- cost checking and resource deduction work correctly
- ability state transitions follow the defined state machine
- abilities load from data-driven definitions
- unit tests pass for all ability lifecycle scenarios
- benchmark baseline established for ability throughput (Category 3)

Verification: `ctest` for unit tests, ability benchmark scenarios.

Status: not started

### M5: Effect And Stat Modification System

Description: establish the effect model and stat modification pipeline.

Prerequisites: M1, M4

Deliverables:

- `GameplayEffect` model: instant, duration, periodic, conditional
- effect application, stacking, and removal pipeline
- stat modification chain: base, additive, multiplicative, override
- effect interaction rules: immunity, cancellation, extension
- unit tests and benchmark baselines

Acceptance criteria:

- all effect types apply and resolve correctly
- stacking rules produce expected results
- stat modification chains compute correct final values
- effect interactions (immunity, cancellation) work correctly
- unit tests pass for all effect scenarios
- benchmark baseline established for effect throughput (Category 3)

Verification: `ctest` for unit tests, effect processing benchmark scenarios.

Status: not started

### M6: Combat Resolution Pipeline

Description: establish the combat resolution pipeline with configurable stages.

Prerequisites: M4, M5

Deliverables:

- damage calculation pipeline with configurable stages
- healing and mitigation resolution
- status effect application during combat
- combat event emission for telemetry and UI
- canary scenario with two-entity combat
- unit tests and benchmark baselines

Acceptance criteria:

- damage, healing, and mitigation resolve correctly
- status effects apply during combat as expected
- combat events emit with correct data
- combat outcomes match expected baselines in canary scenario
- unit tests pass for all combat resolution scenarios
- benchmark baseline established for combat throughput (Category 4)

Verification: `ctest` for unit tests, `zpc canary run gameplay-combat-basic`.

Status: not started

### M7: Inventory And Equipment Hooks

Description: establish inventory and equipment interfaces with stat modification
integration.

Prerequisites: M5

Deliverables:

- inventory container interface with slot and capacity semantics
- equipment interface with stat modification hooks
- item definition schema for data-driven loading
- equipment-granted effect integration
- unit tests

Acceptance criteria:

- inventory add, remove, and query operations work correctly
- equipment stat modifications integrate with the stat modification chain
- equipment-granted effects apply and remove correctly
- item definitions load from data
- unit tests pass for all inventory and equipment scenarios

Verification: `ctest` for unit tests.

Status: not started

### M8: Progression Hooks

Description: establish progression interfaces for experience, levels, and unlocks.

Prerequisites: M1, M5

Deliverables:

- experience and level interface
- stat growth and scaling hooks
- skill tree and unlock interface
- progression event emission
- unit tests

Acceptance criteria:

- experience gain and level advancement work correctly
- stat growth applies through the stat modification chain
- skill unlocks gate correctly on prerequisites
- progression events emit with correct data
- unit tests pass for all progression scenarios

Verification: `ctest` for unit tests.

Status: not started

### M9: AI-Facing Interface

Description: establish the interface layer for AI systems to query and act on
gameplay state.

Prerequisites: M4, M6

Deliverables:

- gameplay state query interface for AI consumers
- action enumeration and scoring interface
- ability and interaction request interface
- debug data export for AI decision inspection
- unit tests and benchmark baselines

Acceptance criteria:

- AI can query entity state, available abilities, and valid targets
- AI can request ability activation through the standard pipeline
- debug export provides readable decision data
- unit tests pass for all AI interface scenarios
- benchmark baseline established for AI query throughput (Category 5)

Verification: `ctest` for unit tests, AI query benchmark scenarios.

Status: not started

### M10: Save, Load, And Replay

Description: establish save/load boundaries and replay support for gameplay state.

Prerequisites: M1 through M8

Deliverables:

- gameplay state snapshot and restore interface
- selective serialization for save-relevant state
- version migration support
- deterministic execution mode
- input recording and replay interface
- state comparison and divergence detection
- unit tests

Acceptance criteria:

- full gameplay state saves and restores correctly
- version migration handles schema evolution
- deterministic mode produces identical results for identical inputs
- replay produces identical state evolution
- divergence detection catches non-determinism
- unit tests pass for all save/load and replay scenarios

Verification: `ctest` for unit tests, replay regression scenarios.

Status: not started

### M11: Telemetry And Balance Analysis

Description: establish gameplay metric collection and balance analysis reporting.

Prerequisites: M6

Deliverables:

- gameplay metric collection framework
- balance analysis data export
- statistical aggregation for combat and progression metrics
- integration with validation reporting
- unit tests

Acceptance criteria:

- metrics collect during gameplay simulation
- balance reports export in machine-readable format
- statistical aggregations compute correctly
- validation integration detects balance regressions
- unit tests pass for all telemetry scenarios

Verification: `ctest` for unit tests, balance report validation.

Status: not started

### M12: Production Readiness Gate

Description: confirm that the full gameplay mechanics system meets production
quality standards.

Prerequisites: M1 through M11

Deliverables:

- all milestone tests passing
- all benchmark baselines established and passing
- all canary scenarios registered and passing
- documentation complete for all public interfaces
- no known regressions from baseline comparisons
- merge preparation into master branch

Acceptance criteria:

- full test suite passes
- full benchmark suite passes with all baselines
- all canary scenarios pass
- documentation reviewed and complete
- branch is clean and mergeable

Verification: full CI-equivalent validation run.

Status: not started

## Milestone Dependency Graph

```
M1 (Entity Foundation)
├── M2 (Event System)
├── M3 (Data Loading)
├── M4 (Ability System) ← M1, M2, M3
│   ├── M5 (Effect System) ← M1, M4
│   │   ├── M6 (Combat) ← M4, M5
│   │   │   ├── M9 (AI Interface) ← M4, M6
│   │   │   └── M11 (Telemetry) ← M6
│   │   ├── M7 (Inventory) ← M5
│   │   └── M8 (Progression) ← M1, M5
│   └── M10 (Save/Load/Replay) ← M1..M8
└── M12 (Production Gate) ← M1..M11
```

## Related Documents

- [gameplay_mechanics_agent_prompt.md](gameplay_mechanics_agent_prompt.md)
- [gameplay_mechanics_research_roadmap.md](gameplay_mechanics_research_roadmap.md)
- [gameplay_mechanics_implementation_roadmap.md](gameplay_mechanics_implementation_roadmap.md)
- [gameplay_mechanics_benchmark_plan.md](gameplay_mechanics_benchmark_plan.md)
- [gameplay_mechanics_risks_and_operating_loop.md](gameplay_mechanics_risks_and_operating_loop.md)
- [canary_gameplay_and_tuning.md](canary_gameplay_and_tuning.md)
- [roadmap.md](roadmap.md)
