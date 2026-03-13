# Gameplay System — Requirements & Agent Brief

**Date:** 2026-03-13
**Branch:** `zpc_3c`

## 1. Mission

Build a production-quality Gameplay (Camera, Character, Control) system on top of the ZPC
framework. The system must be engine-agnostic, validation-driven, and designed for
tuning via canary scenarios. All code must reuse ZPC's existing layering rather than
duplicating or bypassing it.

## 2. Scope

### 2.1 Camera System
- Multiple camera modes: orbit, follow, FPS, cinematic rail, free-fly
- Smooth transitions between modes (blend trees, `slerp`-based interpolation)
- Configurable FOV, near/far planes, aspect ratio
- View and projection matrix output compatible with Vulkan clip conventions
- Frustum extraction for culling queries
- Shake/trauma system with configurable decay
- Dead-zone and soft-zone framing for character tracking
- Input-to-visual latency measurement and validation

### 2.2 Character System
- State machine for character locomotion (idle, walk, run, sprint, jump, fall, land)
- Ground detection abstraction (raycast/spherecast interface, not physics implementation)
- Movement intent pipeline: raw input → action mapping → movement intent → velocity
- Acceleration/deceleration curves with configurable profiles
- Coyote time, jump buffering, variable-height jump
- Rotation modes: velocity-aligned, input-aligned, camera-relative
- State tags for animation system integration
- Telemetry: state transition counts, time-in-state, velocity profiles

### 2.3 Control / Input System
- Device-agnostic action map: named actions bound to keys/axes/buttons
- Input buffering via `RingBuffer`
- Axis processing: dead-zone, response curve (linear/quadratic/custom), sensitivity
- Simultaneous multi-device support (keyboard+mouse, gamepad)
- Action states: pressed, held, released, with duration tracking
- Composable modifiers (shift+key, double-tap detection)
- Remapping support (runtime rebind)

## 3. Non-Goals (Phase 1)
- Networked/replicated character movement
- Full physics engine integration (we define interfaces, not implementations)
- Animation blending (we emit state tags; animation is a consumer)
- Actual rendering (we output view/projection matrices; rendering is a consumer)
- Audio integration

## 4. Quality Gates

Every merge to `master` must satisfy:

| Metric | Threshold | Source |
|--------|-----------|--------|
| Input-to-camera latency | ≤ 16.67 ms (1 frame @ 60 FPS) | Validation suite |
| Camera smoothness (jitter) | ≤ 0.5 px/frame RMS | Validation suite |
| Character state transition correctness | 100% of defined transitions | Unit tests |
| Frame budget (Gameplay update) | ≤ 2 ms @ 60 FPS | Benchmark |
| Coyote time accuracy | ± 1 frame | Canary scenario |
| Jump buffer accuracy | ± 1 frame | Canary scenario |
| slerp precision | ≤ 1e-6 error vs. reference | Unit test |

## 5. Constraints

- **C++20 minimum** — constexpr where possible, concepts for type constraints
- **Header-only preferred** for core types and math; `.cpp` for service implementations
- **No external dependencies** beyond what ZPC already vendors
- **CMake INTERFACE library pattern** per ZPC convention
- **Plain main() tests** — no googletest/catch2; use `assert()` / `fprintf+return 1`
- **CTest registration** for all tests; benchmarks omit `add_test()`
- **Namespace:** `zs::` for framework extensions, `zs::gameplay::` for domain types (TBD)

## 6. Deliverables per Week

| Week | Deliverable |
|------|-------------|
| 1 | Infrastructure audit, architecture doc, module decomposition, shared data schemas |
| 2 | Input abstraction module, action maps, axis processing, unit tests |
| 3 | Camera core (struct, matrix builders, frustum), orbit + follow modes, validation scenarios |
| 4 | Character state machine, movement pipeline, ground detection interface |
| 5 | Camera-character integration, mode transitions, shake system, full canary suite |
| 6 | Performance optimization, benchmark pass, documentation, merge preparation |

## 7. Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|-----------|
| `slerp` gap blocks camera interpolation | High | High | Implement in Week 1 as first code task |
| Input abstraction design mismatch with future engines | Medium | Medium | Keep abstract; define traits/concepts, not concrete device APIs |
| Character state machine complexity explosion | Medium | High | Start with minimal states (5-6); use data-driven transitions |
| Performance regression from validation overhead | Low | Medium | Benchmarks run separately from CTest suite |
| Vulkan clip convention mismatch | Medium | Medium | Test projection matrices against known reference values early |
