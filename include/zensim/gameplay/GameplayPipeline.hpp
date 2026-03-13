#pragma once
/// @file GameplayPipeline.hpp
/// @brief Full-frame Gameplay pipeline: INPUT -> CHARACTER -> CAMERA -> TELEMETRY.
///
/// GameplayPipeline wires together the input, character, and camera subsystems
/// into a single `update()` call that takes an ActionSnapshot and GroundInfo
/// and returns a PipelineOutput containing the CharacterSnapshot, CameraOutput,
/// and FrameTelemetry.
///
/// This is the top-level entry point for the Gameplay system.  It owns a
/// CharacterStateMachine and CameraRig, converting ActionSnapshot axes into
/// MovementIntent and camera look inputs each frame.

#include "zensim/gameplay/Core.hpp"
#include "zensim/gameplay/Telemetry.hpp"
#include "zensim/gameplay/input/InputTypes.hpp"
#include "zensim/gameplay/character/CharacterState.hpp"
#include "zensim/gameplay/camera/CameraRig.hpp"

namespace zs::gameplay {

  // ── Standard action IDs ────────────────────────────────────────────────

  inline constexpr ActionId k_actionJump   = action_id("jump");
  inline constexpr ActionId k_actionSprint = action_id("sprint");

  // ── Pipeline output ────────────────────────────────────────────────────

  /// Complete output of one frame of the Gameplay pipeline.
  struct PipelineOutput {
    CharacterSnapshot character;
    CameraOutput      camera;
    FrameTelemetry    telemetry;
  };

  // ── Pipeline configuration ─────────────────────────────────────────────

  /// Configuration for how the pipeline converts input to camera/character.
  struct PipelineConfig {
    f32 lookSensitivityX  {0.003f};   ///< lookAxis.x -> yaw (radians per unit)
    f32 lookSensitivityY  {0.003f};   ///< lookAxis.y -> pitch (radians per unit)
    bool invertPitchInput {false};    ///< Negate pitch input (flight-sim style)
  };

  // ── GameplayPipeline ───────────────────────────────────────────────────

  /// The top-level Gameplay frame pipeline.
  ///
  /// Owns:
  ///   - CharacterStateMachine (character locomotion)
  ///   - CameraRig (camera modes + transitions + shake)
  ///
  /// Does NOT own:
  ///   - Input system (ActionSnapshot is passed in)
  ///   - Physics/ground detection (GroundInfo is passed in)
  struct GameplayPipeline {
    PipelineConfig        pipelineConfig{};
    CharacterStateMachine character{};
    CameraRig             rig{};

    /// Reset the entire pipeline to initial state at a given position.
    constexpr void reset(Vec3f startPosition) noexcept {
      character.reset(startPosition);
      rig.orbit.reset();
      rig.follow.reset();
      rig.fps.reset(startPosition);
      rig.shake.reset();
      rig.blendAlpha = 1.0f;
      rig.blending = false;
    }

    /// Run one complete frame of the Gameplay pipeline.
    ///
    /// Processing order: INPUT -> CHARACTER -> CAMERA -> TELEMETRY
    ///
    /// @param input   ActionSnapshot from the input system
    /// @param ground  Ground detection result for this frame
    /// @return        Complete PipelineOutput
    constexpr PipelineOutput update(ActionSnapshot const& input,
                                     GroundInfo const& ground) noexcept {
      const FrameContext& ctx = input.frame;

      // ──────────────────────────────────────────────────────────────
      // Phase 1: INPUT -> MovementIntent + camera look
      // ──────────────────────────────────────────────────────────────

      MovementIntent intent = buildMovementIntent(input);

      // ──────────────────────────────────────────────────────────────
      // Phase 2: CHARACTER update
      // ──────────────────────────────────────────────────────────────

      CharacterSnapshot charSnap = character.update(intent, ground, ctx);

      // ──────────────────────────────────────────────────────────────
      // Phase 3: CAMERA update
      // ──────────────────────────────────────────────────────────────

      feedCameraInputs(input, charSnap);
      CameraOutput camOut = rig.update(ctx);
      rig.clearInputs();

      // ──────────────────────────────────────────────────────────────
      // Phase 4: TELEMETRY
      // ──────────────────────────────────────────────────────────────

      FrameTelemetry telemetry = FrameTelemetry::zero(ctx.frameNumber);
      telemetry.stateTransitionsThisFrame =
        (charSnap.state != charSnap.previousState) ? 1 : 0;
      telemetry.velocityMagnitude = charSnap.speed;

      PipelineOutput out;
      out.character = charSnap;
      out.camera = camOut;
      out.telemetry = telemetry;
      return out;
    }

  private:
    /// Convert ActionSnapshot move axes + action buttons into a MovementIntent.
    constexpr MovementIntent buildMovementIntent(ActionSnapshot const& input) const noexcept {
      MovementIntent intent;

      const f32 mx = input.moveAxis(0);  // strafe (left/right)
      const f32 my = input.moveAxis(1);  // forward/back

      const f32 mag = zs::sqrt(mx * mx + my * my);
      if (mag > k_epsilon) {
        // moveAxis is in camera-relative space (x=right, y=forward).
        // Convert to world-space direction using camera rig's active yaw.
        // For simplicity, map moveAxis directly to XZ world:
        //   moveAxis.y -> +Z (forward), moveAxis.x -> +X (right)
        // In a full game, this would be rotated by the camera's yaw.
        intent.direction = Vec3f{mx / mag, 0.0f, my / mag};
        intent.magnitude = (mag > 1.0f) ? 1.0f : mag;
      }

      intent.wantsJump = input.justPressed(k_actionJump);
      intent.wantsSprint = input.isActive(k_actionSprint);

      return intent;
    }

    /// Feed character snapshot and look input into the camera rig's modes.
    constexpr void feedCameraInputs(ActionSnapshot const& input,
                                     CharacterSnapshot const& charSnap) noexcept {
      const f32 yawDelta   = input.lookAxis(0) * pipelineConfig.lookSensitivityX;
      f32 pitchDelta = input.lookAxis(1) * pipelineConfig.lookSensitivityY;
      if (pipelineConfig.invertPitchInput) {
        pitchDelta = -pitchDelta;
      }

      // Orbit mode: pivot at character, look input drives orbit
      rig.orbit.pivot = charSnap.position;
      rig.orbit.yawInput   = yawDelta;
      rig.orbit.pitchInput = pitchDelta;

      // Follow mode: track character position and rotation
      rig.follow.targetPosition    = charSnap.position;
      rig.follow.targetOrientation = charSnap.rotation;

      // FPS mode: bind to character, look input drives yaw/pitch
      rig.fps.characterPosition = charSnap.position;
      rig.fps.yawInput   = yawDelta;
      rig.fps.pitchInput = pitchDelta;
    }
  };

}  // namespace zs::gameplay
