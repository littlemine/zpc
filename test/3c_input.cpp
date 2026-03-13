/// @file 3c_input.cpp
/// @brief Tests for ActionMap, AxisProcessor, InputTypes, and InputBuffer.

#include <cassert>
#include <cmath>
#include <cstdio>
#include "zensim/3c/input/ActionMap.hpp"
#include "zensim/3c/input/InputBuffer.hpp"

static bool near_eq(float a, float b, float eps = 1e-5f) {
  return std::fabs(a - b) < eps;
}

int main() {
  using namespace zs;
  using namespace zs::threeC;

  // ═══════════════════════════════════════════════════════════════════════
  //  action_id: compile-time hash
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[action_id] hash consistency... ");
    constexpr auto id1 = action_id("move_forward");
    constexpr auto id2 = action_id("move_forward");
    constexpr auto id3 = action_id("jump");
    assert(id1 == id2);
    assert(id1 != id3);
    assert(id1 != 0);
    assert(id3 != 0);
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  ActionState: phase queries
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[ActionState] phase queries... ");
    ActionState pressed{action_id("jump"), ActionPhase::pressed, 1.0f, 0.0f};
    assert(pressed.justPressed());
    assert(pressed.isActive());
    assert(!pressed.justReleased());

    ActionState held{action_id("jump"), ActionPhase::held, 1.0f, 0.5f};
    assert(!held.justPressed());
    assert(held.isActive());
    assert(!held.justReleased());

    ActionState released{action_id("jump"), ActionPhase::released, 0.0f, 0.5f};
    assert(!released.isActive());
    assert(released.justReleased());

    ActionState none{action_id("jump"), ActionPhase::none, 0.0f, 0.0f};
    assert(!none.isActive());
    assert(!none.justPressed());
    assert(!none.justReleased());
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  ActionSnapshot: add and find
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[ActionSnapshot] add/find... ");
    auto ctx = FrameContext::first(0.016f);
    auto snap = ActionSnapshot::empty(ctx);

    constexpr auto JUMP = action_id("jump");
    constexpr auto MOVE = action_id("move_forward");

    assert(snap.find(JUMP) == nullptr);
    snap.addAction(ActionState{JUMP, ActionPhase::pressed, 1.0f, 0.0f});
    snap.addAction(ActionState{MOVE, ActionPhase::held, 0.7f, 0.5f});

    auto* jumpState = snap.find(JUMP);
    assert(jumpState != nullptr);
    assert(jumpState->justPressed());
    assert(near_eq(jumpState->value, 1.0f));

    auto* moveState = snap.find(MOVE);
    assert(moveState != nullptr);
    assert(moveState->isActive());
    assert(near_eq(moveState->value, 0.7f));

    assert(snap.justPressed(JUMP));
    assert(!snap.justPressed(MOVE));
    assert(snap.isActive(JUMP));
    assert(snap.isActive(MOVE));
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  AxisProcessor: dead-zone
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[AxisProcessor] dead-zone zeroes small values... ");
    assert(near_eq(apply_dead_zone(0.05f, 0.15f), 0.0f));
    assert(near_eq(apply_dead_zone(-0.1f, 0.15f), 0.0f));
    assert(near_eq(apply_dead_zone(0.15f, 0.15f), 0.0f));  // exactly at threshold
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[AxisProcessor] dead-zone rescales... ");
    // 0.15 dead-zone: 0.575 is midpoint of [0.15, 1.0], should map to 0.5
    float result = apply_dead_zone(0.575f, 0.15f);
    assert(near_eq(result, 0.5f, 0.01f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[AxisProcessor] dead-zone preserves sign... ");
    float pos = apply_dead_zone(0.8f, 0.1f);
    float neg = apply_dead_zone(-0.8f, 0.1f);
    assert(pos > 0.0f);
    assert(neg < 0.0f);
    assert(near_eq(pos, -neg, 1e-5f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[AxisProcessor] dead-zone edge cases... ");
    assert(near_eq(apply_dead_zone(1.0f, 0.0f), 1.0f));
    assert(near_eq(apply_dead_zone(0.5f, 0.0f), 0.5f));
    assert(near_eq(apply_dead_zone(0.5f, 1.0f), 0.0f));
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  AxisProcessor: response curves
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[AxisProcessor] response curves... ");
    // Linear: output = input
    assert(near_eq(apply_response_curve(0.5f, ResponseCurve::linear, 2.0f), 0.5f));

    // Quadratic: output = input^2
    assert(near_eq(apply_response_curve(0.5f, ResponseCurve::quadratic, 2.0f), 0.25f));
    assert(near_eq(apply_response_curve(-0.5f, ResponseCurve::quadratic, 2.0f), -0.25f));

    // Cubic: output = input^3
    assert(near_eq(apply_response_curve(0.5f, ResponseCurve::cubic, 2.0f), 0.125f));

    // Custom with exponent 2.0: same as quadratic
    assert(near_eq(apply_response_curve(0.5f, ResponseCurve::custom, 2.0f), 0.25f));
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  AxisProcessor: full pipeline
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[process_axis] full pipeline... ");
    auto config = AxisConfig::gamepad();  // 15% dead-zone, quadratic
    // Value within dead-zone
    assert(near_eq(process_axis(0.1f, config), 0.0f));
    // Value at max
    float maxOut = process_axis(1.0f, config);
    assert(near_eq(maxOut, 1.0f, 0.01f));
    // Inverted
    auto inv = config;
    inv.invert = true;
    assert(process_axis(1.0f, inv) < 0.0f);
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  AxisProcessor: dual-axis radial dead-zone
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[process_dual_axis] radial dead-zone... ");
    auto config = DualAxisConfig::gamepadStick();

    // Small input zeroed
    Vec2f small = process_dual_axis(0.05f, 0.05f, config);
    assert(near_eq(small(0), 0.0f));
    assert(near_eq(small(1), 0.0f));

    // Full tilt produces non-zero output
    Vec2f full = process_dual_axis(1.0f, 0.0f, config);
    assert(full(0) > 0.0f);
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[process_dual_axis] independent axes... ");
    auto config = DualAxisConfig::defaults();
    Vec2f result = process_dual_axis(0.5f, -0.3f, config);
    assert(near_eq(result(0), 0.5f, 0.01f));
    assert(near_eq(result(1), -0.3f, 0.01f));
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  ActionBinding factory methods
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[ActionBinding] factory methods... ");
    constexpr auto JUMP = action_id("jump");
    auto kb = ActionBinding::key(JUMP, 0x20);
    assert(kb.source == BindingSource::keyboard_key);
    assert(kb.sourceCode == 0x20);
    assert(near_eq(kb.scale, 1.0f));

    auto mb = ActionBinding::mouseButton(JUMP, 0);
    assert(mb.source == BindingSource::mouse_button);

    auto gp = ActionBinding::gamepadBtn(JUMP, GamepadButton::a);
    assert(gp.source == BindingSource::gamepad_button);

    auto ga = ActionBinding::gamepadAxis(JUMP, GamepadAxis::left_x, -1.0f);
    assert(ga.source == BindingSource::gamepad_axis);
    assert(near_eq(ga.scale, -1.0f));
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  ActionMap: creation and binding
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[ActionMap] create and add bindings... ");
    auto map = ActionMap::create();
    assert(map.bindingCount == 0);

    constexpr auto JUMP = action_id("jump");
    constexpr auto MOVE = action_id("move_forward");

    assert(map.addBinding(ActionBinding::key(JUMP, 0x20)));        // space
    assert(map.addBinding(ActionBinding::key(MOVE, 0x57)));        // W
    assert(map.addBinding(ActionBinding::gamepadBtn(JUMP, GamepadButton::a)));
    assert(map.bindingCount == 3);
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  ActionMap: update with raw input
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[ActionMap] update produces correct phases... ");
    auto map = ActionMap::create();
    constexpr auto JUMP = action_id("jump");
    map.addBinding(ActionBinding::key(JUMP, 0x20));  // space bar

    auto ctx = FrameContext::first(0.016f);

    // Frame 1: key not pressed → no action in snapshot
    {
      auto raw = RawInputState::empty();
      auto snap = map.update(raw, ctx);
      assert(snap.find(JUMP) == nullptr);
    }

    // Frame 2: key pressed → pressed phase
    ctx = ctx.next(0.016f);
    {
      auto raw = RawInputState::empty();
      raw.keys[0x20] = true;
      auto snap = map.update(raw, ctx);
      auto* state = snap.find(JUMP);
      assert(state != nullptr);
      assert(state->justPressed());
    }

    // Frame 3: key still held → held phase
    ctx = ctx.next(0.016f);
    {
      auto raw = RawInputState::empty();
      raw.keys[0x20] = true;
      auto snap = map.update(raw, ctx);
      auto* state = snap.find(JUMP);
      assert(state != nullptr);
      assert(state->phase == ActionPhase::held);
    }

    // Frame 4: key released → released phase
    ctx = ctx.next(0.016f);
    {
      auto raw = RawInputState::empty();
      auto snap = map.update(raw, ctx);
      auto* state = snap.find(JUMP);
      assert(state != nullptr);
      assert(state->justReleased());
    }

    // Frame 5: still released → no action (gone)
    ctx = ctx.next(0.016f);
    {
      auto raw = RawInputState::empty();
      auto snap = map.update(raw, ctx);
      assert(snap.find(JUMP) == nullptr);
    }

    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  ActionMap: remove bindings
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[ActionMap] removeBindings... ");
    auto map = ActionMap::create();
    constexpr auto JUMP = action_id("jump");
    constexpr auto MOVE = action_id("move_forward");
    map.addBinding(ActionBinding::key(JUMP, 0x20));
    map.addBinding(ActionBinding::key(MOVE, 0x57));
    map.addBinding(ActionBinding::gamepadBtn(JUMP, GamepadButton::a));
    assert(map.bindingCount == 3);

    map.removeBindings(JUMP);
    assert(map.bindingCount == 1);  // only MOVE binding remains
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  ActionMap: multi-device compositing
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[ActionMap] multi-device compositing... ");
    auto map = ActionMap::create();
    constexpr auto JUMP = action_id("jump");
    map.addBinding(ActionBinding::key(JUMP, 0x20));               // space
    map.addBinding(ActionBinding::gamepadBtn(JUMP, GamepadButton::a));  // A button

    auto ctx = FrameContext::first(0.016f);

    // Both keyboard and gamepad pressed → action should be active
    auto raw = RawInputState::empty();
    raw.keys[0x20] = true;
    raw.gamepadButtons[static_cast<u32>(GamepadButton::a)] = true;
    auto snap = map.update(raw, ctx);
    auto* state = snap.find(JUMP);
    assert(state != nullptr);
    assert(state->justPressed());
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  ActionMap: move/look axes
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[ActionMap] move/look axes... ");
    auto map = ActionMap::create();
    auto ctx = FrameContext::first(0.016f);

    auto raw = RawInputState::empty();
    raw.gamepadAxes[static_cast<u32>(GamepadAxis::left_x)] = 0.8f;
    raw.gamepadAxes[static_cast<u32>(GamepadAxis::left_y)] = -0.5f;
    raw.mouseDelta = Vec2f{10.0f, -5.0f};

    auto snap = map.update(raw, ctx);
    // Move axis should have non-zero output from gamepad
    assert(snap.moveAxis(0) != 0.0f || snap.moveAxis(1) != 0.0f);
    // Look axis should have non-zero output from mouse
    assert(snap.lookAxis(0) != 0.0f || snap.lookAxis(1) != 0.0f);
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  InputBuffer: push and query
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[InputBuffer] push and current... ");
    InputBuffer<8> buf;
    assert(buf.count() == 0);

    auto ctx = FrameContext::first(0.016f);
    auto snap = ActionSnapshot::empty(ctx);
    snap.addAction(ActionState{action_id("jump"), ActionPhase::pressed, 1.0f, 0.0f});
    buf.push(snap);

    assert(buf.count() == 1);
    assert(buf.current().find(action_id("jump")) != nullptr);
    assert(buf.current().justPressed(action_id("jump")));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[InputBuffer] ago() returns correct frames... ");
    InputBuffer<8> buf;
    constexpr auto JUMP = action_id("jump");
    constexpr auto MOVE = action_id("move_forward");

    auto ctx = FrameContext::first(0.016f);

    // Frame 0: jump pressed
    auto snap0 = ActionSnapshot::empty(ctx);
    snap0.addAction(ActionState{JUMP, ActionPhase::pressed, 1.0f, 0.0f});
    buf.push(snap0);

    // Frame 1: move pressed
    ctx = ctx.next(0.016f);
    auto snap1 = ActionSnapshot::empty(ctx);
    snap1.addAction(ActionState{MOVE, ActionPhase::pressed, 1.0f, 0.0f});
    buf.push(snap1);

    // Current should be frame 1
    assert(buf.current().find(MOVE) != nullptr);
    assert(buf.current().find(JUMP) == nullptr);

    // ago(1) should be frame 0
    assert(buf.ago(1).find(JUMP) != nullptr);
    assert(buf.ago(1).find(MOVE) == nullptr);
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[InputBuffer] circular overwrite... ");
    InputBuffer<4> buf;  // capacity of 4

    for (int i = 0; i < 8; ++i) {
      auto ctx = FrameContext{static_cast<double>(i) * 0.016, 0.016f, static_cast<u64>(i)};
      auto snap = ActionSnapshot::empty(ctx);
      buf.push(snap);
    }

    assert(buf.count() == 4);
    assert(buf.full());
    // Current should be frame 7
    assert(buf.current().frame.frameNumber == 7);
    // ago(3) should be frame 4 (oldest)
    assert(buf.ago(3).frame.frameNumber == 4);
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  InputBuffer: wasPressed (jump buffering)
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[InputBuffer] wasPressed (jump buffer)... ");
    InputBuffer<8> buf;
    constexpr auto JUMP = action_id("jump");

    auto ctx = FrameContext::first(0.016f);

    // Frames 0-2: nothing
    for (int i = 0; i < 3; ++i) {
      buf.push(ActionSnapshot::empty(ctx));
      ctx = ctx.next(0.016f);
    }

    // Frame 3: jump pressed
    auto snap3 = ActionSnapshot::empty(ctx);
    snap3.addAction(ActionState{JUMP, ActionPhase::pressed, 1.0f, 0.0f});
    buf.push(snap3);
    ctx = ctx.next(0.016f);

    // Frames 4-5: nothing
    for (int i = 0; i < 2; ++i) {
      buf.push(ActionSnapshot::empty(ctx));
      ctx = ctx.next(0.016f);
    }

    // Jump was pressed 2 frames ago. Check:
    assert(buf.wasPressed(JUMP, 3));   // window of 3 should find it
    assert(buf.wasPressed(JUMP, 4));   // window of 4 should find it
    assert(!buf.wasPressed(JUMP, 1));  // window of 1 (only current) should NOT find it
    assert(!buf.wasPressed(JUMP, 2));  // window of 2 should NOT find it (frames 5,4)
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  InputBuffer: heldFor
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[InputBuffer] heldFor... ");
    InputBuffer<8> buf;
    constexpr auto MOVE = action_id("move_forward");

    auto ctx = FrameContext::first(0.016f);

    // 5 frames of held
    for (int i = 0; i < 5; ++i) {
      auto snap = ActionSnapshot::empty(ctx);
      snap.addAction(ActionState{MOVE, ActionPhase::held, 1.0f, static_cast<float>(i) * 0.016f});
      buf.push(snap);
      ctx = ctx.next(0.016f);
    }

    assert(buf.heldFor(MOVE, 5));   // held for exactly 5 frames
    assert(buf.heldFor(MOVE, 3));   // held for at least 3
    assert(!buf.heldFor(MOVE, 6));  // not held for 6 (only 5 in buffer)
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  InputBuffer: clear
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[InputBuffer] clear... ");
    InputBuffer<8> buf;
    auto ctx = FrameContext::first(0.016f);
    buf.push(ActionSnapshot::empty(ctx));
    buf.push(ActionSnapshot::empty(ctx));
    assert(buf.count() == 2);

    buf.clear();
    assert(buf.count() == 0);
    fprintf(stderr, "ok\n");
  }

  fprintf(stderr, "\n=== All 3C input tests passed ===\n");
  return 0;
}
