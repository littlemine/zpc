# 3C Module Decomposition & Frame Update Order

**Date:** 2026-03-13
**Branch:** `zpc_3c`

---

## 1. Module Hierarchy

```
zpc_3c_core          (shared types, math extensions, telemetry)
├── zpc_3c_input     (action maps, axis processing, input buffer)
├── zpc_3c_character (state machine, movement pipeline, ground query)
├── zpc_3c_camera    (camera struct, matrix builders, modes, shake)
└── zpc_3c_scenarios (canary scenarios, validation glue)
```

### Dependency Graph

```
zpcbase
  └─→ zpc_validation_core
        └─→ zpc_3c_core
              ├─→ zpc_3c_input       (depends on: zpc_3c_core)
              ├─→ zpc_3c_character   (depends on: zpc_3c_core, zpc_3c_input)
              ├─→ zpc_3c_camera      (depends on: zpc_3c_core)
              └─→ zpc_3c_scenarios   (depends on: zpc_canary_core, zpc_3c_camera,
                                       zpc_3c_character, zpc_3c_input)
```

**Key constraint:** Camera and Character do NOT depend on each other directly.
Integration happens at the application layer, where the frame loop wires character
output into camera input. This keeps the modules independently testable.

---

## 2. Directory Layout

```
include/zensim/
  3c/
    Core.hpp              # Forward declarations, common enums, FrameContext
    MathExtensions.hpp    # slerp, damping, spring functions
    Telemetry.hpp         # FrameTelemetry, metrics collection

    input/
      ActionMap.hpp       # ActionDescriptor, ActionState, ActionMap
      AxisProcessor.hpp   # DeadZone, ResponseCurve, Sensitivity
      InputBuffer.hpp     # RingBuffer-backed input history
      InputTypes.hpp      # DeviceType, RawInput, ActionId

    camera/
      Camera.hpp          # Camera struct (pos, orient, FOV, near, far, aspect)
      CameraMode.hpp      # CameraModeBase, orbit, follow, FPS, rail
      MatrixBuilders.hpp  # lookAt(), perspective(), orthographic()
      Frustum.hpp         # Frustum planes, containment tests
      Shake.hpp           # TraumaShake with Perlin noise + decay

    character/
      CharacterState.hpp  # State enum, StateMachine, transition table
      MovementPipeline.hpp # MovementIntent, velocity computation
      GroundQuery.hpp     # IGroundQuery trait, RaycastResult
      JumpMechanics.hpp   # Coyote time, jump buffer, variable-height

    scenarios/
      ThreeCScenarios.hpp # Canary scenario descriptors for 3C validation
```

---

## 3. CMake Registration

Following the existing pattern in `include/zensim/CMakeLists.txt`:

```cmake
# --- 3C Core ---
set(ZENSIM_LIBRARY_3C_CORE_INCLUDE_FILES
  3c/Core.hpp
  3c/MathExtensions.hpp
  3c/Telemetry.hpp
)
add_library(zpc_3c_core INTERFACE)
target_sources(zpc_3c_core INTERFACE ${ZENSIM_LIBRARY_3C_CORE_INCLUDE_FILES})
target_link_libraries(zpc_3c_core INTERFACE zpc_validation_core)

# --- 3C Input ---
set(ZENSIM_LIBRARY_3C_INPUT_INCLUDE_FILES
  3c/input/ActionMap.hpp
  3c/input/AxisProcessor.hpp
  3c/input/InputBuffer.hpp
  3c/input/InputTypes.hpp
)
add_library(zpc_3c_input INTERFACE)
target_sources(zpc_3c_input INTERFACE ${ZENSIM_LIBRARY_3C_INPUT_INCLUDE_FILES})
target_link_libraries(zpc_3c_input INTERFACE zpc_3c_core)

# --- 3C Camera ---
set(ZENSIM_LIBRARY_3C_CAMERA_INCLUDE_FILES
  3c/camera/Camera.hpp
  3c/camera/CameraMode.hpp
  3c/camera/MatrixBuilders.hpp
  3c/camera/Frustum.hpp
  3c/camera/Shake.hpp
)
add_library(zpc_3c_camera INTERFACE)
target_sources(zpc_3c_camera INTERFACE ${ZENSIM_LIBRARY_3C_CAMERA_INCLUDE_FILES})
target_link_libraries(zpc_3c_camera INTERFACE zpc_3c_core)

# --- 3C Character ---
set(ZENSIM_LIBRARY_3C_CHARACTER_INCLUDE_FILES
  3c/character/CharacterState.hpp
  3c/character/MovementPipeline.hpp
  3c/character/GroundQuery.hpp
  3c/character/JumpMechanics.hpp
)
add_library(zpc_3c_character INTERFACE)
target_sources(zpc_3c_character INTERFACE ${ZENSIM_LIBRARY_3C_CHARACTER_INCLUDE_FILES})
target_link_libraries(zpc_3c_character INTERFACE zpc_3c_core zpc_3c_input)

# --- 3C Scenarios ---
set(ZENSIM_LIBRARY_3C_SCENARIOS_INCLUDE_FILES
  3c/scenarios/ThreeCScenarios.hpp
)
add_library(zpc_3c_scenarios INTERFACE)
target_sources(zpc_3c_scenarios INTERFACE ${ZENSIM_LIBRARY_3C_SCENARIOS_INCLUDE_FILES})
target_link_libraries(zpc_3c_scenarios INTERFACE
  zpc_canary_core zpc_3c_camera zpc_3c_character zpc_3c_input)
```

