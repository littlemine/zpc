# ZPC Gameplay API Reference

Complete API reference for the ZPC Gameplay (Camera, Character, Control) system.

## Table of Contents

- [Namespace Layout](#namespace-layout)
- [Core Types](#core-types)
- [Math Extensions](#math-extensions)
- [Input System](#input-system)
- [Camera System](#camera-system)
- [Character System](#character-system)
- [Integration Pipeline](#integration-pipeline)
- [Telemetry](#telemetry)

---

## Namespace Layout

| Namespace | Contents |
|-----------|----------|
| `zs::gameplay` | All Gameplay domain types, camera modes, character state, input, pipeline |
| `zs::` | Math extensions (slerp, damp, spring_damper, lerp, remap) |

CMake targets:

| Target | Links | Headers |
|--------|-------|---------|
| `zpc_gameplay_core` | `zpcbase` | Core.hpp, MathExtensions.hpp, Telemetry.hpp |
| `zpc_gameplay_input` | `zpc_gameplay_core` | InputTypes.hpp, AxisProcessor.hpp, ActionMap.hpp, InputBuffer.hpp |
| `zpc_gameplay_camera` | `zpc_gameplay_core` | Camera.hpp, MatrixBuilders.hpp, Frustum.hpp, CameraMode.hpp, CameraShake.hpp, CameraRig.hpp |
| `zpc_gameplay_character` | `zpc_gameplay_core`, `zpc_gameplay_input` | CharacterState.hpp |
| `zpc_gameplay_integration` | `zpc_gameplay_camera`, `zpc_gameplay_character`, `zpc_gameplay_input` | GameplayPipeline.hpp |

---

## Core Types

**Header:** `zensim/gameplay/Core.hpp`

### Type Aliases

```cpp
using Vec2f  = zs::vec<f32, 2>;
using Vec3f  = zs::vec<f32, 3>;
using Vec4f  = zs::vec<f32, 4>;
using Mat3f  = zs::vec<f32, 3, 3>;
using Mat4f  = zs::vec<f32, 4, 4>;
using Quat4f = zs::vec<f32, 4>;  // (x, y, z, w) layout
```

### Constants

| Name | Value | Description |
|------|-------|-------------|
| `k_pi` | 3.14159... | Pi as f32 |
| `k_half_pi` | 1.57079... | Half pi |
| `k_two_pi` | 6.28318... | Two pi |
| `k_deg2rad` | pi/180 | Degree to radian conversion |
| `k_rad2deg` | 180/pi | Radian to degree conversion |
| `k_epsilon` | 1e-6f | Near-zero threshold |

### `FrameContext`

Per-frame timing and sequencing, passed to every update function.

```cpp
struct FrameContext {
    f64 time;          // Absolute time since start (seconds)
    f32 dt;            // Delta time this frame (seconds)
    u64 frameNumber;   // Monotonically increasing frame counter

    static constexpr FrameContext first(f32 dt_) noexcept;
    constexpr FrameContext next(f32 dt_) const noexcept;
};
```

### Quaternion Utilities

```cpp
constexpr Quat4f identity_quat() noexcept;
constexpr Quat4f quat_from_axis_angle(Vec3f axis, f32 angle) noexcept;
constexpr Quat4f quat_conjugate(Quat4f const& q) noexcept;
constexpr Quat4f quat_multiply(Quat4f const& a, Quat4f const& b) noexcept;
constexpr Vec3f  quat_rotate(Quat4f const& q, Vec3f const& v) noexcept;
```

---

## Math Extensions

**Header:** `zensim/gameplay/MathExtensions.hpp`  
**Namespace:** `zs::`

### `slerp`

Spherical linear interpolation between unit quaternions.

```cpp
template <typename T>
constexpr vec<T, 4> slerp(vec<T, 4> const& a, vec<T, 4> const& b, T t) noexcept;
```

Handles antipodal case (takes shortest arc). Falls back to nlerp for near-parallel quaternions.

### `damp`

Frame-rate-independent exponential damping (smoothing).

```cpp
template <typename T, int N>
constexpr vec<T, N> damp(vec<T, N> const& current, vec<T, N> const& target,
                         T smoothing, T dt) noexcept;

template <typename T>
constexpr T damp(T current, T target, T smoothing, T dt) noexcept;
```

Formula: `current + (target - current) * (1 - exp(-smoothing * dt))`

### `spring_damper`

Semi-implicit Euler integration of a critically damped spring.

```cpp
template <typename T, int N>
constexpr void spring_damper(vec<T, N>& position, vec<T, N>& velocity,
                             vec<T, N> const& target,
                             T omega, T zeta, T dt) noexcept;
```

- `omega`: Angular frequency (higher = stiffer)
- `zeta`: Damping ratio (1.0 = critically damped)

### `remap` / `remap_unclamped`

```cpp
template <typename T>
constexpr T remap(T value, T inMin, T inMax, T outMin, T outMax) noexcept;

template <typename T>
constexpr T remap_unclamped(T value, T inMin, T inMax, T outMin, T outMax) noexcept;
```

### `lerp`

```cpp
template <typename T>
constexpr T lerp(T a, T b, T t) noexcept;

template <typename T, int N>
constexpr vec<T, N> lerp(vec<T, N> const& a, vec<T, N> const& b, T t) noexcept;
```

---

## Input System

### InputTypes

**Header:** `zensim/gameplay/input/InputTypes.hpp`

```cpp
enum class DeviceType : u8 { keyboard, mouse, gamepad, touch };
enum class ActionPhase : u8 { none, pressed, held, released };

using ActionId = u32;
constexpr ActionId action_id(const char* str) noexcept;  // FNV-1a hash

struct ActionState {
    ActionId id;
    ActionPhase phase;
    f32 value;
    f32 heldDuration;

    constexpr bool justPressed() const noexcept;
    constexpr bool isActive() const noexcept;
    constexpr bool justReleased() const noexcept;
};

struct ActionSnapshot {
    ActionState actions[k_maxActions];  // k_maxActions = 64
    u32 actionCount;
    Vec2f lookAxis;   // x=yaw, y=pitch
    Vec2f moveAxis;   // x=strafe, y=forward
    FrameContext frame;

    static constexpr ActionSnapshot empty(FrameContext ctx) noexcept;
    constexpr ActionState const* find(ActionId id) const noexcept;
    constexpr bool justPressed(ActionId id) const noexcept;
    constexpr bool isActive(ActionId id) const noexcept;
    constexpr bool addAction(ActionState state) noexcept;
};
```

### AxisProcessor

**Header:** `zensim/gameplay/input/AxisProcessor.hpp`

Transforms raw axis values through: dead-zone -> response curve -> sensitivity -> invert.

```cpp
enum class ResponseCurve : u8 { linear, quadratic, cubic, custom };

struct AxisConfig {
    f32 deadZone, sensitivity;
    ResponseCurve curve;
    f32 exponent;
    bool invert;

    static constexpr AxisConfig defaults() noexcept;
    static constexpr AxisConfig gamepad() noexcept;    // 15% dead-zone, quadratic
    static constexpr AxisConfig mouse(f32 sens = 1.0f) noexcept;
};

struct DualAxisConfig {
    AxisConfig xConfig, yConfig;
    bool radialDeadZone;  // circular dead-zone for gamepads

    static constexpr DualAxisConfig defaults() noexcept;
    static constexpr DualAxisConfig gamepadStick() noexcept;
    static constexpr DualAxisConfig mouseLook(f32 sens = 1.0f) noexcept;
};

constexpr f32   process_axis(f32 raw, AxisConfig const& config) noexcept;
constexpr Vec2f process_dual_axis(f32 rawX, f32 rawY, DualAxisConfig const& config) noexcept;
```

### ActionMap

**Header:** `zensim/gameplay/input/ActionMap.hpp`

Maps physical inputs to abstract actions with pressed/held/released lifecycle tracking.

```cpp
struct ActionMap {
    static constexpr ActionMap create() noexcept;
    constexpr bool addBinding(ActionBinding binding) noexcept;
    constexpr void removeBindings(ActionId id) noexcept;
    constexpr ActionSnapshot update(RawInputState const& raw, FrameContext const& frame) noexcept;
};
```

### InputBuffer

**Header:** `zensim/gameplay/input/InputBuffer.hpp`

Ring-buffer of ActionSnapshot history for replay and jump buffering.

```cpp
template <int Size = 16>
struct InputBuffer {
    constexpr void push(ActionSnapshot const& snap) noexcept;
    constexpr ActionSnapshot const& current() const noexcept;
    constexpr ActionSnapshot const& ago(int n) const noexcept;
    constexpr bool wasPressed(ActionId id, int frames) const noexcept;
    constexpr bool heldFor(ActionId id, int frames) const noexcept;
};
```

---

## Camera System

### Camera / CameraOutput

**Header:** `zensim/gameplay/camera/Camera.hpp`

```cpp
enum class CameraModeId : u8 { orbit, follow, fps, rail, free_fly, custom };

struct Camera {
    Vec3f  position;      // World-space eye position
    Quat4f orientation;   // Unit quaternion (identity = look along -Z)
    f32    fovY;          // Vertical FOV (radians), default 60 deg
    f32    nearPlane;     // Near clip (default 0.1)
    f32    farPlane;      // Far clip (default 1000)
    f32    aspectRatio;   // Width/height (default 16:9)

    constexpr Vec3f forward() const noexcept;
    constexpr Vec3f right() const noexcept;
    constexpr Vec3f up() const noexcept;
};

struct CameraOutput {
    Camera       camera;
    Mat4f        view;         // World -> camera
    Mat4f        projection;   // Camera -> clip
    CameraModeId activeMode;
    CameraModeId blendFrom;
    f32          blendAlpha;   // 1.0 = fully in activeMode
};
```

### MatrixBuilders

**Header:** `zensim/gameplay/camera/MatrixBuilders.hpp`

Conventions: right-handed, Y-up, Vulkan clip space (Y-down, Z in [0,1]).

```cpp
constexpr Mat4f look_at(Vec3f eye, Vec3f target, Vec3f worldUp) noexcept;
constexpr Mat4f perspective(f32 fovY, f32 aspect, f32 zNear, f32 zFar) noexcept;
constexpr Mat4f perspective_reversed_z(f32 fovY, f32 aspect, f32 zNear, f32 zFar) noexcept;
constexpr Mat4f orthographic(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar) noexcept;
constexpr Mat4f view_from_camera(Vec3f position, Quat4f orientation) noexcept;
```

### Frustum

**Header:** `zensim/gameplay/camera/Frustum.hpp`

View-frustum culling via Gribb-Hartmann plane extraction.

```cpp
struct Frustum {
    Vec4f planes[6];  // (nx, ny, nz, d) per plane

    static constexpr Frustum from_view_projection(Mat4f const& vp) noexcept;
    constexpr bool contains_point(Vec3f const& p) const noexcept;
    constexpr SphereResult contains_sphere(Vec3f const& center, f32 radius) const noexcept;
};
```

### Camera Modes

**Header:** `zensim/gameplay/camera/CameraMode.hpp`

#### OrbitMode

Third-person orbit camera around a pivot point.

```cpp
struct OrbitConfig {
    f32 distance, minDistance, maxDistance;
    f32 minPitch, maxPitch;
    f32 yawSpeed, pitchSpeed, zoomSpeed, smoothing;
};

struct OrbitMode {
    OrbitConfig config;
    Vec3f pivot;                   // set before update
    f32 yawInput, pitchInput, zoomInput;  // set before update

    constexpr void reset() noexcept;
    constexpr Camera update(FrameContext const& ctx) noexcept;
    constexpr void clear_input() noexcept;
};
```

#### FollowMode

Spring-damped follow camera behind a target.

```cpp
struct FollowConfig {
    Vec3f offset;          // {0, 2, -5} default
    Vec3f lookAtOffset;    // {0, 1, 0} default
    f32 positionSmoothing, rotationSmoothing;
};

struct FollowMode {
    FollowConfig config;
    Vec3f  targetPosition;      // set before update
    Quat4f targetOrientation;   // set before update

    constexpr void reset() noexcept;
    constexpr Camera update(FrameContext const& ctx) noexcept;
};
```

#### FpsMode

First-person camera locked to character head.

```cpp
struct FpsConfig {
    Vec3f eyeOffset;     // {0, 1.7, 0} default
    f32 minPitch, maxPitch;
    f32 yawSpeed, pitchSpeed, smoothing;
};

struct FpsMode {
    FpsConfig config;
    Vec3f characterPosition;  // set before update
    f32 yawInput, pitchInput; // set before update

    constexpr void reset(Vec3f charPos) noexcept;
    constexpr Camera update(FrameContext const& ctx) noexcept;
    constexpr void clear_input() noexcept;
    constexpr Quat4f yaw_quaternion() const noexcept;  // for character sync
};
```

### CameraShake

**Header:** `zensim/gameplay/camera/CameraShake.hpp`

Trauma-based camera shake with hash noise.

```cpp
struct ShakeConfig {
    f32 maxYaw, maxPitch, maxRoll;      // max rotational offsets at trauma=1
    f32 maxOffsetX, maxOffsetY, maxOffsetZ;  // max positional offsets
    f32 frequency;   // oscillation Hz (default 25)
    f32 decayRate;   // trauma decay speed (default 3)
};

struct CameraShake {
    ShakeConfig config;
    f32 trauma;    // [0, 1]
    f32 elapsed;

    constexpr void addTrauma(f32 amount) noexcept;
    constexpr void reset() noexcept;
    constexpr f32 intensity() const noexcept;  // trauma^2
    constexpr Camera apply(Camera cam, FrameContext const& ctx) noexcept;
};
```

### CameraRig

**Header:** `zensim/gameplay/camera/CameraRig.hpp`

Mode manager with smooth transitions and blending.

```cpp
struct TransitionConfig {
    f32 blendDuration;    // seconds (default 0.5)
    f32 blendSmoothing;   // smoothstep factor (default 4)
};

struct CameraRig {
    OrbitMode   orbit;
    FollowMode  follow;
    FpsMode     fps;
    CameraShake shake;
    TransitionConfig transConfig;
    CameraModeId activeMode;   // current mode

    constexpr void setMode(CameraModeId newMode) noexcept;
    constexpr CameraOutput update(FrameContext const& ctx) noexcept;
    constexpr void clearInputs() noexcept;
};
```

---

## Character System

**Header:** `zensim/gameplay/character/CharacterState.hpp`

### LocomotionState

```cpp
enum class LocomotionState : u8 {
    idle, walk, run, sprint,
    jump_ascend, jump_apex, fall, land,
    count
};
```

### AnimTag

Bitmask flags for animation system integration:

```cpp
namespace AnimTag {
    constexpr u32 grounded, airborne, accelerating, decelerating,
                  turning, landing, jump_rising, jump_falling;
}
```

### GroundInfo

```cpp
struct GroundInfo {
    bool grounded;
    Vec3f normal, point;
    f32 distance, slope;

    static constexpr GroundInfo on_ground(Vec3f pos, Vec3f norm = {0,1,0}) noexcept;
    static constexpr GroundInfo in_air(f32 dist = 1.0f) noexcept;
};
```

### MovementIntent

```cpp
struct MovementIntent {
    Vec3f direction;   // normalized or zero
    f32   magnitude;   // [0, 1]
    bool  wantsJump, wantsSprint;

    static constexpr MovementIntent none() noexcept;
    constexpr bool hasMovement() const noexcept;
};
```

### CharacterSnapshot

Per-frame output of the character system.

```cpp
struct CharacterSnapshot {
    Vec3f position, velocity;
    Quat4f rotation;
    LocomotionState state, previousState;
    f32 timeInState, speed;
    u32 stateTransitionCount, animTags;
    GroundInfo ground;
};
```

### CharacterConfig

```cpp
struct CharacterConfig {
    f32 walkSpeed{2}, runSpeed{5}, sprintSpeed{8};
    f32 acceleration{20}, deceleration{15}, airAcceleration{5};
    f32 turnSmoothing{10};
    f32 gravity{-20}, jumpVelocity{10}, jumpCutMultiplier{0.5};
    f32 coyoteTime{0.1}, jumpBufferTime{0.1}, landRecoveryTime{0.1};
    f32 apexThreshold{1}, walkThreshold{0.5};
    f32 maxSlopeAngle{50 * k_deg2rad};
};
```

### CharacterStateMachine

```cpp
struct CharacterStateMachine {
    CharacterConfig config;
    Vec3f position, velocity;
    Quat4f rotation;
    LocomotionState currentState;

    constexpr void reset(Vec3f pos) noexcept;
    constexpr CharacterSnapshot update(MovementIntent const& intent,
                                       GroundInfo const& ground,
                                       FrameContext const& ctx) noexcept;
};
```

Features: coyote time, jump buffering, variable-height jump (cut on release), gravity, ground snapping, rotation smoothing toward movement direction.

---

## Integration Pipeline

**Header:** `zensim/gameplay/GameplayPipeline.hpp`

### Standard Action IDs

```cpp
inline constexpr ActionId k_actionJump   = action_id("jump");
inline constexpr ActionId k_actionSprint = action_id("sprint");
```

### PipelineConfig

```cpp
struct PipelineConfig {
    f32 lookSensitivityX{0.003f};
    f32 lookSensitivityY{0.003f};
    bool invertPitchInput{false};
};
```

### PipelineOutput

```cpp
struct PipelineOutput {
    CharacterSnapshot character;
    CameraOutput      camera;
    FrameTelemetry    telemetry;
};
```

### GameplayPipeline

Top-level entry point for the Gameplay system.

```cpp
struct GameplayPipeline {
    PipelineConfig        pipelineConfig;
    CharacterStateMachine character;
    CameraRig             rig;

    constexpr void reset(Vec3f startPosition) noexcept;
    constexpr PipelineOutput update(ActionSnapshot const& input,
                                    GroundInfo const& ground) noexcept;
};
```

**Frame update order:** INPUT -> CHARACTER -> CAMERA -> TELEMETRY

---

## Telemetry

**Header:** `zensim/gameplay/Telemetry.hpp`

```cpp
struct FrameTelemetry {
    f64 inputPhaseMs, characterPhaseMs, cameraPhaseMs, totalPhaseMs;
    f32 inputToScreenLatencyMs, cameraJitterPx, cameraSmoothness;
    u32 stateTransitionsThisFrame;
    f32 velocityMagnitude;
    u64 frameNumber;

    static constexpr FrameTelemetry zero(u64 frame) noexcept;
};
```

Quality gate thresholds:

| Metric | Threshold |
|--------|-----------|
| `totalPhaseMs` | ≤ 2.0 ms |
| `inputPhaseMs` | ≤ 0.2 ms |
| `characterPhaseMs` | ≤ 0.5 ms |
| `cameraPhaseMs` | ≤ 0.5 ms |
| `inputToScreenLatencyMs` | ≤ 16.67 ms |
| `cameraJitterPx` | ≤ 0.5 px RMS |
