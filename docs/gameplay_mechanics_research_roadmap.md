# Gameplay Mechanics Research Roadmap

This document defines the research areas, sequencing, and investigation targets for
the gameplay mechanics system. It is meant to be consumed alongside the agent prompt
and used as a living checklist of what needs to be understood before or during
implementation.

## Purpose

Research is not implementation. The goal of this roadmap is to ensure that every
major design decision in the gameplay mechanics system is informed by prior
investigation of existing engines, academic approaches, ZPC-internal infrastructure,
and concrete tradeoff analysis rather than guesswork.

## Research Areas

### Area 1: Entity And Actor Representation

Questions to resolve:

- how actors and entities should be represented at the gameplay layer
- whether a component-based, trait-based, or archetype-based model best fits ZPC
- how gameplay-layer entities relate to ECS or simulation-layer entities if both
  exist
- how entity identity, lifecycle, ownership, and authority should be modeled
- how entity composition supports hot-reload, save/load, and network replication

Survey targets:

- Unity ECS and MonoBehaviour composition model
- Unreal Engine actor and component model
- Bevy ECS archetype storage and system scheduling
- EnTT sparse-set ECS model
- flecs query and relationship model
- academic entity-component surveys

Deliverable: a written recommendation on entity representation that fits the ZPC
module and validation philosophy.

### Area 2: Ability And Effect Systems

Questions to resolve:

- how abilities should be authored, parameterized, and scheduled
- how effects modify state: immediate, over-time, conditional, stacking
- how targeting, cost, cooldown, and prerequisite systems compose
- how ability and effect metadata supports tooling, serialization, and validation
- what the boundary is between generic ability infrastructure and project-specific
  ability content

Survey targets:

- Unreal Gameplay Ability System (GAS)
- Unity DOTS ability prototypes
- Path of Exile and Diablo-family effect stacking models
- academic gameplay ontology research
- data-driven ability table approaches in MMO server architectures

Deliverable: a typed ability and effect model sketch with clear generic versus
project-specific boundaries.

### Area 3: Combat And Interaction Systems

Questions to resolve:

- how damage, healing, mitigation, and status effects compose
- how combat resolution ordering should be determined
- how interaction systems generalize beyond combat to environmental, social, and
  economic interactions
- how collision, targeting, and area-of-effect queries integrate with physics and
  spatial systems
- how deterministic or partially deterministic combat resolution supports replay
  and validation

Survey targets:

- GDC talks on combat system architecture
- tabletop RPG resolution models as formal systems
- MMO server-side damage pipeline architectures
- action game hit-detection and confirmation models
- fighting game rollback and deterministic frame models

Deliverable: a combat and interaction pipeline sketch with extension points for
project-specific rules.

### Area 4: State, Tag, And Condition Systems

Questions to resolve:

- how gameplay state and tags should be represented
- how conditions, prerequisites, and reactive triggers compose
- how state queries support both runtime logic and tooling inspection
- how tag and condition systems interact with save/load boundaries
- how tag namespaces and scoping prevent collision across independent content
  modules

Survey targets:

- Unreal Gameplay Tags
- condition and prerequisite systems in strategy and RPG engines
- state machine and statechart models for gameplay state
- blackboard and knowledge-base patterns in game AI

Deliverable: a tag and condition model sketch compatible with ZPC validation schemas.

### Area 5: Data-Driven Mechanics Authoring

Questions to resolve:

- how mechanics definitions should be serialized and loaded
- what the right format family is: JSON, binary, schema-validated tables, or DSL
- how hot-reload and live iteration should work for mechanics data
- how content validation should catch errors at load time rather than at runtime
- how authoring tools generate and consume mechanics data

Survey targets:

- ZPC ValidationSchema and ValidationFormat infrastructure
- data-driven design patterns in commercial engines
- configuration-as-code approaches in server-side game systems
- schema-evolution and migration strategies for long-lived game content

Deliverable: a data format and validation strategy recommendation that reuses ZPC
validation infrastructure.

### Area 6: Event Bus And Messaging

Questions to resolve:

- how gameplay events should be dispatched and consumed
- whether a central event bus, per-system callbacks, or reactive streams best fit
- how event ordering and priority affect determinism
- how event history supports replay, debugging, and telemetry
- how event schemas integrate with validation and serialization

Survey targets:

- observer and mediator patterns in game engines
- reactive programming models (Rx-family) for gameplay
- event sourcing patterns in server-side game architectures
- ZPC async runtime and signaling infrastructure

Deliverable: an event system design sketch that integrates with ZPC async runtime.

