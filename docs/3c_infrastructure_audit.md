# 3C Infrastructure Audit — ZPC Codebase

**Date:** 2026-03-13
**Branch:** `zpc_3c`
**Worktree:** `H:\Codes\zpc_3c`

## 1. Summary

This document records the results of auditing the ZPC framework codebase to determine
what infrastructure is available for building a 3C (Camera, Character, Control) system
and what gaps must be filled.

**Verdict:** ZPC provides strong math primitives, a full validation/benchmark pipeline,
canary scenario infrastructure, and a clean CMake module pattern. No 3C-specific code
exists. Key gaps are: `slerp`, input abstraction, camera struct, view/projection matrix
builders, and frustum utilities.

---

## 2. Available Infrastructure

### 2.1 Math

| Asset | Location | Notes |
|-------|----------|-------|
| `zs::vec<T, Ns...>` | `include/zensim/math/Vec.h`, `VecInterface.hpp` | `.dot()` (L506), `.cross()` (L922), `.normalized()` (L934) |
| `zs::Rotation<T,3>` | `include/zensim/math/Rotation.hpp` | Quaternion storage, axis-angle ctor, euler angles |
| Quaternion multiply | `Rotation.hpp` L245+ | `operator*`, conjugate multiply |
| Matrix types | `Vec.h` | `zs::vec<T, R, C>` used as matrix |

**Gap — `slerp`:** No standalone spherical-linear-interpolation function exists.
Must be added to `Rotation.hpp` or a new `Interpolation.hpp`.

### 2.2 Containers

| Asset | Location | Notes |
|-------|----------|-------|
| `RingBuffer<T, Size>` | `include/zensim/container/RingBuffer.hpp` | Fixed-size, lock-free. Ideal for input buffering. |
| `SmallString` | `include/zensim/types/SmallVector.hpp` L98 | SSO string type |
| `Vector`, `TileVector`, `HashTable`, `Bvh` | `include/zensim/container/` | General-purpose containers |

### 2.3 Validation & Benchmark

| Asset | Location | Notes |
|-------|----------|-------|
| `ValidationRecord` | `ValidationSchema.hpp` | Single metric record with thresholds |
| `ValidationSuiteReport` | `ValidationSchema.hpp` | Collection of records, summary refresh |
| `ValidationComparisonReport` | `ValidationCompare.hpp` | Diff two reports (regressed/improved/added/removed) |
| `ValidationMeasurement` | `ValidationSchema.hpp` | Numeric value + unit |
| `ValidationThreshold` | `ValidationSchema.hpp` | Modes: `less_equal`, `greater_equal`, `inclusive_range` |
| JSON format/parse | `ValidationFormat.hpp`, `ValidationPersistence.hpp` | Round-trip JSON serialization, file I/O |

3C feel-metrics (frame time, input-to-screen latency, smoothness, overshoot) can
plug directly into `ValidationRecord` with appropriate thresholds.

### 2.4 Canary / Scenario

| Asset | Location | Notes |
|-------|----------|-------|
| `CanaryScenarioDescriptor` | `CanaryScenario.hpp` | Scenario registration with parameters |
| `CanaryParameterDescriptor` | `CanaryScenario.hpp` | Parameter name, type, range, default |
| `CanaryScenarioService` | `CanaryScenario.hpp` | Abstract service interface |
| `LocalCanaryScenarioService` | `LocalCanaryService.hpp` | In-process implementation |

3C scenarios (e.g., "orbit camera 360° at 60 FPS", "sprint + jump sequence") can
register through this system with tunable parameters.

### 2.5 Interface Services & Async Runtime

| Asset | Location | Notes |
|-------|----------|-------|
| `LocalInterfaceServices` | `LocalInterfaceServices.hpp` | Wraps `AsyncRuntime`, session management |
| `AsyncRuntime` | `AsyncRuntime.hpp` | Thread pool, executor tracking |
| `ExecutionGraph` | `ExecutionGraph.hpp` | `PassNode`, `ResourceHandle`, `AccessMode`, `ExecutionLane` |

The execution graph could model the 3C frame update pipeline (input → character → camera)
as dependent passes with explicit resource hazards.

### 2.6 CMake Pattern

Defined in `include/zensim/CMakeLists.txt` L519-547:

```cmake
add_library(zpc_<module>_core INTERFACE)
target_sources(zpc_<module>_core INTERFACE <headers>)
target_link_libraries(zpc_<module>_core INTERFACE <deps>)
```

New 3C modules should follow this pattern exactly:
- `zpc_3c_core` — math extensions, data types
- `zpc_3c_input` — input abstraction
- `zpc_3c_camera` — camera types and controllers
- `zpc_3c_character` — character state and movement
- `zpc_3c_validation` — 3C-specific validation scenarios

### 2.7 Test Infrastructure

- **No external test framework** — tests are plain `main()` executables using `assert()`.
- **CTest integration** — tests registered via `add_test()` in `test/CMakeLists.txt`.
- **Benchmarks** use `add_executable` + `add_dependencies` but omit `add_test()`.
- **Naming convention:** CTest names prefixed with `Zs` (e.g., `ZsValidationSchema`).
- **Minimal linking:** tests link to the smallest relevant target (`zpc_validation_core`, `zpcbase`, etc.).
- **Test utilities** in `test/utils/` as header-only helpers in `namespace zs`.

3C tests should follow these conventions exactly.

---

## 3. Identified Gaps

| # | Gap | Severity | Where to Add |
|---|-----|----------|-------------|
| 1 | `slerp(q0, q1, t)` for quaternions | High | `Rotation.hpp` or new `Interpolation.hpp` |
| 2 | Input abstraction (action maps, device polling) | High | New module under `include/zensim/` |
| 3 | Camera struct (position, orientation, FOV, near/far, aspect) | High | New header |
| 4 | View/projection matrix builders (`lookAt`, `perspective`, `ortho`) | High | New header or extend `Vec.h` |
| 5 | Frustum representation and culling | Medium | New header |
| 6 | Character state machine / movement model | High | New module |
| 7 | Frame-rate-independent delta-time utilities | Medium | May already exist in AsyncRuntime |
| 8 | Viewport lifecycle management | Low | Deferred to integration phase |

---

## 4. Existing Docs Consulted

- `docs/application_layer_design.md` — mentions input/event abstraction and viewport lifecycle as app-layer concerns
- `docs/gameplay_and_mechanics.md` — general gameplay design notes
- `docs/rendering_and_visualization.md` — rendering pipeline overview
- `docs/canary_gameplay_and_tuning.md` — canary tuning approach
- `docs/roadmap.md` — overall ZPC roadmap
- `docs/platform_and_build_profiles.md` — platform targeting

---

## 5. Conclusion

ZPC is well-suited as a foundation for 3C. The validation pipeline is production-ready
for feel-metrics. The canary system handles scenario management. Math primitives cover
vector/quaternion basics but need `slerp`. The main engineering work is defining the 3C
domain types (camera, character, input) and wiring them through the existing framework.
