# Gameplay Mechanics Benchmark Plan

This document defines the benchmark strategy, scenarios, metrics, baseline
management, and reporting conventions for the gameplay mechanics system. Benchmarks
are first-class deliverables, not afterthoughts.

## Purpose

Every gameplay mechanics stage must be benchmarkable. This plan ensures that
performance characteristics are measured, tracked, compared, and gated rather than
left to subjective judgment.

## Benchmark Principles

- benchmarks must be reproducible: same inputs produce same measurements within
  acceptable noise margins
- benchmarks must be automated: CLI-runnable with no manual setup required
- benchmarks must produce machine-readable output compatible with ZPC validation
  schemas
- benchmarks must have baselines: new results are always compared against stored
  reference values
- benchmarks must cover both microbenchmarks and scenario-level workloads

## Infrastructure

### ZPC Validation Stack

All benchmark results must be serialized using `ValidationFormat` and persisted
using `ValidationPersistence`. Comparison against baselines must use
`ValidationCompare` with appropriate tolerance strategies.

### ZPC Canary Infrastructure

Benchmark scenarios must be registered as `CanaryScenario` instances through
`LocalCanaryService` so they are discoverable and runnable through the CLI and
future GUI and web surfaces.

### Benchmark Harness

The gameplay benchmark harness should provide:

- warmup and measurement phase separation
- configurable iteration counts
- statistical aggregation: mean, median, p95, p99, min, max, stddev
- result emission in validation-compatible format
- baseline loading and comparison
- pass/fail determination based on threshold or envelope rules

## Benchmark Categories

### Category 1: Entity Throughput

Measures the raw performance of entity and component operations.

Scenarios:

- entity creation throughput: create N entities with M components, measure wall
  time and allocation count
- entity destruction throughput: destroy N entities, measure cleanup cost
- component access throughput: iterate N entities accessing K components, measure
  per-entity cost
- tag query throughput: query N entities for tag membership, measure per-query cost

Parameters:

- N: entity count (100, 1000, 10000, 100000)
- M: component count per entity (1, 5, 10, 20)
- K: accessed component count (1, 3, 5)

Target metrics:

- wall time (microseconds)
- throughput (entities per second)
- memory usage (bytes)
- allocation count

### Category 2: Event System Performance

Measures event dispatch and delivery throughput.

Scenarios:

- event dispatch throughput: emit N events with M subscribers, measure per-event
  cost
- event history throughput: emit N events and record history, measure overhead
  versus no-history baseline
- event filtering throughput: emit N events with conditional subscriber filters,
  measure filter evaluation cost

Parameters:

- N: event count (100, 1000, 10000)
- M: subscriber count (1, 10, 50, 100)

Target metrics:

- dispatch latency (nanoseconds per event)
- throughput (events per second)
- memory overhead (bytes per event in history)

### Category 3: Ability And Effect Pipeline

Measures the cost of ability execution and effect processing.

Scenarios:

- ability activation throughput: activate N abilities simultaneously, measure
  scheduling and state-machine cost
- effect application throughput: apply N effects to M entities, measure per-effect
  cost
- stat modification throughput: compute final stats for N entities with M stacked
  modifiers, measure chain evaluation cost
- cooldown management throughput: advance N cooldown timers, measure per-timer cost

Parameters:

- N: operation count (100, 1000, 10000)
- M: target or modifier count (1, 5, 20)

Target metrics:

- wall time (microseconds)
- per-operation cost (nanoseconds)
- memory allocation during processing

### Category 4: Combat Resolution

Measures the cost of full combat resolution pipelines.

Scenarios:

- single combat exchange: one attacker, one defender, full damage pipeline,
  measure total resolution cost
- batch combat resolution: N simultaneous exchanges, measure scaling behavior
- complex combat resolution: N exchanges with status effects, mitigation,
  healing, and event emission

Parameters:

- N: exchange count (10, 100, 1000)

Target metrics:

- per-exchange resolution cost (microseconds)
- event emission count and cost
- memory allocation during resolution

### Category 5: Scenario-Level Workloads

Measures realistic gameplay frame budgets.

Scenarios:

- idle world tick: N entities with full gameplay state, no actions, measure base
  cost of state maintenance
