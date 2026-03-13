# 3C Shared Data Model

**Date:** 2026-03-13
**Branch:** `zpc_3c`

This document defines the minimal shared data types that flow between the 3C modules.
These are the "wire types" — the structs that cross module boundaries at each frame.

---

## 1. Core Types (`3c/Core.hpp`)

```cpp
#pragma once
#include "zensim/math/Vec.h"
#include "zensim/math/Rotation.hpp"
#include "zensim/TypeAlias.hpp"

namespace zs::threeC {

  // Shorthand aliases for 3C domain
  using Vec2f = zs::vec<f32, 2>;
  using Vec3f = zs::vec<f32, 3>;
  using Vec4f = zs::vec<f32, 4>;
  using Mat4f = zs::vec<f32, 4, 4>;
  using Quatf = zs::Rotation<f32, 3>;  // quaternion

  /// Per-frame context passed to all 3C update phases
  struct FrameContext {
    f64 time;           // absolute time in seconds since start
    f32 dt;             // delta time this frame (seconds)
    u64 frameNumber;    // monotonically increasing frame counter
  };

}  // namespace zs::threeC
```

---

## 2. Input Types (`3c/input/InputTypes.hpp`)

```cpp
#pragma once
#include "zensim/3c/Core.hpp"
#include "zensim/types/SmallVector.hpp"

namespace zs::threeC {

  /// Identifies an input device type
  enum class DeviceType : u8 {
    keyboard,
    mouse,
    gamepad,
    touch,
  };

  /// Unique action identifier (compile-time or hashed string)
  using ActionId = u32;

  /// Action state for a single frame
  enum class ActionPhase : u8 {
    none,       // not active
    pressed,    // just pressed this frame
    held,       // held from previous frame
    released,   // just released this frame
  };

  /// Single axis value after processing (dead-zone, curve, sensitivity applied)
  struct AxisValue {
    f32 raw;        // raw device value [-1, 1]
    f32 processed;  // after dead-zone + response curve
  };

  /// Snapshot of a single action's state
  struct ActionState {
    ActionId id;
    ActionPhase phase;
    f32 value;          // 0 or 1 for digital; continuous for analog
    f32 heldDuration;   // seconds held (0 if not held)
  };

  /// Complete input snapshot for one frame (output of input phase)
  struct ActionSnapshot {
    static constexpr u32 MAX_ACTIONS = 64;
    ActionState actions[MAX_ACTIONS];
    u32 actionCount;
    Vec2f lookAxis;      // camera look input (processed)
    Vec2f moveAxis;      // movement input (processed)
    FrameContext frame;
  };

}  // namespace zs::threeC
```

---

## 3. Character Types (`3c/character/CharacterState.hpp`)

```cpp
#pragma once
#include "zensim/3c/Core.hpp"

namespace zs::threeC {

  /// Character locomotion states
  enum class LocomotionState : u8 {
    idle,
    walk,
    run,
    sprint,
    jump_ascend,
    jump_apex,
    fall,
    land,
    // extensible via custom states
  };

  /// Movement intent computed from input (intermediate representation)
  struct MovementIntent {
    Vec3f direction;    // world-space desired direction (normalized, or zero)
    f32 magnitude;      // [0, 1] intensity (from analog stick or key combo)
    bool wantsJump;     // jump requested this frame
    bool wantsSprint;   // sprint modifier active
  };

  /// Ground detection result (returned by IGroundQuery)
  struct GroundInfo {
    bool grounded;       // standing on surface
    Vec3f normal;        // surface normal (up if not grounded)
    Vec3f point;         // contact point
    f32 distance;        // distance to ground (0 if grounded)
    f32 slope;           // angle from vertical in radians
  };

  /// Per-frame output of the character system
  struct CharacterSnapshot {
    Vec3f position;
    Vec3f velocity;
    Quatf rotation;
    LocomotionState state;
    LocomotionState previousState;
    f32 timeInState;         // seconds in current state
    u32 stateTransitionCount; // total transitions this session

    // Tags for animation system (bitmask)
    u32 animTags;

    // Ground info
    GroundInfo ground;
  };

}  // namespace zs::threeC
```

---

## 4. Camera Types (`3c/camera/Camera.hpp`)