### Area 7: Inventory, Equipment, And Progression Hooks

Questions to resolve:

- how inventory and equipment systems should be structured as framework hooks
- how stat modification chains compose with ability and effect systems
- how progression systems (experience, levels, skill trees) should be represented
- what the boundary is between framework hooks and project-specific content
- how persistence and save/load boundaries apply to inventory and progression state

Survey targets:

- inventory system architectures in RPG engines
- stat modification and attribute pipeline patterns
- progression and unlock systems in various game genres
- item database and loot table design patterns

Deliverable: a hook and extension point specification, not a full inventory
implementation.

### Area 8: AI-Facing Interfaces

Questions to resolve:

- how AI systems should query and act on gameplay state
- how behavior trees, utility AI, GOAP, and other AI models interface with the
  mechanics layer
- how AI decision-making integrates with ability and interaction systems
- how AI interfaces support debugging, visualization, and telemetry
- how AI-facing interfaces differ from player-facing interfaces

Survey targets:

- behavior tree implementations in commercial engines
- utility AI scoring models
- GOAP and HTN planning systems
- blackboard and world-state query patterns

Deliverable: an AI-facing interface contract sketch that decouples AI strategy from
mechanics implementation.

### Area 9: Determinism, Replay, And Networking Friendliness

Questions to resolve:

- what level of determinism is achievable and desirable for gameplay mechanics
- how replay systems should capture and reproduce gameplay state evolution
- how client-server authority boundaries affect mechanics execution
- how rollback and prediction interact with ability and effect pipelines
- what the cost of full determinism is versus selective determinism

Survey targets:

- deterministic lockstep models in RTS engines
- rollback netcode in fighting games
- server-authoritative MMO architectures
- Gaffer On Games networking articles
- floating-point determinism strategies

Deliverable: a determinism strategy recommendation with clear tradeoffs documented.

### Area 10: Performance And Scalability

Questions to resolve:

- how many active entities with full gameplay state can be processed per frame
- what the memory layout implications are for different entity and component models
- how system scheduling and parallelism affect gameplay throughput
- where the bottlenecks are likely to appear first: CPU, memory, cache, or
  serialization
- how batch processing and SIMD-friendly layouts apply to gameplay systems

Survey targets:

- ECS performance benchmarks and data-oriented design literature
- ZPC existing memory and async runtime infrastructure
- commercial engine entity count limits and optimization strategies
- SoA versus AoS tradeoffs for gameplay data

Deliverable: performance model estimates and benchmark targets for the gameplay
layer.

## Research Sequencing

### Phase 1: Foundations (Areas 1, 4, 5, 6)

Entity representation, state and tags, data-driven authoring, and event messaging
form the substrate on which everything else is built. These must be investigated
first.

### Phase 2: Core Mechanics (Areas 2, 3)

Ability and effect systems, combat and interaction pipelines depend on the
foundation decisions from Phase 1. These are the primary gameplay-visible systems.

### Phase 3: Extension Points (Areas 7, 8)

Inventory, equipment, progression hooks, and AI-facing interfaces are extension
points that build on the core mechanics. They can be researched in parallel with
Phase 2 but should not drive foundation decisions.

### Phase 4: Production Qualities (Areas 9, 10)

Determinism, replay, networking friendliness, and performance scalability are
cross-cutting concerns that affect all layers. They should be investigated
continuously but become primary focus once the core mechanics shape is stable.

## Research Method

For each area:

1. survey two to four existing implementations or references
2. identify the tradeoffs relevant to ZPC constraints
3. write a short recommendation with explicit alternatives considered
4. validate the recommendation against ZPC validation, canary, and service
   infrastructure
5. update this roadmap with findings and remaining open questions

## Relationship To Implementation

Research findings feed directly into the implementation roadmap. No major
implementation decision should proceed without a corresponding research
recommendation in this document or its linked artifacts.

## Related Documents

- [gameplay_mechanics_agent_prompt.md](gameplay_mechanics_agent_prompt.md)
- [gameplay_mechanics_implementation_roadmap.md](gameplay_mechanics_implementation_roadmap.md)
- [gameplay_mechanics_benchmark_plan.md](gameplay_mechanics_benchmark_plan.md)
- [gameplay_mechanics_milestones.md](gameplay_mechanics_milestones.md)
- [gameplay_mechanics_risks_and_operating_loop.md](gameplay_mechanics_risks_and_operating_loop.md)
- [gameplay_and_mechanics.md](gameplay_and_mechanics.md)
- [canary_gameplay_and_tuning.md](canary_gameplay_and_tuning.md)
- [roadmap.md](roadmap.md)
