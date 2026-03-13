# Gameplay System — 6-Week Milestone Plan

**Date:** 2026-03-13
**Branch:** `zpc_3c`

---

## Phase 1: Foundation (Week 1)

**Goal:** Establish architecture, module boundaries, and shared types.

### Tasks
1. **Infrastructure audit** — audit ZPC codebase for reusable math, validation, canary,
   CMake, and test infrastructure. Document gaps. *(DONE)*
2. **Module decomposition** — define top-level modules (`gameplay_input`, `gameplay_camera`,
   `gameplay_character`, `gameplay_core`) and their dependency graph. Define frame update order.
3. **Shared data model** — define schemas for: `InputAction`, `MovementIntent`,
   `CharacterState`, `CameraMode`, `FrameTelemetry`.
4. **Validation layout** — define validation scenario descriptors and artifact directory
   structure (`test/gameplay/`, `zpc_assets/gameplay/baselines/`).
5. **Architecture document** — consolidate all decisions into `docs/gameplay_architecture.md`.

### Exit Criteria
- Architecture doc reviewed and stable
- CMake skeleton builds (empty targets, correct dependencies)
- Shared type headers compile and have basic tests

---

## Phase 2: Input System (Week 2)

**Goal:** Deliver a working input abstraction with action maps and axis processing.

### Tasks
1. `ActionMap` — named actions with key/axis bindings, pressed/held/released states
2. Axis processing pipeline — dead-zone, response curve, sensitivity
3. `InputBuffer` — ring-buffer backed input history (reuse `zs::RingBuffer`)
4. Multi-device compositing — keyboard+mouse and gamepad simultaneously
5. Unit tests and validation scenarios for input latency

### Exit Criteria
- `ActionMap` can be configured, polled, and returns correct states
- Axis processing produces expected curves (validated against reference data)
- Input buffer correctly stores and replays N frames of history
- All tests pass under CTest

---

## Phase 3: Camera Core (Week 3)

**Goal:** Camera struct, matrix builders, and two basic camera modes.

### Tasks
1. `Camera` struct — position, orientation (quaternion), FOV, near/far, aspect
2. `lookAt()`, `perspective()`, `orthographic()` — Vulkan clip conventions
3. `slerp()` — add to ZPC math (gap from audit)
4. Orbit camera mode — pivot point, distance, azimuth/elevation, zoom
5. Follow camera mode — target tracking with offset, smoothing, dead-zone
6. Frustum extraction and plane representation
7. Validation scenarios: orbit 360°, follow moving target, matrix correctness

### Exit Criteria
- Projection matrices match Vulkan reference values (validated)
- `slerp` precision ≤ 1e-6 (unit test)
- Orbit and follow modes produce smooth output (jitter ≤ 0.5 px RMS)
- Camera update ≤ 0.5 ms per frame (benchmark)

---

## Phase 4: Character System (Week 4)

**Goal:** Character state machine and movement pipeline.

### Tasks
1. State machine — idle, walk, run, sprint, jump, fall, land transitions
2. Movement intent pipeline — input action → movement intent → velocity
3. Acceleration/deceleration curves — configurable profiles (linear, ease-in-out)
4. Ground detection interface — `IGroundQuery` trait with raycast/spherecast
5. Jump mechanics — coyote time, jump buffering, variable-height jump
6. State tags for animation consumers
7. Telemetry — state transition counts, time-in-state histograms

### Exit Criteria
- All defined state transitions are correct (100% coverage test)
- Coyote time and jump buffer accurate to ± 1 frame
- Movement velocity profiles match configured curves (validated)
- Character update ≤ 0.5 ms per frame (benchmark)

---

## Phase 5: Integration (Week 5)

**Goal:** Wire camera, character, and input together. Add advanced features.

### Tasks
1. Camera-character binding — camera follows character with configured mode
2. Camera mode transitions — blend between orbit/follow/FPS with slerp
3. Camera shake/trauma — Perlin-based shake with configurable decay
4. FPS camera mode — mouse-look with pitch clamp, character-relative
5. Full canary scenario suite — scripted sequences exercising all modes
6. Input-to-screen latency measurement end-to-end

### Exit Criteria
- Mode transitions are smooth (no discontinuities in output)
- Shake system produces bounded, decaying offsets
- End-to-end latency ≤ 16.67 ms
- Full canary suite passes with stable baselines

---

## Phase 6: Polish & Merge (Week 6)

**Goal:** Optimize, document, and prepare for merge to `master`.

### Tasks
1. Performance profiling — identify and eliminate hotspots
2. Total Gameplay frame budget ≤ 2 ms (camera + character + input combined)
3. API documentation — all public types and functions documented
4. Integration guide — how to consume Gameplay from an application
5. Baseline snapshots — freeze validation baselines for CI
6. Final merge preparation — rebase on latest `master`, resolve conflicts

### Exit Criteria
- All quality gates from requirements doc pass
- No regressions in existing ZPC tests
- Documentation complete
- Clean merge to `master` with no conflicts

---

## Dependency Graph

```
Week 1 (Foundation)
  └─→ Week 2 (Input)
        └─→ Week 3 (Camera) ←── slerp added in Week 3
              └─→ Week 4 (Character)
                    └─→ Week 5 (Integration)
                          └─→ Week 6 (Polish)
```

Input is independent of Camera/Character at the API level but is consumed by both.
Camera and Character are independent of each other but integrated in Week 5.
