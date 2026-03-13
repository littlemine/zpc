#pragma once
/// @file InputTypes.hpp
/// @brief Fundamental input types for the 3C control system.
///
/// Defines device types, action identifiers, action phases, and the
/// ActionSnapshot that flows from the input phase to character and camera phases.

#include "zensim/3c/Core.hpp"

namespace zs::threeC {

  // ── Device identification ──────────────────────────────────────────────

  /// Input device type
  enum class DeviceType : u8 {
    keyboard = 0,
    mouse    = 1,
    gamepad  = 2,
    touch    = 3,
  };

  // ── Action identification ──────────────────────────────────────────────

  /// Unique action identifier. Use compile-time hashing or sequential IDs.
  using ActionId = u32;

  /// Compile-time FNV-1a hash for action name strings.
  /// Usage: constexpr auto MOVE_FORWARD = action_id("move_forward");
  constexpr ActionId action_id(const char* str) noexcept {
    u32 hash = 2166136261u;
    while (*str) {
      hash ^= static_cast<u32>(*str++);
      hash *= 16777619u;
    }
    return hash;
  }

  // ── Action phases ──────────────────────────────────────────────────────

  /// Lifecycle phase of an action within a single frame.
  enum class ActionPhase : u8 {
    none     = 0,  ///< Not active
    pressed  = 1,  ///< Just pressed this frame (transition from inactive → active)
    held     = 2,  ///< Held from a previous frame
    released = 3,  ///< Just released this frame (transition from active → inactive)
  };

  // ── Action state ───────────────────────────────────────────────────────

  /// State of a single action for one frame.
  struct ActionState {
    ActionId id;           ///< Which action this is
    ActionPhase phase;     ///< Current phase
    f32 value;             ///< 0 or 1 for digital; continuous for analog
    f32 heldDuration;      ///< Seconds held (0 if not held/pressed)

    /// True if the action was just pressed this frame
    constexpr bool justPressed() const noexcept { return phase == ActionPhase::pressed; }
    /// True if the action is currently active (pressed or held)
    constexpr bool isActive() const noexcept {
      return phase == ActionPhase::pressed || phase == ActionPhase::held;
    }
    /// True if the action was just released this frame
    constexpr bool justReleased() const noexcept { return phase == ActionPhase::released; }
  };

  // ── ActionSnapshot ─────────────────────────────────────────────────────

  /// Maximum number of simultaneous actions in a single frame.
  /// 64 is more than sufficient for any realistic input binding set.
  inline constexpr u32 k_maxActions = 64;

  /// Complete input state for one frame — the output of the input phase.
  ///
  /// This is the primary data structure that flows from the input system
  /// to both the character and camera systems. It uses fixed-size arrays
  /// to avoid any heap allocation in the hot path.
  struct ActionSnapshot {
    ActionState actions[k_maxActions];  ///< All active action states
    u32 actionCount;                    ///< Number of valid entries in actions[]

    Vec2f lookAxis;   ///< Camera look input (processed): x=yaw, y=pitch
    Vec2f moveAxis;   ///< Movement input (processed): x=strafe, y=forward

    FrameContext frame;  ///< Frame timing info

    /// Zero-initialize
    static constexpr ActionSnapshot empty(FrameContext ctx) noexcept {
      ActionSnapshot snap{};
      snap.actionCount = 0;
      snap.lookAxis = Vec2f{0.0f, 0.0f};
      snap.moveAxis = Vec2f{0.0f, 0.0f};
      snap.frame = ctx;
      return snap;
    }

    /// Find an action by ID. Returns nullptr if not found.
    constexpr ActionState const* find(ActionId id) const noexcept {
      for (u32 i = 0; i < actionCount; ++i) {
        if (actions[i].id == id) return &actions[i];
      }
      return nullptr;
    }

    /// Check if a specific action was just pressed
    constexpr bool justPressed(ActionId id) const noexcept {
      auto* s = find(id);
      return s && s->justPressed();
    }

    /// Check if a specific action is active (pressed or held)
    constexpr bool isActive(ActionId id) const noexcept {
      auto* s = find(id);
      return s && s->isActive();
    }

    /// Add an action state. Returns false if full.
    constexpr bool addAction(ActionState state) noexcept {
      if (actionCount >= k_maxActions) return false;
      actions[actionCount++] = state;
      return true;
    }
  };

}  // namespace zs::threeC
