#pragma once
/// @file ActionMap.hpp
/// @brief Action binding and state tracking for the Gameplay input system.
///
/// ActionMap maps abstract action names (via ActionId) to input bindings,
/// tracks per-action state across frames (pressed/held/released lifecycle),
/// and produces an ActionSnapshot each frame.
///
/// Design: pull-based polling model. The application calls update() each frame
/// with the current raw input state, and ActionMap resolves phases and durations.

#include "zensim/gameplay/input/AxisProcessor.hpp"
#include "zensim/gameplay/input/InputTypes.hpp"

namespace zs::gameplay {

  // ── Key / Button identifiers ───────────────────────────────────────────

  /// Abstract key code. Maps to platform key codes externally.
  using KeyCode = u32;

  /// Abstract gamepad button.
  enum class GamepadButton : u8 {
    a = 0, b, x, y,
    lb, rb, lt_click, rt_click,
    start, select,
    dpad_up, dpad_down, dpad_left, dpad_right,
    lstick_click, rstick_click,
    count
  };

  /// Gamepad axis identifier.
  enum class GamepadAxis : u8 {
    left_x = 0, left_y,
    right_x, right_y,
    left_trigger, right_trigger,
    count
  };

  // ── Binding types ──────────────────────────────────────────────────────

  /// Source of an action binding.
  enum class BindingSource : u8 {
    keyboard_key,
    mouse_button,
    mouse_axis,
    gamepad_button,
    gamepad_axis,
  };

  /// A single binding that maps a physical input to an action.
  struct ActionBinding {
    ActionId actionId;       ///< Target action
    BindingSource source;    ///< Input source type
    u32 sourceCode;          ///< Key code, button index, or axis index
    f32 scale;               ///< Multiplier (e.g., -1 for inverted axis)

    static constexpr ActionBinding key(ActionId id, KeyCode code, f32 scale_ = 1.0f) noexcept {
      return ActionBinding{id, BindingSource::keyboard_key, code, scale_};
    }

    static constexpr ActionBinding mouseButton(ActionId id, u32 button, f32 scale_ = 1.0f) noexcept {
      return ActionBinding{id, BindingSource::mouse_button, button, scale_};
    }

    static constexpr ActionBinding gamepadBtn(ActionId id, GamepadButton btn, f32 scale_ = 1.0f) noexcept {
      return ActionBinding{id, BindingSource::gamepad_button, static_cast<u32>(btn), scale_};
    }

    static constexpr ActionBinding gamepadAxis(ActionId id, GamepadAxis axis, f32 scale_ = 1.0f) noexcept {
      return ActionBinding{id, BindingSource::gamepad_axis, static_cast<u32>(axis), scale_};
    }
  };

  // ── Raw input state (provided by platform layer) ───────────────────────

  /// Maximum number of keyboard keys to track.
  inline constexpr u32 k_maxKeys = 256;
  /// Maximum number of mouse buttons.
  inline constexpr u32 k_maxMouseButtons = 8;
  /// Maximum number of gamepad buttons.
  inline constexpr u32 k_maxGamepadButtons = static_cast<u32>(GamepadButton::count);
  /// Maximum number of gamepad axes.
  inline constexpr u32 k_maxGamepadAxes = static_cast<u32>(GamepadAxis::count);

  /// Raw input state snapshot provided by the platform each frame.
  /// The Gameplay system does NOT poll hardware — the application fills this struct.
  struct RawInputState {
    bool keys[k_maxKeys];                   ///< true = currently pressed
    bool mouseButtons[k_maxMouseButtons];   ///< true = currently pressed
    Vec2f mouseDelta;                       ///< Mouse movement delta this frame
    f32 gamepadAxes[k_maxGamepadAxes];      ///< Axis values [-1, 1]
    bool gamepadButtons[k_maxGamepadButtons]; ///< true = currently pressed
    f32 scrollDelta;                        ///< Mouse scroll wheel delta

    /// Zero-initialize
    static constexpr RawInputState empty() noexcept {
      RawInputState state{};
      state.mouseDelta = Vec2f{0.0f, 0.0f};
      state.scrollDelta = 0.0f;
      return state;
    }
  };

  // ── ActionMap ──────────────────────────────────────────────────────────

  /// Maximum number of bindings in an ActionMap.
  inline constexpr u32 k_maxBindings = 128;

  /// Maps abstract actions to physical input bindings and tracks state
  /// across frames to provide pressed/held/released lifecycle.
  struct ActionMap {
    // ── Bindings ──
    ActionBinding bindings[k_maxBindings];
    u32 bindingCount;

    // ── Axis processing ──
    DualAxisConfig moveAxisConfig;
    DualAxisConfig lookAxisConfig;

    // ── Axis binding (which gamepad axes map to move/look) ──
    GamepadAxis moveXAxis;
    GamepadAxis moveYAxis;
    GamepadAxis lookXAxis;
    GamepadAxis lookYAxis;

    // ── Previous frame state for phase detection ──
    bool prevActive[k_maxActions];  ///< Was each action active last frame?
    f32  heldTime[k_maxActions];    ///< How long each action has been held

    // ── Construction ──

