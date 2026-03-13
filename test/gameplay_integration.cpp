/// @file gameplay_integration.cpp
/// @brief Integration tests for Phase 5: FpsMode, CameraShake, CameraRig,
///        GameplayPipeline, mode transitions, and full pipeline wiring.

#include <cassert>
#include <cstdio>
#include <cmath>

#include "zensim/gameplay/GameplayPipeline.hpp"

using namespace zs;
using namespace zs::gameplay;

// ── Helpers ──────────────────────────────────────────────────────────────

static constexpr f32 kTol = 1e-3f;

static f32 fabsf_(f32 x) { return x < 0.0f ? -x : x; }

static f32 vec3_dist(Vec3f const& a, Vec3f const& b) {
  f32 dx = a(0) - b(0), dy = a(1) - b(1), dz = a(2) - b(2);
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

static f32 quat_dot(Quat4f const& a, Quat4f const& b) {
  return a(0)*b(0) + a(1)*b(1) + a(2)*b(2) + a(3)*b(3);
}

/// Angle between two quaternions (radians).
static f32 quat_angle(Quat4f const& a, Quat4f const& b) {
  f32 d = quat_dot(a, b);
  if (d < 0.0f) d = -d;
  if (d > 1.0f) d = 1.0f;
  return 2.0f * std::acos(d);
}

#define CHECK(cond, msg)                              \
  do {                                                \
    if (!(cond)) {                                    \
      fprintf(stderr, "FAIL: %s (%s:%d)\n",           \
              msg, __FILE__, __LINE__);               \
      return 1;                                       \
    }                                                 \
  } while(0)

#define CHECKF(val, expected, tol, msg)               \
  do {                                                \
    if (fabsf_((val) - (expected)) > (tol)) {         \
      fprintf(stderr, "FAIL: %s  got=%f expected=%f (%s:%d)\n", \
              msg, (double)(val), (double)(expected), \
              __FILE__, __LINE__);                    \
      return 1;                                       \
    }                                                 \
  } while(0)

// ═══════════════════════════════════════════════════════════════════════
//  Test 1: FpsMode basics
// ═══════════════════════════════════════════════════════════════════════

static int test_fps_mode() {
  FpsMode fps;
  fps.config.smoothing = 1000.0f;  // near-instant for deterministic tests
  fps.reset(Vec3f{0.0f, 0.0f, 0.0f});

  FrameContext ctx = FrameContext::first(1.0f / 60.0f);

  // Initial update: look along -Z
  Camera cam = fps.update(ctx);
  CHECK(fabsf_(cam.position(1) - fps.config.eyeOffset(1)) < kTol,
        "FPS eye offset Y");

  // Yaw right
  fps.yawInput = 0.5f;  // radians
  fps.pitchInput = 0.0f;
  ctx = ctx.next(1.0f / 60.0f);
  cam = fps.update(ctx);
  fps.clear_input();

  CHECK(fabsf_(fps.yaw - 0.5f) < kTol, "FPS yaw accumulation");

  // Yaw left
  fps.yawInput = -0.3f;
  ctx = ctx.next(1.0f / 60.0f);
  cam = fps.update(ctx);
  fps.clear_input();

  CHECKF(fps.yaw, 0.2f, kTol, "FPS yaw subtraction");

  // Pitch clamping: try to exceed max
  fps.pitchInput = 100.0f;  // huge value
  ctx = ctx.next(1.0f / 60.0f);
  cam = fps.update(ctx);
  fps.clear_input();

  CHECK(fps.pitch <= fps.config.maxPitch + kTol, "FPS pitch clamped at max");

  // Pitch clamping: try to go below min
  fps.pitchInput = -200.0f;
  ctx = ctx.next(1.0f / 60.0f);
  cam = fps.update(ctx);
  fps.clear_input();

  CHECK(fps.pitch >= fps.config.minPitch - kTol, "FPS pitch clamped at min");

  // Character position binding
  fps.characterPosition = Vec3f{10.0f, 5.0f, 3.0f};
  ctx = ctx.next(1.0f / 60.0f);
  cam = fps.update(ctx);

  CHECKF(cam.position(0), 10.0f + fps.config.eyeOffset(0), kTol,
         "FPS character X binding");
  CHECKF(cam.position(1), 5.0f + fps.config.eyeOffset(1), kTol,
         "FPS character Y binding");
  CHECKF(cam.position(2), 3.0f + fps.config.eyeOffset(2), kTol,
         "FPS character Z binding");

  // yaw_quaternion consistency
  Quat4f yawQ = fps.yaw_quaternion();
  Quat4f expected = quat_from_axis_angle(Vec3f{0.0f, 1.0f, 0.0f}, fps.yaw);
  CHECK(quat_angle(yawQ, expected) < kTol, "FPS yaw_quaternion matches yaw angle");

  fprintf(stderr, "  [PASS] test_fps_mode\n");
  return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  Test 2: CameraShake
// ═══════════════════════════════════════════════════════════════════════

static int test_camera_shake() {
  CameraShake shake;
  FrameContext ctx = FrameContext::first(1.0f / 60.0f);

  // No trauma -> no shake
  Camera base;
  base.position = Vec3f{0.0f, 5.0f, 0.0f};
  base.orientation = identity_quat();

  Camera result = shake.apply(base, ctx);
  CHECK(vec3_dist(result.position, base.position) < kTol,
        "No shake when trauma=0");

  // Add trauma
  shake.addTrauma(0.8f);
  CHECK(fabsf_(shake.trauma - 0.8f) < kTol, "addTrauma sets correctly");

  // Trauma clamping
  shake.addTrauma(0.5f);
  CHECK(fabsf_(shake.trauma - 1.0f) < kTol, "addTrauma clamps to 1.0");

  // Apply shake produces offsets
  ctx = ctx.next(1.0f / 60.0f);
  result = shake.apply(base, ctx);

  // With trauma > 0, position or orientation should differ from base
  f32 posDiff = vec3_dist(result.position, base.position);
  f32 oriDiff = quat_angle(result.orientation, base.orientation);
  CHECK(posDiff > 1e-6f || oriDiff > 1e-6f,
        "Shake produces non-zero offsets when trauma > 0");

  // Trauma decays over time
  f32 prevTrauma = shake.trauma;
  for (int i = 0; i < 60; ++i) {
    ctx = ctx.next(1.0f / 60.0f);
    shake.apply(base, ctx);
  }
  CHECK(shake.trauma < prevTrauma, "Trauma decays over time");

  // Full decay: run many frames
  for (int i = 0; i < 600; ++i) {
    ctx = ctx.next(1.0f / 60.0f);
    shake.apply(base, ctx);
  }
  CHECK(shake.trauma < 1e-4f, "Trauma fully decays");

  // After full decay, shake returns to baseline
  ctx = ctx.next(1.0f / 60.0f);
  result = shake.apply(base, ctx);
  CHECK(vec3_dist(result.position, base.position) < kTol,
        "Shake returns to baseline after decay");

  // Reset clears state
  shake.addTrauma(1.0f);
  shake.reset();
  CHECK(fabsf_(shake.trauma) < kTol, "reset() clears trauma");
  CHECK(fabsf_(shake.elapsed) < kTol, "reset() clears elapsed");

  // intensity() is trauma^2
  shake.trauma = 0.5f;
  CHECKF(shake.intensity(), 0.25f, kTol, "intensity() = trauma^2");

  fprintf(stderr, "  [PASS] test_camera_shake\n");
  return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  Test 3: CameraRig mode transitions
// ═══════════════════════════════════════════════════════════════════════

static int test_camera_rig() {
  CameraRig rig;
  rig.transConfig.blendDuration = 0.5f;

  // Start in orbit mode
  CHECK(rig.activeMode == CameraModeId::orbit, "Default mode is orbit");
  CHECK(!rig.blending, "Not blending initially");

  FrameContext ctx = FrameContext::first(1.0f / 60.0f);

  // Set orbit pivot so orbit produces a valid camera
  rig.orbit.pivot = Vec3f{0.0f, 0.0f, 0.0f};
  rig.orbit.reset();

  // Update in orbit mode
  CameraOutput out = rig.update(ctx);
  CHECK(out.activeMode == CameraModeId::orbit, "Output reports orbit mode");
  CHECKF(out.blendAlpha, 1.0f, kTol, "No blend -> alpha=1");

  // Set follow target
  rig.follow.targetPosition = Vec3f{0.0f, 0.0f, 0.0f};
  rig.follow.targetOrientation = identity_quat();
  rig.follow.reset();

  // Transition orbit -> follow
  Camera preTransitionCam = out.camera;
  rig.setMode(CameraModeId::follow);
  CHECK(rig.activeMode == CameraModeId::follow, "Mode set to follow");
  CHECK(rig.blending, "Blending active after setMode");
  CHECKF(rig.blendAlpha, 0.0f, kTol, "blendAlpha starts at 0");

  // First frame of blend: should be close to the pre-transition camera
  ctx = ctx.next(1.0f / 60.0f);
  out = rig.update(ctx);
  CHECK(out.blendAlpha > 0.0f && out.blendAlpha < 1.0f,
        "blendAlpha progresses during transition");
  CHECK(out.blendFrom == CameraModeId::orbit, "blendFrom reports orbit");

  // Run blend to completion
  for (int i = 0; i < 100; ++i) {
    ctx = ctx.next(1.0f / 60.0f);
    out = rig.update(ctx);
  }
  CHECKF(out.blendAlpha, 1.0f, kTol, "Blend completes to 1.0");
  CHECK(!rig.blending, "Blending stops after completion");

  // Transition follow -> fps
  rig.fps.reset(Vec3f{0.0f, 0.0f, 0.0f});
  rig.setMode(CameraModeId::fps);
  CHECK(rig.activeMode == CameraModeId::fps, "Mode set to fps");
  CHECK(rig.blending, "Blending active for follow->fps");

  // Run blend to completion
  for (int i = 0; i < 100; ++i) {
    ctx = ctx.next(1.0f / 60.0f);
    out = rig.update(ctx);
  }
  CHECKF(out.blendAlpha, 1.0f, kTol, "fps blend completes");
  CHECK(out.activeMode == CameraModeId::fps, "Final mode is fps");

  // Setting same mode does nothing
  rig.setMode(CameraModeId::fps);
  CHECK(!rig.blending, "setMode to current mode does not blend");

  // View/projection matrices are generated
  // View matrix should not be identity (camera is somewhere)
  Mat4f identity = Mat4f::identity();
  bool viewIsIdentity = true;
  for (int i = 0; i < 4 && viewIsIdentity; ++i)
    for (int j = 0; j < 4 && viewIsIdentity; ++j)
      if (fabsf_(out.view(i, j) - identity(i, j)) > kTol)
        viewIsIdentity = false;
  CHECK(!viewIsIdentity, "View matrix is computed (not identity)");

  fprintf(stderr, "  [PASS] test_camera_rig\n");
  return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  Test 4: Mode transition smoothness (no discontinuity)
// ═══════════════════════════════════════════════════════════════════════

static int test_transition_smoothness() {
  CameraRig rig;
  rig.transConfig.blendDuration = 0.3f;

  // Place orbit and follow in clearly different positions
  rig.orbit.pivot = Vec3f{0.0f, 0.0f, 0.0f};
  rig.orbit.config.distance = 10.0f;
  rig.orbit.reset();

  rig.follow.targetPosition = Vec3f{20.0f, 0.0f, 0.0f};
  rig.follow.targetOrientation = identity_quat();
  rig.follow.reset();

  FrameContext ctx = FrameContext::first(1.0f / 60.0f);

  // Warm up orbit for a few frames
  for (int i = 0; i < 10; ++i) {
    ctx = ctx.next(1.0f / 60.0f);
    rig.update(ctx);
  }

  // Record last orbit camera
  CameraOutput preTrans = rig.update(ctx);

  // Transition to follow
  rig.setMode(CameraModeId::follow);

  // Check continuity: first blend frame should be near the pre-transition
  ctx = ctx.next(1.0f / 60.0f);
  CameraOutput firstBlend = rig.update(ctx);

  f32 posDist = vec3_dist(firstBlend.camera.position, preTrans.camera.position);
  // The first blend frame has blendAlpha close to 0, so it's mostly the old camera
  // with a tiny contribution from the new camera. Should be close.
  CHECK(posDist < 5.0f,
        "First blend frame is near pre-transition (no discontinuity)");

  // Track positions throughout blend — verify monotonic movement
  Vec3f prevPos = firstBlend.camera.position;
  f32 maxJump = 0.0f;
  for (int i = 0; i < 60; ++i) {
    ctx = ctx.next(1.0f / 60.0f);
    CameraOutput blendOut = rig.update(ctx);
    f32 jump = vec3_dist(blendOut.camera.position, prevPos);
    if (jump > maxJump) maxJump = jump;
    prevPos = blendOut.camera.position;
  }
  // Per-frame jump should be reasonable (not a teleport).
  // With cameras ~25 units apart and a 0.3s blend at 60fps,
  // peak per-frame movement (mid-blend smoothstep) can be ~2-3 units.
  CHECK(maxJump < 5.0f,
        "No large jumps during mode transition blend");

  fprintf(stderr, "  [PASS] test_transition_smoothness\n");
  return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  Test 5: GameplayPipeline full frame
// ═══════════════════════════════════════════════════════════════════════

static int test_pipeline_full_frame() {
  GameplayPipeline pipeline;
  pipeline.reset(Vec3f{0.0f, 0.0f, 0.0f});

  // Start in orbit mode (default)
  FrameContext ctx = FrameContext::first(1.0f / 60.0f);

  // Empty input: character stays idle
  ActionSnapshot input = ActionSnapshot::empty(ctx);
  GroundInfo ground = GroundInfo::on_ground(Vec3f{0.0f, 0.0f, 0.0f});

  PipelineOutput out = pipeline.update(input, ground);

  CHECK(out.character.state == LocomotionState::idle,
        "Character starts idle with no input");
  CHECKF(out.character.speed, 0.0f, kTol,
         "Character speed is 0 with no input");
  CHECK(out.camera.activeMode == CameraModeId::orbit,
        "Camera starts in orbit mode");

  // Move forward: set moveAxis.y = 1.0
  ctx = ctx.next(1.0f / 60.0f);
  input = ActionSnapshot::empty(ctx);
  input.moveAxis = Vec2f{0.0f, 1.0f};

  out = pipeline.update(input, ground);

  // After one frame with forward input, character should be moving
  CHECK(out.character.state != LocomotionState::idle ||
        out.character.speed > 0.0f,
        "Character responds to forward input");

  // Run several frames with forward input
  for (int i = 0; i < 30; ++i) {
    ctx = ctx.next(1.0f / 60.0f);
    input = ActionSnapshot::empty(ctx);
    input.moveAxis = Vec2f{0.0f, 1.0f};
    out = pipeline.update(input, ground);
  }

  CHECK(out.character.speed > 0.5f,
        "Character accelerates over multiple frames");
  CHECK(out.character.position(2) > 0.1f,
        "Character moves forward in Z");

  // Jump
  ctx = ctx.next(1.0f / 60.0f);
  input = ActionSnapshot::empty(ctx);
  input.addAction(ActionState{k_actionJump, ActionPhase::pressed, 1.0f, 0.0f});

  out = pipeline.update(input, ground);

  CHECK(out.character.state == LocomotionState::jump_ascend,
        "Character jumps on jump action");
  CHECK(out.character.velocity(1) > 0.0f,
        "Character has upward velocity after jump");

  // Telemetry is populated
  CHECK(out.telemetry.frameNumber == ctx.frameNumber,
        "Telemetry frameNumber matches");

  fprintf(stderr, "  [PASS] test_pipeline_full_frame\n");
  return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  Test 6: Pipeline camera follows character
// ═══════════════════════════════════════════════════════════════════════

static int test_camera_follows_character() {
  GameplayPipeline pipeline;
  pipeline.reset(Vec3f{0.0f, 0.0f, 0.0f});

  // Switch to follow mode
  pipeline.rig.setMode(CameraModeId::follow);

  FrameContext ctx = FrameContext::first(1.0f / 60.0f);
  GroundInfo ground = GroundInfo::on_ground(Vec3f{0.0f, 0.0f, 0.0f});

  // Run forward for many frames
  for (int i = 0; i < 120; ++i) {
    ctx = ctx.next(1.0f / 60.0f);
    ActionSnapshot input = ActionSnapshot::empty(ctx);
    input.moveAxis = Vec2f{0.0f, 1.0f};
    ground = GroundInfo::on_ground(pipeline.character.position);
    pipeline.update(input, ground);
  }

  // Character should have moved forward significantly
  f32 charZ = pipeline.character.position(2);
  CHECK(charZ > 5.0f, "Character moved forward significantly");

  // Camera should be near the character (follow offset distance)
  f32 camCharDist = vec3_dist(
    pipeline.rig.follow.smoothedPos,
    pipeline.character.position
  );
  CHECK(camCharDist < 20.0f,
        "Follow camera stays near character");

  fprintf(stderr, "  [PASS] test_camera_follows_character\n");
  return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  Test 7: Pipeline look input drives camera
// ═══════════════════════════════════════════════════════════════════════

static int test_look_input_drives_camera() {
  GameplayPipeline pipeline;
  pipeline.reset(Vec3f{0.0f, 0.0f, 0.0f});

  // Use orbit mode with high smoothing for near-instant response
  pipeline.rig.orbit.config.smoothing = 1000.0f;
  pipeline.rig.orbit.reset();

  FrameContext ctx = FrameContext::first(1.0f / 60.0f);
  GroundInfo ground = GroundInfo::on_ground(Vec3f{0.0f, 0.0f, 0.0f});

  // Baseline: no look input
  ActionSnapshot input = ActionSnapshot::empty(ctx);
  PipelineOutput out = pipeline.update(input, ground);
  Quat4f baseOri = out.camera.camera.orientation;

  // Apply look input (yaw right)
  for (int i = 0; i < 10; ++i) {
    ctx = ctx.next(1.0f / 60.0f);
    input = ActionSnapshot::empty(ctx);
    input.lookAxis = Vec2f{100.0f, 0.0f};  // large yaw input
    out = pipeline.update(input, ground);
  }

  // Camera orientation should have changed
  f32 oriDelta = quat_angle(out.camera.camera.orientation, baseOri);
  CHECK(oriDelta > 0.01f, "Look input changes camera orientation");

  fprintf(stderr, "  [PASS] test_look_input_drives_camera\n");
  return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  Test 8: Sprint action
// ═══════════════════════════════════════════════════════════════════════

static int test_sprint_action() {
  GameplayPipeline pipeline;
  pipeline.reset(Vec3f{0.0f, 0.0f, 0.0f});

  FrameContext ctx = FrameContext::first(1.0f / 60.0f);
  GroundInfo ground = GroundInfo::on_ground(Vec3f{0.0f, 0.0f, 0.0f});

  // Run forward without sprint
  f32 normalSpeed = 0.0f;
  for (int i = 0; i < 60; ++i) {
    ctx = ctx.next(1.0f / 60.0f);
    ActionSnapshot input = ActionSnapshot::empty(ctx);
    input.moveAxis = Vec2f{0.0f, 1.0f};
    PipelineOutput out = pipeline.update(input, ground);
    normalSpeed = out.character.speed;
  }

  // Reset and run with sprint
  pipeline.reset(Vec3f{0.0f, 0.0f, 0.0f});
  ctx = FrameContext::first(1.0f / 60.0f);

  f32 sprintSpeed = 0.0f;
  for (int i = 0; i < 60; ++i) {
    ctx = ctx.next(1.0f / 60.0f);
    ActionSnapshot input = ActionSnapshot::empty(ctx);
    input.moveAxis = Vec2f{0.0f, 1.0f};
    input.addAction(ActionState{k_actionSprint, ActionPhase::held, 1.0f, 0.0f});
    PipelineOutput out = pipeline.update(input, ground);
    sprintSpeed = out.character.speed;
  }

  CHECK(sprintSpeed > normalSpeed + 0.5f,
        "Sprint action increases character speed");

  fprintf(stderr, "  [PASS] test_sprint_action\n");
  return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  Main
// ═══════════════════════════════════════════════════════════════════════

int main() {
  fprintf(stderr, "=== Gameplay Integration Tests ===\n");

  int failures = 0;
  failures += test_fps_mode();
  failures += test_camera_shake();
  failures += test_camera_rig();
  failures += test_transition_smoothness();
  failures += test_pipeline_full_frame();
  failures += test_camera_follows_character();
  failures += test_look_input_drives_camera();
  failures += test_sprint_action();

  if (failures == 0) {
    fprintf(stderr, "\n=== ALL GAMEPLAY INTEGRATION TESTS PASSED ===\n");
  } else {
    fprintf(stderr, "\n=== %d TEST(S) FAILED ===\n", failures);
  }
  return failures;
}