```cpp
#pragma once
#include "zensim/3c/Core.hpp"

namespace zs::threeC {

  /// Camera mode identifier
  enum class CameraModeId : u8 {
    orbit,
    follow,
    fps,
    rail,
    free_fly,
    custom,
  };

  /// Core camera state
  struct Camera {
    Vec3f position;
    Quatf orientation;
    f32 fovY;       // vertical field of view in radians
    f32 nearPlane;
    f32 farPlane;
    f32 aspectRatio; // width / height
  };

  /// Per-frame output of the camera system
  struct CameraOutput {
    Camera camera;          // final camera state
    Mat4f view;             // view matrix (lookAt)
    Mat4f projection;       // projection matrix (perspective, Vulkan clip)
    CameraModeId activeMode;
    CameraModeId blendFrom; // mode blending from (if transitioning)
    f32 blendAlpha;         // transition progress [0, 1]
  };

  /// Frustum representation (6 planes, each as Vec4f: normal.xyz + distance.w)
  struct Frustum {
    Vec4f planes[6];  // left, right, bottom, top, near, far

    /// Test if a sphere is inside or intersecting the frustum
    bool containsSphere(Vec3f center, f32 radius) const;

    /// Test if a point is inside the frustum
    bool containsPoint(Vec3f point) const;
  };

}  // namespace zs::threeC
```

---

## 5. Telemetry Types (`3c/Telemetry.hpp`)

```cpp
#pragma once
#include "zensim/3c/Core.hpp"

namespace zs::threeC {

  /// Per-frame telemetry collected by the 3C system
  struct FrameTelemetry {
    // Timing
    f64 inputPhaseMs;       // input phase duration
    f64 characterPhaseMs;   // character phase duration
    f64 cameraPhaseMs;      // camera phase duration
    f64 totalPhaseMs;       // sum of above

    // Quality metrics
    f32 inputToScreenLatencyMs;  // input event to final camera output
    f32 cameraJitterPx;          // frame-to-frame camera position jitter (pixels)
    f32 cameraSmoothness;        // 1.0 = perfectly smooth, 0.0 = maximum jitter

    // Character
    u32 stateTransitionsThisFrame;
    f32 velocityMagnitude;

    // Frame info
    u64 frameNumber;
  };

  /// How FrameTelemetry maps to ValidationRecord fields:
  ///
  ///   "3c.total_phase_ms"         → totalPhaseMs     (threshold: ≤ 2.0)
  ///   "3c.input_latency_ms"      → inputToScreenLatencyMs (threshold: ≤ 16.67)
  ///   "3c.camera_jitter_px_rms"  → cameraJitterPx   (threshold: ≤ 0.5)
  ///   "3c.camera_phase_ms"       → cameraPhaseMs    (threshold: ≤ 0.5)
  ///   "3c.character_phase_ms"    → characterPhaseMs  (threshold: ≤ 0.5)

}  // namespace zs::threeC
```

---

## 6. Type Flow Diagram

```
Raw Input (device)
    │
    ▼
┌──────────────┐    ActionSnapshot    ┌──────────────────┐   CharacterSnapshot
│  Input Phase │ ──────────────────→ │  Character Phase  │ ─────────────────┐
└──────────────┘         │            └──────────────────┘                   │
                         │                                                   ▼
                         │            ┌──────────────────┐   CameraOutput  ┌───────────┐
                         └──────────→ │   Camera Phase   │ ──────────────→ │ Renderer  │
                                      └──────────────────┘                 └───────────┘
                                              │
                                              ▼
                                      FrameTelemetry → ValidationRecord
```

---

## 7. Design Decisions

1. **Fixed-size arrays over dynamic allocation** — `ActionSnapshot::actions[64]` uses
   a fixed array to avoid heap allocation in the hot path. 64 simultaneous actions is
   more than sufficient for any realistic binding set.

2. **Quaternion as rotation representation** — all rotations use `zs::Rotation<f32, 3>`
   (quaternion). Euler angles are never stored; they are computed on demand for display.

3. **World-space coordinates** — all positions, velocities, and directions are in
   world space. Camera-relative movement is computed in the character phase, not stored.

4. **Separation of raw vs. processed input** — `AxisValue` carries both raw and processed
   values so validation scenarios can verify the processing pipeline.

5. **Animation tags as bitmask** — `animTags` is a simple `u32` bitmask. The 3C system
   sets tags; the animation system consumes them. No animation logic lives in 3C.

6. **Telemetry maps directly to ValidationRecord** — each telemetry field has a named
   mapping to a `ValidationRecord` ID, enabling direct plugging into ZPC's validation
   framework without adapter code.