---

## 4. Frame Update Order

The 3C system processes in a strict deterministic order within a single frame:

```
┌──────────────────────────────────────────────────────────────┐
│                      FRAME N                                 │
│                                                              │
│  1. INPUT PHASE (zpc_3c_input)                              │
│     ├── Poll raw input from devices                         │
│     ├── Apply axis processing (dead-zone, curve, sensitivity)│
│     ├── Resolve action states (pressed, held, released)     │
│     ├── Push to input buffer ring                           │
│     └── Output: ActionSnapshot                              │
│                                                              │
│  2. CHARACTER PHASE (zpc_3c_character)                       │
│     ├── Read ActionSnapshot                                 │
│     ├── Compute MovementIntent from actions                 │
│     ├── Query ground state (IGroundQuery)                   │
│     ├── Update state machine (idle→walk→jump→fall→...)      │
│     ├── Apply acceleration/deceleration curves              │
│     ├── Handle jump mechanics (coyote, buffer, variable)    │
│     ├── Compute final velocity & position delta             │
│     └── Output: CharacterSnapshot (position, velocity,      │
│              rotation, state, tags)                          │
│                                                              │
│  3. CAMERA PHASE (zpc_3c_camera)                            │
│     ├── Read CharacterSnapshot (follow target)              │
│     ├── Read ActionSnapshot (camera-specific input: look)   │
│     ├── Update active CameraMode                            │
│     │   ├── Orbit: update azimuth/elevation/distance        │
│     │   ├── Follow: smooth-track target with dead-zone      │
│     │   ├── FPS: mouse-look with pitch clamp                │
│     │   └── Rail: advance along spline                      │
│     ├── Apply mode transitions (slerp blend)                │
│     ├── Apply shake/trauma                                  │
│     ├── Build view matrix (lookAt)                          │
│     ├── Build projection matrix (perspective)               │
│     └── Output: CameraOutput (view, projection, frustum)   │
│                                                              │
│  4. TELEMETRY PHASE (zpc_3c_core)                           │
│     ├── Measure frame timing                                │
│     ├── Record input-to-camera latency                      │
│     ├── Record character state transitions                  │
│     ├── Record camera smoothness (jitter metric)            │
│     └── Output: FrameTelemetry → ValidationRecord           │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### Phase Dependencies (DAG)

```
ActionSnapshot ──┬──→ CHARACTER ──→ CharacterSnapshot ──┐
                 │                                       │
                 └──→ CAMERA ←──────────────────────────┘
                         │
                         └──→ CameraOutput
```

Input feeds both Character and Camera. Character feeds Camera.
Camera never feeds back to Character (no circular dependency).

### Integration with ExecutionGraph (optional)

If the application uses ZPC's `ExecutionGraph`, the 3C phases map to `PassNode`s:

```
PassNode("3c_input")    → writes ActionSnapshot
PassNode("3c_character") → reads ActionSnapshot, writes CharacterSnapshot
PassNode("3c_camera")    → reads ActionSnapshot + CharacterSnapshot, writes CameraOutput
PassNode("3c_telemetry") → reads all snapshots (read-only)
```

The execution graph automatically enforces ordering via resource hazards.

---

## 5. Namespace Strategy

```cpp
namespace zs {
  // Math extensions added to zs:: directly (they extend ZPC's math)
  template <typename T>
  constexpr auto slerp(Rotation<T, 3> const& a, Rotation<T, 3> const& b, T t) -> Rotation<T, 3>;

  namespace threeC {
    // Domain types live here
    struct Camera { ... };
    struct CharacterState { ... };
    struct ActionMap { ... };
    struct FrameTelemetry { ... };
    // etc.
  }
}
```

Rationale: `zs::threeC` keeps 3C types scoped without polluting the top-level `zs`
namespace, while math extensions that are genuinely general-purpose go in `zs::`.
