# ZPC Gameplay Integration Guide

How to consume the Gameplay (Camera, Character, Control) system from an application.

---

## Table of Contents

- [Quick Start](#quick-start)
- [CMake Setup](#cmake-setup)
- [Minimal Example](#minimal-example)
- [Input Setup](#input-setup)
- [Camera Modes](#camera-modes)
- [Character Setup](#character-setup)
- [Full Pipeline](#full-pipeline)
- [Custom Camera Modes](#custom-camera-modes)
- [Tuning Parameters](#tuning-parameters)
- [Conventions](#conventions)
- [Troubleshooting](#troubleshooting)

---

## Quick Start

The fastest way to get Gameplay running is through `GameplayPipeline`, which wires
input, character, and camera together with a single `update()` call:

```cpp
#include "zensim/gameplay/GameplayPipeline.hpp"

using namespace zs;
using namespace zs::gameplay;

int main() {
    GameplayPipeline pipeline;
    pipeline.reset(Vec3f{0, 0, 0});

    // Game loop
    FrameContext frame = FrameContext::first(1.0f / 60.0f);
    for (;;) {
        ActionSnapshot input = /* poll your input system */;
        input.frame = frame;

        GroundInfo ground = GroundInfo::on_ground(pipeline.character.position);

        PipelineOutput out = pipeline.update(input, ground);

        // Use out.camera.view and out.camera.projection for rendering
        // Use out.character.position for world placement
        // Use out.telemetry for diagnostics

        frame = frame.next(1.0f / 60.0f);
    }
}
```

---

## CMake Setup

Link against the CMake target that matches your needs:

| Target | Use case |
|--------|----------|
| `zpc_gameplay_core` | Math extensions, `FrameContext`, quaternion utilities only |
| `zpc_gameplay_input` | Input system (action maps, axis processing, input buffer) |
| `zpc_gameplay_camera` | Camera modes, matrix builders, frustum, shake, rig |
| `zpc_gameplay_character` | Character state machine, locomotion, jump mechanics |
| `zpc_gameplay_integration` | Full pipeline — links all of the above |

All targets are `INTERFACE` libraries (header-only). No separate build step
is needed; linking the target adds the headers to your include path.

```cmake
# Full pipeline
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE zpc_gameplay_integration)

# Camera only (no character)
add_executable(cam_viewer viewer.cpp)
target_link_libraries(cam_viewer PRIVATE zpc_gameplay_camera)
```

### Dependency Graph

```
zpcbase
  └── zpc_gameplay_core
        ├── zpc_gameplay_input
        ├── zpc_gameplay_camera
        └── zpc_gameplay_character (also links zpc_gameplay_input)
              └── zpc_gameplay_integration (links camera + character + input)
```

---

## Minimal Example

### Camera Only

If you only need a camera (e.g., a level editor or model viewer), use the
camera module directly without character or pipeline:

```cpp
#include "zensim/gameplay/camera/CameraMode.hpp"
#include "zensim/gameplay/camera/MatrixBuilders.hpp"

using namespace zs;
using namespace zs::gameplay;

OrbitMode orbit;
orbit.config.distance = 10.0f;
orbit.config.smoothing = 8.0f;
orbit.reset();

FrameContext frame = FrameContext::first(1.0f / 60.0f);

// Each frame:
orbit.pivot = Vec3f{0, 0, 0};    // what we're looking at
orbit.yawInput   = mouseDeltaX;  // raw mouse delta
orbit.pitchInput = mouseDeltaY;
Camera cam = orbit.update(frame);
orbit.clear_input();

Mat4f view = view_from_camera(cam.position, cam.orientation);
Mat4f proj = perspective(cam.fovY, cam.aspectRatio, cam.nearPlane, cam.farPlane);

frame = frame.next(dt);
```

### Character Only

If you only need locomotion (e.g., a physics sandbox), use the character
module alone:

```cpp
#include "zensim/gameplay/character/CharacterState.hpp"

using namespace zs;
using namespace zs::gameplay;

CharacterStateMachine character;
character.config.runSpeed = 6.0f;
character.config.jumpVelocity = 12.0f;
character.reset(Vec3f{0, 0, 0});

FrameContext frame = FrameContext::first(1.0f / 60.0f);

// Each frame:
MovementIntent intent;
intent.direction = Vec3f{0, 0, 1};  // forward
intent.magnitude = 1.0f;
intent.wantsJump = spacePressed;

GroundInfo ground = GroundInfo::on_ground(character.position);
CharacterSnapshot snap = character.update(intent, ground, frame);

// snap.position  — new world position
// snap.state     — current locomotion state (idle, walk, run, etc.)
// snap.animTags  — bitmask for animation system
// snap.velocity  — current velocity vector

frame = frame.next(dt);
```

---

## Input Setup

The input system translates raw device state into abstract `ActionSnapshot`s.

### Action Bindings

```cpp
#include "zensim/gameplay/input/ActionMap.hpp"

using namespace zs::gameplay;

ActionMap actionMap = ActionMap::create();

// Bind a keyboard key to "jump"
ActionBinding jumpBind;
jumpBind.actionId = action_id("jump");
jumpBind.device = DeviceType::keyboard;
jumpBind.physicalId = KEY_SPACE;  // your key code
actionMap.addBinding(jumpBind);

// Poll each frame
RawInputState raw = /* read from OS/platform */;
ActionSnapshot snap = actionMap.update(raw, frame);
```

### Axis Processing

Raw analog inputs (gamepad sticks, mouse) go through a configurable pipeline:
dead-zone → response curve → sensitivity → invert.

```cpp
#include "zensim/gameplay/input/AxisProcessor.hpp"

using namespace zs::gameplay;

// Gamepad stick with 15% radial dead-zone and quadratic response
DualAxisConfig stickConfig = DualAxisConfig::gamepadStick();
Vec2f processed = process_dual_axis(rawX, rawY, stickConfig);

// Mouse look with sensitivity scaling
AxisConfig mouseConfig = AxisConfig::mouse(2.0f);  // 2x sensitivity
f32 look = process_axis(rawMouseDelta, mouseConfig);
```

### Input Buffer (Jump Buffering / Replay)

```cpp
#include "zensim/gameplay/input/InputBuffer.hpp"

using namespace zs::gameplay;

InputBuffer<16> buffer;  // 16-frame history

// Each frame:
buffer.push(snapshot);

// Check if jump was pressed within the last 6 frames
bool bufferedJump = buffer.wasPressed(action_id("jump"), 6);
```

---

## Camera Modes

Three built-in camera modes are provided. Each is independently usable
or can be managed by `CameraRig` for automatic transitions.

### Orbit Mode

Third-person orbit camera rotating around a pivot point.

```cpp
OrbitMode orbit;
orbit.config.distance    = 10.0f;
orbit.config.minDistance  = 2.0f;
orbit.config.maxDistance  = 50.0f;
orbit.config.minPitch    = -80.0f * k_deg2rad;
orbit.config.maxPitch    =  80.0f * k_deg2rad;
orbit.config.smoothing   = 8.0f;
orbit.reset();

// Per frame: set pivot, input, call update
orbit.pivot      = targetPosition;
orbit.yawInput   = mouseDX;
orbit.pitchInput = mouseDY;
orbit.zoomInput  = scrollWheel;
Camera cam = orbit.update(frame);
orbit.clear_input();
```

### Follow Mode

Spring-damped camera that follows behind a target with configurable offset.

```cpp
FollowMode follow;
follow.config.offset           = Vec3f{0, 3, -8};  // behind and above
follow.config.lookAtOffset     = Vec3f{0, 1, 0};    // look at head height
follow.config.positionSmoothing = 5.0f;
follow.config.rotationSmoothing = 8.0f;
follow.reset();

// Per frame:
follow.targetPosition    = characterPos;
follow.targetOrientation = characterRot;
Camera cam = follow.update(frame);
```

### FPS Mode

First-person camera locked to character head position.

```cpp
FpsMode fps;
fps.config.eyeOffset = Vec3f{0, 1.7f, 0};   // eye height
fps.config.minPitch  = -85.0f * k_deg2rad;
fps.config.maxPitch  =  85.0f * k_deg2rad;
fps.config.smoothing = 12.0f;
fps.reset(characterPos);

// Per frame:
fps.characterPosition = characterPos;
fps.yawInput   = mouseDX;
fps.pitchInput = mouseDY;
Camera cam = fps.update(frame);
fps.clear_input();

// Get the yaw quaternion for character rotation sync:
Quat4f charYaw = fps.yaw_quaternion();
```

### Camera Rig (Mode Manager)

`CameraRig` manages mode switching with smooth blending:

```cpp
CameraRig rig;
rig.transConfig.blendDuration  = 0.5f;  // 0.5s transition
rig.transConfig.blendSmoothing = 4.0f;

rig.activeMode = CameraModeId::orbit;

// Switch modes at runtime (blends automatically)
rig.setMode(CameraModeId::follow);

// Feed inputs and update each frame
rig.orbit.pivot = charPos;
rig.follow.targetPosition = charPos;
CameraOutput output = rig.update(frame);
rig.clearInputs();

// output.blendAlpha < 1.0 during transition
// output.blendFrom tells you the previous mode
```

### Camera Shake

Trauma-based shake with configurable decay:

```cpp
CameraShake shake;
shake.config.maxYaw     = 3.0f * k_deg2rad;
shake.config.maxPitch   = 3.0f * k_deg2rad;
shake.config.frequency  = 25.0f;
shake.config.decayRate  = 3.0f;

// On impact/explosion:
shake.addTrauma(0.5f);  // values clamped to [0, 1]

// Each frame (typically called via CameraRig automatically):
Camera shaken = shake.apply(camera, frame);
```

---

## Character Setup

### Configuration

All movement parameters are tunable at runtime:

```cpp
CharacterStateMachine character;
character.config.walkSpeed       = 2.0f;
character.config.runSpeed        = 5.0f;
character.config.sprintSpeed     = 8.0f;
character.config.acceleration    = 20.0f;
character.config.deceleration    = 15.0f;
character.config.gravity         = -20.0f;
character.config.jumpVelocity    = 10.0f;
character.config.coyoteTime      = 0.1f;   // 100ms grace after leaving edge
character.config.jumpBufferTime  = 0.1f;   // 100ms input buffer for jump
character.config.landRecoveryTime = 0.1f;  // 100ms landing lag
character.reset(Vec3f{0, 0, 0});
```

### State Machine

The character moves through these states:

```
idle → walk → run → sprint
  ↕                    ↕
jump_ascend → jump_apex → fall → land → idle
```

States are exposed via `LocomotionState` enum. Transitions happen
automatically based on velocity, ground contact, and input.

### Animation Tags

`CharacterSnapshot::animTags` is a bitmask you can query for animation:

```cpp
if (snap.animTags & AnimTag::grounded)     { /* play ground anims */ }
if (snap.animTags & AnimTag::airborne)     { /* play air anims */ }
if (snap.animTags & AnimTag::accelerating) { /* blend to accel anim */ }
if (snap.animTags & AnimTag::turning)      { /* play turn anim */ }
if (snap.animTags & AnimTag::landing)      { /* play land anim */ }
```

### Ground Detection

The Gameplay system does **not** own physics. You provide ground info each frame:

```cpp
// From your physics/raycast system:
GroundInfo ground;
ground.grounded = rayHit;
ground.normal   = hitNormal;
ground.point    = hitPoint;
ground.distance = hitDistance;
ground.slope    = angleBetweenNormalAndUp;

// Or use the convenience constructors:
GroundInfo ground = GroundInfo::on_ground(characterPos);
GroundInfo ground = GroundInfo::in_air(distToGround);
```

---

## Full Pipeline

`GameplayPipeline` is the recommended entry point for applications that need
camera + character + input working together.

### Setup

```cpp
#include "zensim/gameplay/GameplayPipeline.hpp"

using namespace zs;
using namespace zs::gameplay;

GameplayPipeline pipeline;

// Configure subsystems
pipeline.pipelineConfig.lookSensitivityX = 0.003f;
pipeline.pipelineConfig.lookSensitivityY = 0.003f;
pipeline.pipelineConfig.invertPitchInput = false;

pipeline.character.config.runSpeed = 6.0f;
pipeline.rig.transConfig.blendDuration = 0.3f;
pipeline.rig.activeMode = CameraModeId::follow;

pipeline.reset(Vec3f{0, 0, 0});
```

### Frame Loop

```cpp
FrameContext frame = FrameContext::first(1.0f / 60.0f);

while (running) {
    // 1. Build ActionSnapshot from your input system
    ActionSnapshot input = ActionSnapshot::empty(frame);
    input.lookAxis = Vec2f{mouseDX, mouseDY};
    input.moveAxis = Vec2f{moveX, moveY};  // WASD or stick

    if (spacePressed)
        input.addAction(ActionState{k_actionJump, ActionPhase::pressed, 1.0f, 0.0f});
    if (shiftHeld)
        input.addAction(ActionState{k_actionSprint, ActionPhase::held, 1.0f, shiftDuration});

    // 2. Get ground info from physics
    GroundInfo ground = queryGround(pipeline.character.position);

    // 3. Run the pipeline
    PipelineOutput out = pipeline.update(input, ground);

    // 4. Use results
    renderScene(out.camera.view, out.camera.projection);
    updateCharacterModel(out.character.position, out.character.rotation);

    // 5. Optional: check telemetry
    if (out.telemetry.stateTransitionsThisFrame > 0) {
        logStateChange(out.character.state);
    }

    // 6. Advance frame
    frame = frame.next(computedDt);
}
```

### Switching Camera Modes at Runtime

```cpp
// Player presses a "toggle view" key
if (viewTogglePressed) {
    CameraModeId current = pipeline.rig.activeMode;
    if (current == CameraModeId::follow)
        pipeline.rig.setMode(CameraModeId::orbit);
    else if (current == CameraModeId::orbit)
        pipeline.rig.setMode(CameraModeId::fps);
    else
        pipeline.rig.setMode(CameraModeId::follow);
}
```

### Adding Camera Shake

```cpp
// On explosion or hit event:
pipeline.rig.shake.addTrauma(0.6f);

// Shake is automatically applied during pipeline.update()
// Configure shake parameters:
pipeline.rig.shake.config.maxYaw    = 5.0f * k_deg2rad;
pipeline.rig.shake.config.frequency = 30.0f;
pipeline.rig.shake.config.decayRate = 2.0f;
```

---

## Custom Camera Modes

The camera rig supports `orbit`, `follow`, and `fps` out of the box.
For custom behavior (e.g., rail cameras, cinematic sequences), compute
a `Camera` struct directly and blend it with the rig output:

```cpp
Camera customCam;
customCam.position    = railPosition;
customCam.orientation = railOrientation;
customCam.fovY        = 45.0f * k_deg2rad;
customCam.nearPlane   = 0.1f;
customCam.farPlane    = 1000.0f;
customCam.aspectRatio = 16.0f / 9.0f;

// Manual blend with rig output
CameraOutput rigOut = pipeline.rig.update(frame);
f32 t = blendFactor;  // 0 = rig, 1 = custom

Camera blended;
blended.position    = lerp(rigOut.camera.position, customCam.position, t);
blended.orientation = slerp(rigOut.camera.orientation, customCam.orientation, t);
blended.fovY        = lerp(rigOut.camera.fovY, customCam.fovY, t);

Mat4f view = view_from_camera(blended.position, blended.orientation);
Mat4f proj = perspective(blended.fovY, blended.aspectRatio, blended.nearPlane, blended.farPlane);
```

---

## Tuning Parameters

### Character Tuning Guide

| Parameter | Effect | Typical Range |
|-----------|--------|---------------|
| `walkSpeed` | Speed at low stick deflection | 1.5 – 3.0 |
| `runSpeed` | Default movement speed | 4.0 – 7.0 |
| `sprintSpeed` | Speed while sprint button held | 7.0 – 12.0 |
| `acceleration` | How fast character reaches target speed | 10 – 30 |
| `deceleration` | How fast character stops | 10 – 25 |
| `gravity` | Downward acceleration (negative) | -15 to -30 |
| `jumpVelocity` | Initial upward velocity on jump | 8.0 – 15.0 |
| `coyoteTime` | Seconds after leaving edge where jump still works | 0.08 – 0.15 |
| `jumpBufferTime` | Seconds before landing where jump input is buffered | 0.08 – 0.15 |
| `turnSmoothing` | Rotation smoothing (higher = snappier) | 5 – 15 |

### Camera Tuning Guide

| Parameter | Effect | Typical Range |
|-----------|--------|---------------|
| Orbit `distance` | Distance from pivot | 5 – 20 |
| Orbit `smoothing` | Orbit movement smoothing | 4 – 12 |
| Follow `offset` | Camera position relative to target | {0, 2–4, -5 to -10} |
| Follow `positionSmoothing` | Follow lag (higher = tighter) | 3 – 10 |
| Follow `rotationSmoothing` | Look rotation lag | 5 – 15 |
| FPS `eyeOffset` | Eye height above character origin | {0, 1.5–1.8, 0} |
| FPS `smoothing` | Mouse look smoothing (higher = snappier) | 8 – 20 |
| Transition `blendDuration` | Mode switch blend time (seconds) | 0.2 – 1.0 |
| Shake `frequency` | Shake oscillation rate (Hz) | 15 – 40 |
| Shake `decayRate` | How fast trauma decays (per second) | 1.5 – 5.0 |

---

## Conventions

### Coordinate System

- **Right-handed, Y-up**
- Forward: `-Z` (camera default look direction)
- Right: `+X`
- Up: `+Y`

### Quaternion Layout

Quaternions use `(x, y, z, w)` storage order (indices 0, 1, 2, 3). Identity
quaternion is `{0, 0, 0, 1}`.

### Clip Space

Matrix builders produce **Vulkan-convention** clip space:
- Y-axis inverted (Y-down in NDC)
- Z range `[0, 1]`
- `perspective_reversed_z()` available for infinite far plane with reversed depth

### Units

- Distances: meters (recommended, not enforced)
- Angles: radians (use `k_deg2rad` / `k_rad2deg` for conversion)
- Time: seconds (`FrameContext::dt`)

---

## Troubleshooting

### Character falls through the ground

Ensure you are providing correct `GroundInfo` each frame. If `grounded` is
`false`, the character enters free-fall with gravity.

### Camera snaps on mode switch

Increase `TransitionConfig::blendDuration`. Default is 0.5s. Ensure
`setMode()` is called (not directly assigning `activeMode`), as `setMode()`
sets up the blend state.

### Mouse look feels laggy

Increase `FpsConfig::smoothing` (higher = less lag, more responsive). A
value of 0 disables smoothing entirely. Also check that you are passing
raw mouse deltas (not pre-smoothed values from the OS).

### Camera jitters at low frame rates

The math utilities (`damp`, `spring_damper`) are frame-rate-independent,
but very low dt values (< 0.001s) can cause precision issues. Clamp dt
to a reasonable minimum (e.g., 1/240s) before passing to the pipeline.

### Jump doesn't trigger

Check that your `ActionSnapshot` contains an action with id
`action_id("jump")` and phase `ActionPhase::pressed` on the frame the
button is pressed. The character system uses `justPressed()` which only
matches the `pressed` phase.

### Character state stuck in "land"

Land recovery time (`config.landRecoveryTime`) keeps the character in
the landing state briefly. Reduce it for snappier feel, or set to 0
to skip landing entirely.

### Build errors about missing includes

Ensure you are linking the correct CMake target. All include paths are
provided transitively through `zpcbase`. Do not add include directories
manually.
