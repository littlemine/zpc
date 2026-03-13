# Gameplay Module Decomposition & Frame Update Order

**Date:** 2026-03-13
**Branch:** `zpc_3c`

---

## 1. Module Hierarchy

```
zpc_gameplay_core          (shared types, math extensions, telemetry)
├── zpc_gameplay_input     (action maps, axis processing, input buffer)
├── zpc_gameplay_character (state machine, movement pipeline, ground query)
├── zpc_gameplay_camera    (camera struct, matrix builders, modes, shake)
└── zpc_gameplay_scenarios (canary scenarios, validation glue)
```

### Dependency Graph

```
zpcbase
  └─→ zpc_validation_core
        └─→ zpc_gameplay_core
              ├─→ zpc_gameplay_input       (depends on: zpc_gameplay_core)
              ├─→ zpc_gameplay_character   (depends on: zpc_gameplay_core, zpc_gameplay_input)
              ├─→ zpc_gameplay_camera      (depends on: zpc_gameplay_core)
              └─→ zpc_gameplay_scenarios   (depends on: zpc_canary_core, zpc_gameplay_camera,
                                             zpc_gameplay_character, zpc_gameplay_input)
```

**Key constraint:** Camera and Character do NOT depend on each other directly.
Integration happens at the application layer, where the frame loop wires character
output into camera input. This keeps the modules independently testable.

---

## 2. Directory Layout

```
include/zensim/
  gameplay/
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
      GameplayScenarios.hpp # Canary scenario descriptors for Gameplay validation
```

---

## 3. CMake Registration

Following the existing pattern in `include/zensim/CMakeLists.txt`:

```cmake
# --- Gameplay Core ---
set(ZENSIM_LIBRARY_GAMEPLAY_CORE_INCLUDE_FILES
  gameplay/Core.hpp
  gameplay/MathExtensions.hpp
  gameplay/Telemetry.hpp
)
add_library(zpc_gameplay_core INTERFACE)
target_sources(zpc_gameplay_core INTERFACE ${ZENSIM_LIBRARY_GAMEPLAY_CORE_INCLUDE_FILES})
target_link_libraries(zpc_gameplay_core INTERFACE zpc_validation_core)

# --- Gameplay Input ---
set(ZENSIM_LIBRARY_GAMEPLAY_INPUT_INCLUDE_FILES
  gameplay/input/ActionMap.hpp
  gameplay/input/AxisProcessor.hpp
  gameplay/input/InputBuffer.hpp
  gameplay/input/InputTypes.hpp
)
add_library(zpc_gameplay_input INTERFACE)
target_sources(zpc_gameplay_input INTERFACE ${ZENSIM_LIBRARY_GAMEPLAY_INPUT_INCLUDE_FILES})
target_link_libraries(zpc_gameplay_input INTERFACE zpc_gameplay_core)

# --- Gameplay Camera ---
set(ZENSIM_LIBRARY_GAMEPLAY_CAMERA_INCLUDE_FILES
  gameplay/camera/Camera.hpp
  gameplay/camera/CameraMode.hpp
  gameplay/camera/MatrixBuilders.hpp
  gameplay/camera/Frustum.hpp
  gameplay/camera/Shake.hpp
)
add_library(zpc_gameplay_camera INTERFACE)
target_sources(zpc_gameplay_camera INTERFACE ${ZENSIM_LIBRARY_GAMEPLAY_CAMERA_INCLUDE_FILES})
target_link_libraries(zpc_gameplay_camera INTERFACE zpc_gameplay_core)

# --- Gameplay Character ---
set(ZENSIM_LIBRARY_GAMEPLAY_CHARACTER_INCLUDE_FILES
  gameplay/character/CharacterState.hpp
  gameplay/character/MovementPipeline.hpp
  gameplay/character/GroundQuery.hpp
  gameplay/character/JumpMechanics.hpp
)
add_library(zpc_gameplay_character INTERFACE)
target_sources(zpc_gameplay_character INTERFACE ${ZENSIM_LIBRARY_GAMEPLAY_CHARACTER_INCLUDE_FILES})
target_link_libraries(zpc_gameplay_character INTERFACE zpc_gameplay_core zpc_gameplay_input)

# --- Gameplay Scenarios ---
set(ZENSIM_LIBRARY_GAMEPLAY_SCENARIOS_INCLUDE_FILES
  gameplay/scenarios/GameplayScenarios.hpp
)
add_library(zpc_gameplay_scenarios INTERFACE)
target_sources(zpc_gameplay_scenarios INTERFACE ${ZENSIM_LIBRARY_GAMEPLAY_SCENARIOS_INCLUDE_FILES})
target_link_libraries(zpc_gameplay_scenarios INTERFACE
  zpc_canary_core zpc_gameplay_camera zpc_gameplay_character zpc_gameplay_input)
```

---

## 4. Frame Update Order

The Gameplay system processes in a strict deterministic order within a single frame:

```
┌──────────────────────────────────────────────────────────────┐
│                      FRAME N                                 │
│                                                              │
│  1. INPUT PHASE (zpc_gameplay_input)                              │
│     ├── Poll raw input from devices                         │
│     ├── Apply axis processing (dead-zone, curve, sensitivity)│
│     ├── Resolve action states (pressed, held, released)     │
│     ├── Push to input buffer ring                           │
│     └── Output: ActionSnapshot                              │
│                                                              │
│  2. CHARACTER PHASE (zpc_gameplay_character)                       │
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
│  3. CAMERA PHASE (zpc_gameplay_camera)                            │
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
│  4. TELEMETRY PHASE (zpc_gameplay_core)                           │
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

If the application uses ZPC's `ExecutionGraph`, the Gameplay phases map to `PassNode`s:

```
PassNode("gameplay_input")    → writes ActionSnapshot
PassNode("gameplay_character") → reads ActionSnapshot, writes CharacterSnapshot
PassNode("gameplay_camera")    → reads ActionSnapshot + CharacterSnapshot, writes CameraOutput
PassNode("gameplay_telemetry") → reads all snapshots (read-only)
```

The execution graph automatically enforces ordering via resource hazards.

---

## 5. Namespace Strategy

```cpp
namespace zs {
  // Math extensions added to zs:: directly (they extend ZPC's math)
  template <typename T>
  constexpr auto slerp(Rotation<T, 3> const& a, Rotation<T, 3> const& b, T t) -> Rotation<T, 3>;

  namespace gameplay {
    // Domain types live here
    struct Camera { ... };
    struct CharacterState { ... };
    struct ActionMap { ... };
    struct FrameTelemetry { ... };
    // etc.
  }
}
```

Rationale: `zs::gameplay` keeps Gameplay types scoped without polluting the top-level `zs`
namespace, while math extensions that are genuinely general-purpose go in `zs::`.
