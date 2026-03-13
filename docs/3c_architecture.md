# 3C System — Architecture Design Document

**Date:** 2026-03-13
**Branch:** `zpc_3c`
**Status:** Approved for implementation (Week 1 deliverable)

---

## Table of Contents

1. [Overview](#1-overview)
2. [Design Principles](#2-design-principles)
3. [Module Architecture](#3-module-architecture)
4. [Frame Update Pipeline](#4-frame-update-pipeline)
5. [Shared Data Model](#5-shared-data-model)
6. [ZPC Integration Points](#6-zpc-integration-points)
7. [Math Extensions Required](#7-math-extensions-required)
8. [Test & Validation Strategy](#8-test--validation-strategy)
9. [CMake Structure](#9-cmake-structure)
10. [Namespace & Coding Conventions](#10-namespace--coding-conventions)
11. [Open Questions & Decisions](#11-open-questions--decisions)
12. [References](#12-references)

---

## 1. Overview

The 3C system provides Camera, Character, and Control (input) subsystems for
game/simulation applications built on ZPC. It is:

- **Engine-agnostic** — outputs view/projection matrices and state snapshots;
  does not own rendering or physics
- **Validation-driven** — every feature has measurable quality gates backed
  by ZPC's validation framework
- **Modular** — camera, character, and input are independently testable
  modules linked only through shared data types
- **Tunable** — all parameters exposed through ZPC's canary scenario system
  for systematic experimentation

### What 3C Does
- Translates raw device input into processed actions with dead-zone/curve/sensitivity
- Manages character locomotion state (idle→walk→run→jump→fall→land)
- Computes character movement with acceleration curves and jump mechanics
- Drives camera modes (orbit, follow, FPS, rail) with smooth transitions and shake
- Outputs view/projection matrices for rendering and frustum for culling
- Collects per-frame telemetry mapped to validation records

### What 3C Does Not Do
- Render anything (consumer responsibility)
- Simulate physics (defines `IGroundQuery` interface; physics implements it)
- Blend animations (emits state tags; animation system consumes them)
- Handle networking (single-player frame loop only in Phase 1)

---

## 2. Design Principles

1. **No circular dependencies.** Data flows in one direction: Input → Character → Camera.
   Camera never feeds back to Character within a frame.

2. **Fixed-size hot-path types.** Types used per-frame (`ActionSnapshot`,
   `CharacterSnapshot`, `CameraOutput`) use fixed-size arrays and value types.
   No heap allocation in the update loop.

3. **Quaternion-only rotations.** All rotations stored as quaternions. Euler angles
   are computed on demand for debug display, never stored.

4. **Validation-first development.** Every new feature ships with a validation scenario
   and baseline. Code without validation is considered incomplete.

5. **Reuse ZPC infrastructure.** Math, containers, validation, canary, and CMake patterns
   come from ZPC. If a capability is missing, add it to ZPC's foundation (e.g., `slerp`)
   rather than reimplementing locally.

6. **Header-only for data types, compiled for services.** Structs, enums, and small
   functions are header-only. Stateful services (input polling, canary registration)
   may use `.cpp` files.

---

## 3. Module Architecture

### 3.1 Dependency Graph

```
zpcbase
  └─→ zpc_validation_core
        └─→ zpc_3c_core          [Core.hpp, MathExtensions.hpp, Telemetry.hpp]
              ├─→ zpc_3c_input     [ActionMap, AxisProcessor, InputBuffer, InputTypes]
              ├─→ zpc_3c_camera    [Camera, CameraMode, MatrixBuilders, Frustum, Shake]
              ├─→ zpc_3c_character [CharacterState, MovementPipeline, GroundQuery, JumpMechanics]
              │     └─→ zpc_3c_input (for MovementIntent from ActionSnapshot)
              └─→ zpc_3c_scenarios [ThreeCScenarios — canary glue]
                    └─→ zpc_canary_core + all 3c modules
```

### 3.2 Key Invariant

Camera and Character have **no compile-time dependency on each other**. They communicate
only through `CharacterSnapshot` and `ActionSnapshot`, which are defined in `zpc_3c_core`.
This means:

- Camera can be tested without Character (feed synthetic snapshots)
- Character can be tested without Camera
- Integration tests wire them together

### 3.3 Directory Layout

```
include/zensim/3c/
  Core.hpp
  MathExtensions.hpp
  Telemetry.hpp
  input/
    ActionMap.hpp
    AxisProcessor.hpp
    InputBuffer.hpp
    InputTypes.hpp
  camera/
    Camera.hpp
    CameraMode.hpp
    MatrixBuilders.hpp
    Frustum.hpp
    Shake.hpp
  character/
    CharacterState.hpp
    MovementPipeline.hpp
    GroundQuery.hpp
    JumpMechanics.hpp
  scenarios/
    ThreeCScenarios.hpp
```

---

## 4. Frame Update Pipeline

Every frame executes four phases in strict order:

```
FRAME N
  1. INPUT      → reads devices, produces ActionSnapshot
  2. CHARACTER   → reads ActionSnapshot, produces CharacterSnapshot
  3. CAMERA      → reads ActionSnapshot + CharacterSnapshot, produces CameraOutput
  4. TELEMETRY   → reads all outputs, produces FrameTelemetry → ValidationRecord
```

Phase dependencies form a DAG:

```
ActionSnapshot ──┬──→ CHARACTER ──→ CharacterSnapshot ──┐
                 │                                       │
                 └──→ CAMERA ←──────────────────────────┘
                         │
                         └──→ CameraOutput
```

### Integration with ZPC ExecutionGraph (optional)

Each phase maps to a `PassNode` with explicit resource hazards:

| Pass | Reads | Writes |
|------|-------|--------|
| `3c_input` | raw device state | `ActionSnapshot` |
| `3c_character` | `ActionSnapshot` | `CharacterSnapshot` |
| `3c_camera` | `ActionSnapshot`, `CharacterSnapshot` | `CameraOutput` |
| `3c_telemetry` | all snapshots (read-only) | `FrameTelemetry` |

---

## 5. Shared Data Model

Defined in detail in `docs/3c_shared_data_model.md`. Summary of key types:

| Type | Module | Purpose |
|------|--------|---------|
| `FrameContext` | core | dt, time, frameNumber |
| `ActionSnapshot` | input | All action states + processed axes |
| `MovementIntent` | character | Desired direction, magnitude, jump/sprint flags |
| `CharacterSnapshot` | character | Position, velocity, rotation, state, ground info |
| `CameraOutput` | camera | Camera state + view/projection matrices |
| `Frustum` | camera | 6 clip planes for culling |
| `FrameTelemetry` | core | Per-frame performance and quality metrics |

---

## 6. ZPC Integration Points

| ZPC Asset | How 3C Uses It |
|-----------|---------------|
| `zs::vec<T, N>` | Vec2f, Vec3f, Vec4f, Mat4f — all spatial math |
| `zs::Rotation<T, 3>` | Quatf — all rotation storage and interpolation |
| `zs::RingBuffer<T, N>` | InputBuffer — N-frame input history for replay/validation |
| `ValidationRecord` | Each telemetry metric maps to a named record with thresholds |
| `ValidationSuiteReport` | Per-scenario validation output, saved as JSON baseline |
| `compare_validation_reports()` | Regression detection between runs |
| `ValidationPersistence` | Save/load baseline JSON files |
| `CanaryScenarioDescriptor` | Register 3C scenarios with tunable parameters |
| `LocalCanaryScenarioService` | Run scenarios in-process, manage baselines |
| `ExecutionGraph` (optional) | Model 3C phases as PassNodes with resource hazards |
| `SmallString` | Scenario names, parameter keys |

### Gaps to Fill in ZPC

| Gap | Plan |
|-----|------|
| `slerp(q0, q1, t)` | Add to `MathExtensions.hpp` or propose to ZPC `Rotation.hpp` |
| `damping(current, target, smoothing, dt)` | Add to `MathExtensions.hpp` |
| `spring(current, target, velocity, stiffness, damping, dt)` | Add to `MathExtensions.hpp` |

---

## 7. Math Extensions Required

```cpp
namespace zs {

  /// Spherical linear interpolation between two quaternions
  template <typename T>
  constexpr auto slerp(Rotation<T, 3> const& a, Rotation<T, 3> const& b, T t)
    -> Rotation<T, 3>;

  /// Exponential damping (frame-rate independent smoothing)
  /// Returns: current + (target - current) * (1 - exp(-smoothing * dt))
  template <typename T, int N>
  constexpr auto damp(vec<T, N> const& current, vec<T, N> const& target,
                      T smoothing, T dt) -> vec<T, N>;

  /// Critically damped spring (for camera follow, character acceleration)
  template <typename T, int N>
  constexpr auto spring_damper(vec<T, N>& position, vec<T, N>& velocity,
                               vec<T, N> const& target,
                               T stiffness, T damping, T dt) -> void;

  /// Remap a value from one range to another with clamping
  template <typename T>
  constexpr auto remap(T value, T inMin, T inMax, T outMin, T outMax) -> T;

}
```

---

## 8. Test & Validation Strategy

### 8.1 Test Framework

- **No external framework.** Plain `main()` executables with `assert()`.
- **CTest registration.** All tests use `add_test()`. Benchmarks omit it.
- **Naming convention:** `Zs3c<Category><Test>` (e.g., `Zs3cCameraOrbit360`).
- **Minimal linking:** Tests link to the smallest relevant `zpc_3c_*` target.

### 8.2 Validation Pipeline

1. **Scenario definition** — `CanaryScenarioDescriptor` with named parameters
2. **Scenario execution** — deterministic N-frame simulation, produces `ValidationSuiteReport`
3. **Baseline save** — JSON to `zpc_assets/3c/baselines/<category>/<scenario>.json`
4. **Regression check** — `compare_validation_reports()` against saved baseline
5. **Baseline promotion** — when tuning improves metrics, overwrite baseline

### 8.3 Quality Gates

| Metric | Threshold | Validated By |
|--------|-----------|-------------|
| Total 3C frame budget | ≤ 2.0 ms | `3c.integration.full_loop` |
| Input-to-camera latency | ≤ 16.67 ms | `3c.integration.input_to_screen` |
| Camera jitter | ≤ 0.5 px RMS | `3c.camera.orbit_360`, `3c.camera.follow_moving` |
| State transition correctness | 100% | `3c.char.state_transitions` |
| Coyote time accuracy | ± 1 frame | `3c.char.coyote_time` |
| Jump buffer accuracy | ± 1 frame | `3c.char.jump_buffer` |
| slerp precision | ≤ 1e-6 | `test/3c/math/slerp.cpp` |
| Projection matrix accuracy | ≤ 1e-6 | `3c.camera.projection_reference` |

Full scenario list in `docs/3c_validation_scenarios.md`.

---

## 9. CMake Structure

New targets are INTERFACE libraries following ZPC convention:

```cmake
# File lists
set(ZENSIM_LIBRARY_3C_CORE_INCLUDE_FILES      3c/Core.hpp 3c/MathExtensions.hpp 3c/Telemetry.hpp)
set(ZENSIM_LIBRARY_3C_INPUT_INCLUDE_FILES      3c/input/ActionMap.hpp ...)
set(ZENSIM_LIBRARY_3C_CAMERA_INCLUDE_FILES     3c/camera/Camera.hpp ...)
set(ZENSIM_LIBRARY_3C_CHARACTER_INCLUDE_FILES  3c/character/CharacterState.hpp ...)
set(ZENSIM_LIBRARY_3C_SCENARIOS_INCLUDE_FILES  3c/scenarios/ThreeCScenarios.hpp)

# Targets
add_library(zpc_3c_core INTERFACE)
target_sources(zpc_3c_core INTERFACE ${ZENSIM_LIBRARY_3C_CORE_INCLUDE_FILES})
target_link_libraries(zpc_3c_core INTERFACE zpc_validation_core)

add_library(zpc_3c_input INTERFACE)
target_sources(zpc_3c_input INTERFACE ${ZENSIM_LIBRARY_3C_INPUT_INCLUDE_FILES})
target_link_libraries(zpc_3c_input INTERFACE zpc_3c_core)

add_library(zpc_3c_camera INTERFACE)
target_sources(zpc_3c_camera INTERFACE ${ZENSIM_LIBRARY_3C_CAMERA_INCLUDE_FILES})
target_link_libraries(zpc_3c_camera INTERFACE zpc_3c_core)

add_library(zpc_3c_character INTERFACE)
target_sources(zpc_3c_character INTERFACE ${ZENSIM_LIBRARY_3C_CHARACTER_INCLUDE_FILES})
target_link_libraries(zpc_3c_character INTERFACE zpc_3c_core zpc_3c_input)

add_library(zpc_3c_scenarios INTERFACE)
target_sources(zpc_3c_scenarios INTERFACE ${ZENSIM_LIBRARY_3C_SCENARIOS_INCLUDE_FILES})
target_link_libraries(zpc_3c_scenarios INTERFACE zpc_canary_core zpc_3c_camera zpc_3c_character zpc_3c_input)
```

---

## 10. Namespace & Coding Conventions

- **Domain types:** `zs::threeC::` — Camera, CharacterState, ActionMap, etc.
- **Math extensions:** `zs::` — slerp, damp, spring_damper (general-purpose)
- **Enums:** scoped (`enum class`), underlying type specified (`u8`)
- **constexpr:** all pure functions, all data-only structs
- **Include guards:** `#pragma once`
- **Documentation:** `///` Doxygen-style comments on all public API
- **File naming:** PascalCase for type-centric headers (e.g., `CharacterState.hpp`)

---

## 11. Open Questions & Decisions

| # | Question | Decision | Rationale |
|---|----------|----------|-----------|
| 1 | Coordinate system handedness? | **Right-handed, Y-up** | Common convention; Vulkan clip handled in projection matrix |
| 2 | `zs::threeC` or `zs::tc`? | **`zs::threeC`** | Explicit and searchable; abbreviation is ambiguous |
| 3 | Should `slerp` go in ZPC core or 3C? | **ZPC core** (`MathExtensions.hpp` initially, propose upstream later) | `slerp` is universally useful beyond 3C |
| 4 | Character state extensibility? | **Enum + transition table** | Start simple; upgrade to data-driven if needed in Week 4 |
| 5 | Input device polling model? | **Pull-based** (application calls poll each frame) | Push-based adds threading complexity; pull is deterministic |

---

## 12. References

| Document | Path |
|----------|------|
| Infrastructure Audit | `docs/3c_infrastructure_audit.md` |
| Requirements & Agent Brief | `docs/3c_requirements.md` |
| 6-Week Milestone Plan | `docs/3c_milestone_plan.md` |
| Module Decomposition | `docs/3c_module_decomposition.md` |
| Shared Data Model | `docs/3c_shared_data_model.md` |
| Validation Scenarios | `docs/3c_validation_scenarios.md` |
| ZPC Application Layer Design | `docs/application_layer_design.md` |
| ZPC Roadmap | `docs/roadmap.md` |
| ZPC Gameplay & Mechanics | `docs/gameplay_and_mechanics.md` |