    static constexpr ActionMap create() noexcept {
      ActionMap map{};
      map.bindingCount = 0;
      map.moveAxisConfig = DualAxisConfig::gamepadStick();
      map.lookAxisConfig = DualAxisConfig::mouseLook();
      map.moveXAxis = GamepadAxis::left_x;
      map.moveYAxis = GamepadAxis::left_y;
      map.lookXAxis = GamepadAxis::right_x;
      map.lookYAxis = GamepadAxis::right_y;
      for (u32 i = 0; i < k_maxActions; ++i) {
        map.prevActive[i] = false;
        map.heldTime[i] = 0.0f;
      }
      return map;
    }

    /// Add a binding. Returns false if the binding array is full.
    constexpr bool addBinding(ActionBinding binding) noexcept {
      if (bindingCount >= k_maxBindings) return false;
      bindings[bindingCount++] = binding;
      return true;
    }

    /// Remove all bindings for an action.
    constexpr void removeBindings(ActionId id) noexcept {
      u32 write = 0;
      for (u32 read = 0; read < bindingCount; ++read) {
        if (bindings[read].actionId != id) {
          bindings[write++] = bindings[read];
        }
      }
      bindingCount = write;
    }

    /// Process raw input and produce an ActionSnapshot.
    /// This is the main per-frame entry point for the input system.
    constexpr ActionSnapshot update(RawInputState const& raw, FrameContext const& frame) noexcept {
      ActionSnapshot snap = ActionSnapshot::empty(frame);

      // Track which actions are active this frame
      bool currentActive[k_maxActions]{};
      f32 currentValue[k_maxActions]{};

      // Resolve bindings
      for (u32 i = 0; i < bindingCount; ++i) {
        auto const& b = bindings[i];
        f32 rawValue = 0.0f;
        bool active = false;

        switch (b.source) {
          case BindingSource::keyboard_key:
            if (b.sourceCode < k_maxKeys) {
              active = raw.keys[b.sourceCode];
              rawValue = active ? 1.0f : 0.0f;
            }
            break;
          case BindingSource::mouse_button:
            if (b.sourceCode < k_maxMouseButtons) {
              active = raw.mouseButtons[b.sourceCode];
              rawValue = active ? 1.0f : 0.0f;
            }
            break;
          case BindingSource::gamepad_button:
            if (b.sourceCode < k_maxGamepadButtons) {
              active = raw.gamepadButtons[b.sourceCode];
              rawValue = active ? 1.0f : 0.0f;
            }
            break;
          case BindingSource::gamepad_axis:
            if (b.sourceCode < k_maxGamepadAxes) {
              rawValue = raw.gamepadAxes[b.sourceCode] * b.scale;
              active = (rawValue < -0.01f || rawValue > 0.01f);
            }
            break;
          case BindingSource::mouse_axis:
            // Mouse axes handled separately via lookAxis
            break;
        }

        // Find the action slot index (hash to slot for O(1))
        // Simple: use actionId % k_maxActions
        const u32 slot = b.actionId % k_maxActions;

        // OR the active state (any binding can activate)
        if (active) currentActive[slot] = true;
        // Take the max value for composite bindings
        const f32 scaled = rawValue * b.scale;
        const f32 absScaled = scaled < 0.0f ? -scaled : scaled;
        const f32 absCurrent = currentValue[slot] < 0.0f ? -currentValue[slot] : currentValue[slot];
        if (absScaled > absCurrent) {
          currentValue[slot] = scaled;
        }
      }

      // Resolve phases and build the snapshot
      for (u32 i = 0; i < bindingCount; ++i) {
        const u32 slot = bindings[i].actionId % k_maxActions;
        const ActionId id = bindings[i].actionId;

        // Skip if we already added this action
        if (snap.find(id) != nullptr) continue;

        ActionPhase phase = ActionPhase::none;
        f32 duration = 0.0f;

        if (currentActive[slot]) {
          if (prevActive[slot]) {
            phase = ActionPhase::held;
            duration = heldTime[slot] + frame.dt;
          } else {
            phase = ActionPhase::pressed;
            duration = 0.0f;
          }
        } else {
          if (prevActive[slot]) {
            phase = ActionPhase::released;
            duration = heldTime[slot];
          } else {
            continue;  // Not active, wasn't active — skip
          }
        }

        snap.addAction(ActionState{id, phase, currentValue[slot], duration});
      }

      // Update tracking state
      for (u32 i = 0; i < k_maxActions; ++i) {
        if (currentActive[i]) {
          heldTime[i] = prevActive[i] ? heldTime[i] + frame.dt : 0.0f;
        } else {
          heldTime[i] = 0.0f;
        }
        prevActive[i] = currentActive[i];
      }

      // Process move/look axes
      {
        // Move: gamepad stick or WASD composite
        f32 moveRawX = raw.gamepadAxes[static_cast<u32>(moveXAxis)];
        f32 moveRawY = raw.gamepadAxes[static_cast<u32>(moveYAxis)];
        snap.moveAxis = process_dual_axis(moveRawX, moveRawY, moveAxisConfig);
      }
      {
        // Look: mouse delta (already a delta, not absolute) + gamepad right stick
        f32 lookRawX = raw.mouseDelta(0) * lookAxisConfig.xConfig.sensitivity
                     + raw.gamepadAxes[static_cast<u32>(lookXAxis)];
        f32 lookRawY = raw.mouseDelta(1) * lookAxisConfig.yConfig.sensitivity
                     + raw.gamepadAxes[static_cast<u32>(lookYAxis)];
        // For look, only process gamepad part through curve; mouse is raw * sensitivity
        snap.lookAxis = Vec2f{lookRawX, lookRawY};
      }

      return snap;
    }
  };

}  // namespace zs::gameplay