- active combat tick: N entities with M active abilities and effects, measure full
  frame cost
- mixed workload tick: N entities with varied activity levels simulating realistic
  distribution

Parameters:

- N: entity count (100, 1000, 5000)
- M: active ability count per entity (0, 1, 3)
- frame budget target: 16.67ms (60fps), 8.33ms (120fps)

Target metrics:

- total frame time (milliseconds)
- per-entity cost (microseconds)
- system scheduling overhead
- percentage of frame budget consumed

### Category 6: Serialization And Persistence

Measures the cost of gameplay state serialization.

Scenarios:

- entity state snapshot: serialize N entities with full gameplay state, measure
  serialization throughput
- save file generation: serialize full world state, measure total cost and output
  size
- state comparison: compare two snapshots of N entities, measure diff cost

Parameters:

- N: entity count (100, 1000, 10000)

Target metrics:

- serialization throughput (entities per second)
- output size (bytes per entity)
- comparison throughput (entities per second)

## Baseline Management

### Baseline Storage

Baselines are stored as validation artifacts alongside the benchmark code. Each
baseline contains:

- scenario identifier and version
- parameter values
- platform and build configuration
- timestamp
- metric values with statistical summaries

### Baseline Promotion

A new baseline is promoted when:

- all benchmark scenarios pass their current thresholds
- the change is intentional and documented
- the previous baseline is archived for historical comparison

### Threshold Strategy

Different benchmark categories use different comparison strategies:

- entity throughput: envelope comparison with 10% tolerance
- event performance: envelope comparison with 15% tolerance
- ability and combat: envelope comparison with 10% tolerance
- scenario workloads: hard threshold against frame budget targets
- serialization: envelope comparison with 10% tolerance

Regressions exceeding the tolerance fail the benchmark gate.

## Reporting

### Machine-Readable Output

All benchmark results must be emitted in JSON format compatible with
`ValidationFormat`. Fields include:

- scenario name and version
- parameters
- metric name, value, unit, and statistical summary
- comparison result: pass, warn, fail
- baseline reference

### Human-Readable Output

CLI output should include:

- summary table with scenario, metric, current value, baseline value, and status
- highlighted regressions and improvements
- overall pass/fail status

### Trend Tracking

Over time, benchmark results should be comparable across runs to detect gradual
drift. The validation persistence layer supports this through timestamped artifact
storage.

## Benchmark Sequencing

### Stage 1 Benchmarks (Entity Throughput, Event Performance)

Available when: Stage 1 of implementation roadmap is complete.
Validates: entity and component model, tag queries, event dispatch.

### Stage 2 Benchmarks (Ability, Effect, Combat)

Available when: Stage 2 of implementation roadmap is complete.
Validates: ability pipeline, effect processing, combat resolution.

### Stage 3 Benchmarks (Extension Systems)

Available when: Stage 3 of implementation roadmap is complete.
Validates: inventory operations, progression hooks, AI query throughput.

### Stage 4 Benchmarks (Scenario Workloads, Serialization)

Available when: Stage 4 of implementation roadmap is complete.
Validates: full-frame gameplay cost, save/load performance, replay overhead.

## Anti-Patterns

- do not benchmark only the happy path; include edge cases and stress scenarios
- do not use benchmarks that require manual setup or visual inspection
- do not compare benchmarks across different hardware without explicit platform
  tagging
- do not promote baselines without documenting the reason for the change
- do not ignore small consistent regressions; they compound over time

## Related Documents

- [gameplay_mechanics_agent_prompt.md](gameplay_mechanics_agent_prompt.md)
- [gameplay_mechanics_research_roadmap.md](gameplay_mechanics_research_roadmap.md)
- [gameplay_mechanics_implementation_roadmap.md](gameplay_mechanics_implementation_roadmap.md)
- [gameplay_mechanics_milestones.md](gameplay_mechanics_milestones.md)
- [gameplay_mechanics_risks_and_operating_loop.md](gameplay_mechanics_risks_and_operating_loop.md)
- [canary_gameplay_and_tuning.md](canary_gameplay_and_tuning.md)
- [roadmap.md](roadmap.md)
