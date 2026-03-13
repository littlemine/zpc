#pragma once
/// @file CharacterState.hpp
/// @brief Character locomotion state machine, ground detection, and snapshot types.
///
/// The state machine is a table-driven finite automaton:
///   - LocomotionState enum defines all possible states.
///   - GroundInfo carries per-frame ground detection results.
///   - MovementIntent is the intermediate between input and locomotion.
///   - CharacterSnapshot is the per-frame output of the character system.
///   - CharacterStateMachine owns the state and transition logic.
///
/// Conventions:
///   - Y-up, right-handed coordinate system.
///   - Velocity is world-space, meters per second.
///   - Rotation stored as unit quaternion (x, y, z, w).

#include "zensim/gameplay/Core.hpp"
#include "zensim/gameplay/MathExtensions.hpp"

namespace zs::gameplay {

  // ====================================================================
  //  Locomotion states
  // ====================================================================

  /// Character locomotion states.
  enum class LocomotionState : u8 {
    idle,
    walk,
    run,
    sprint,
    jump_ascend,  ///< Rising phase of jump
    jump_apex,    ///< Near the top of the jump arc
    fall,         ///< Falling (no ground contact)
    land,         ///< Just landed (brief recovery)
    count         ///< Sentinel for array sizing
  };

  /// Animation tag bitmask flags.
  namespace AnimTag {
    inline constexpr u32 none           = 0;
    inline constexpr u32 grounded       = 1u << 0;
    inline constexpr u32 airborne       = 1u << 1;
    inline constexpr u32 accelerating   = 1u << 2;
    inline constexpr u32 decelerating   = 1u << 3;
    inline constexpr u32 turning        = 1u << 4;
    inline constexpr u32 landing        = 1u << 5;
    inline constexpr u32 jump_rising    = 1u << 6;
    inline constexpr u32 jump_falling   = 1u << 7;
  }

  // ====================================================================
  //  Ground detection
  // ====================================================================

  /// Result of a ground query (raycast / spherecast).
  struct GroundInfo {
    bool  grounded  {false};                ///< Standing on a surface
    Vec3f normal    {0.0f, 1.0f, 0.0f};     ///< Surface normal (world up if not grounded)
    Vec3f point     {0.0f, 0.0f, 0.0f};     ///< Contact point in world space
    f32   distance  {0.0f};                 ///< Distance to ground (0 if grounded)
    f32   slope     {0.0f};                 ///< Angle from vertical in radians

    /// Create a grounded result.
    static constexpr GroundInfo on_ground(Vec3f pos, Vec3f norm = Vec3f{0.0f, 1.0f, 0.0f}) noexcept {
      GroundInfo g;
      g.grounded = true;
      g.normal = norm;
      g.point = pos;
      g.distance = 0.0f;
      // slope = acos(dot(normal, up))
      g.slope = zs::acos(zs::math::clamp(norm(1), -1.0f, 1.0f));
      return g;
    }

    /// Create an airborne result.
    static constexpr GroundInfo in_air(f32 dist = 1.0f) noexcept {
      GroundInfo g;
      g.grounded = false;
      g.distance = dist;
      return g;
    }
  };

  // ====================================================================
  //  Movement intent (input -> character bridge)
  // ====================================================================

  /// Computed from input: what the player wants the character to do.
  struct MovementIntent {
    Vec3f direction {0.0f, 0.0f, 0.0f};  ///< World-space desired direction (normalized or zero)
    f32   magnitude {0.0f};               ///< [0, 1] intensity (analog stick magnitude)
    bool  wantsJump   {false};            ///< Jump requested this frame
    bool  wantsSprint {false};            ///< Sprint modifier active

    /// No movement intent.
    static constexpr MovementIntent none() noexcept {
      return MovementIntent{};
    }

    /// Helper: does the player want to move?
    constexpr bool hasMovement() const noexcept {
      return magnitude > 0.01f;
    }
  };

  // ====================================================================
  //  Character snapshot (per-frame output)
  // ====================================================================

  /// Complete per-frame state of the character, produced by CharacterStateMachine::update().
  struct CharacterSnapshot {
    Vec3f           position       {0.0f, 0.0f, 0.0f};
    Vec3f           velocity       {0.0f, 0.0f, 0.0f};
    Quat4f          rotation       {0.0f, 0.0f, 0.0f, 1.0f};
    LocomotionState state          {LocomotionState::idle};
    LocomotionState previousState  {LocomotionState::idle};
    f32             timeInState    {0.0f};   ///< Seconds in current state
    u32             stateTransitionCount {0};
    u32             animTags       {AnimTag::grounded};
    GroundInfo      ground;
    f32             speed          {0.0f};   ///< Horizontal speed (derived from velocity)
  };

  // ====================================================================
  //  Character configuration
  // ====================================================================

  /// Tuning parameters for the character movement and state machine.
  struct CharacterConfig {
    f32 walkSpeed         {2.0f};     ///< Max walk speed (m/s)
    f32 runSpeed          {5.0f};     ///< Max run speed (m/s)
    f32 sprintSpeed       {8.0f};     ///< Max sprint speed (m/s)
    f32 acceleration      {20.0f};    ///< Ground acceleration (m/s^2)
    f32 deceleration      {15.0f};    ///< Ground deceleration when no input (m/s^2)
    f32 airAcceleration   {5.0f};     ///< Air control acceleration (m/s^2)
    f32 turnSmoothing     {10.0f};    ///< Rotation smoothing (higher = snappier)
    f32 gravity           {-20.0f};   ///< Gravity acceleration (m/s^2, negative = down)
    f32 jumpVelocity      {10.0f};    ///< Initial jump velocity (m/s)
    f32 jumpCutMultiplier {0.5f};     ///< Velocity multiplier when jump button released early
    f32 coyoteTime        {0.1f};     ///< Seconds after leaving ground where jump still allowed
    f32 jumpBufferTime    {0.1f};     ///< Seconds before landing where jump input is buffered
    f32 landRecoveryTime  {0.1f};     ///< Duration of the landing state
    f32 apexThreshold     {1.0f};     ///< Vertical speed below which jump_apex is entered (m/s)
    f32 walkThreshold     {0.5f};     ///< Magnitude below which -> walk (above -> run)
    f32 maxSlopeAngle     {50.0f * k_deg2rad}; ///< Max walkable slope angle
  };

  // ====================================================================
  //  Character state machine
  // ====================================================================

  /// Drives character locomotion state transitions and movement.
  struct CharacterStateMachine {
    CharacterConfig config{};

    // -- State --
    Vec3f           position       {0.0f, 0.0f, 0.0f};
    Vec3f           velocity       {0.0f, 0.0f, 0.0f};
    Quat4f          rotation       {0.0f, 0.0f, 0.0f, 1.0f};
    LocomotionState currentState   {LocomotionState::idle};
    LocomotionState previousState  {LocomotionState::idle};
    f32             timeInState    {0.0f};
    u32             transitionCount {0};

    // -- Jump tracking --
    f32             coyoteTimer    {0.0f};  ///< Time since last grounded (for coyote time)
    f32             jumpBufferTimer{0.0f};  ///< Time since jump was requested
    bool            jumpHeld       {false}; ///< Is the jump button currently held?
    bool            hasJumped      {false}; ///< Has the character jumped (consumed coyote time)?

    /// Reset to initial state at given position.
    constexpr void reset(Vec3f pos) noexcept {
      position = pos;
      velocity = Vec3f{0.0f, 0.0f, 0.0f};
      rotation = identity_quat();
      currentState = LocomotionState::idle;
      previousState = LocomotionState::idle;
      timeInState = 0.0f;
      transitionCount = 0;
      coyoteTimer = 0.0f;
      jumpBufferTimer = 0.0f;
      jumpHeld = false;
      hasJumped = false;
    }

    /// Transition to a new state.
    constexpr void transition(LocomotionState next) noexcept {
      if (next == currentState) return;
      previousState = currentState;
      currentState = next;
      timeInState = 0.0f;
      ++transitionCount;
    }

    /// Compute the target speed based on intent and state.
    constexpr f32 targetSpeed(MovementIntent const& intent) const noexcept {
      if (!intent.hasMovement()) return 0.0f;
      if (intent.wantsSprint) return config.sprintSpeed * intent.magnitude;
      if (intent.magnitude > config.walkThreshold)
        return config.runSpeed * intent.magnitude;
      return config.walkSpeed * intent.magnitude;
    }

    /// Update the state machine for one frame.
    ///
    /// @param intent   Movement intent from the input system
    /// @param ground   Ground detection result for this frame
    /// @param ctx      Frame timing context
    /// @return         CharacterSnapshot for this frame
    constexpr CharacterSnapshot update(MovementIntent const& intent,
                                        GroundInfo const& ground,
                                        FrameContext const& ctx) noexcept {
      const f32 dt = ctx.dt;
      timeInState += dt;

      // -- Coyote time tracking --
      if (ground.grounded) {
        coyoteTimer = 0.0f;
        hasJumped = false;
      } else {
        coyoteTimer += dt;
      }

      // -- Jump buffer tracking --
      if (intent.wantsJump) {
        jumpBufferTimer = config.jumpBufferTime;
        jumpHeld = true;
      } else {
        jumpBufferTimer -= dt;
        if (jumpBufferTimer < 0.0f) jumpBufferTimer = 0.0f;
        jumpHeld = false;
      }

      // -- State transitions --
      const bool canCoyoteJump = (coyoteTimer <= config.coyoteTime) && !hasJumped;
      const bool wantsJumpNow = jumpBufferTimer > 0.0f;

      switch (currentState) {
        case LocomotionState::idle:
        case LocomotionState::walk:
        case LocomotionState::run:
        case LocomotionState::sprint: {
          if (!ground.grounded && coyoteTimer > config.coyoteTime) {
            transition(LocomotionState::fall);
          } else if (wantsJumpNow && canCoyoteJump) {
            velocity(1) = config.jumpVelocity;
            hasJumped = true;
            jumpBufferTimer = 0.0f;
            transition(LocomotionState::jump_ascend);
          } else if (!intent.hasMovement()) {
            transition(LocomotionState::idle);
          } else if (intent.wantsSprint) {
            transition(LocomotionState::sprint);
          } else if (intent.magnitude > config.walkThreshold) {
            transition(LocomotionState::run);
          } else {
            transition(LocomotionState::walk);
          }
          break;
        }

        case LocomotionState::jump_ascend: {
          // Variable-height jump: cut velocity when button released
          if (!jumpHeld && velocity(1) > 0.0f) {
            velocity(1) *= config.jumpCutMultiplier;
          }
          if (velocity(1) <= config.apexThreshold && velocity(1) >= -config.apexThreshold) {
            transition(LocomotionState::jump_apex);
          } else if (velocity(1) < -config.apexThreshold) {
            transition(LocomotionState::fall);
          }
          if (ground.grounded && timeInState > 0.05f) {
            transition(LocomotionState::land);
          }
          break;
        }

        case LocomotionState::jump_apex: {
          if (velocity(1) < -config.apexThreshold) {
            transition(LocomotionState::fall);
          }
          if (ground.grounded && timeInState > 0.02f) {
            transition(LocomotionState::land);
          }
          break;
        }

        case LocomotionState::fall: {
          // Allow coyote jump during fall if timer hasn't expired
          if (wantsJumpNow && canCoyoteJump) {
            velocity(1) = config.jumpVelocity;
            hasJumped = true;
            jumpBufferTimer = 0.0f;
            transition(LocomotionState::jump_ascend);
          } else if (ground.grounded) {
            transition(LocomotionState::land);
          }
          break;
        }

        case LocomotionState::land: {
          if (timeInState >= config.landRecoveryTime) {
            // Check for buffered jump
            if (wantsJumpNow) {
              velocity(1) = config.jumpVelocity;
              hasJumped = true;
              jumpBufferTimer = 0.0f;
              transition(LocomotionState::jump_ascend);
            } else if (intent.hasMovement()) {
              transition(intent.magnitude > config.walkThreshold
                         ? LocomotionState::run : LocomotionState::walk);
            } else {
              transition(LocomotionState::idle);
            }
          }
          break;
        }

        default:
          break;
      }

      // -- Horizontal movement --
      const bool isGrounded = (currentState != LocomotionState::jump_ascend &&
                               currentState != LocomotionState::jump_apex &&
                               currentState != LocomotionState::fall);
      const f32 accel = isGrounded ? config.acceleration : config.airAcceleration;
      const f32 maxSpeed = targetSpeed(intent);

      // Horizontal velocity (XZ plane)
      Vec3f horizVel{velocity(0), 0.0f, velocity(2)};
      const f32 horizSpeed = zs::sqrt(horizVel(0)*horizVel(0) + horizVel(2)*horizVel(2));

      if (intent.hasMovement()) {
        // Accelerate toward desired direction
        horizVel(0) += intent.direction(0) * accel * dt;
        horizVel(2) += intent.direction(2) * accel * dt;

        // Clamp horizontal speed
        const f32 newSpeed = zs::sqrt(horizVel(0)*horizVel(0) + horizVel(2)*horizVel(2));
        if (newSpeed > maxSpeed && newSpeed > k_epsilon) {
          const f32 scale = maxSpeed / newSpeed;
          horizVel(0) *= scale;
          horizVel(2) *= scale;
        }
      } else if (isGrounded && horizSpeed > k_epsilon) {
        // Decelerate when no input (ground only)
        const f32 reduction = config.deceleration * dt;
        if (reduction >= horizSpeed) {
          horizVel(0) = 0.0f;
          horizVel(2) = 0.0f;
        } else {
          const f32 scale = (horizSpeed - reduction) / horizSpeed;
          horizVel(0) *= scale;
          horizVel(2) *= scale;
        }
      }

      velocity(0) = horizVel(0);
      velocity(2) = horizVel(2);

      // -- Vertical movement (gravity) --
      if (!ground.grounded || currentState == LocomotionState::jump_ascend
          || currentState == LocomotionState::jump_apex) {
        velocity(1) += config.gravity * dt;
      } else {
        // Snap to ground
        if (velocity(1) < 0.0f) velocity(1) = 0.0f;
      }

      // -- Integrate position --
      position(0) += velocity(0) * dt;
      position(1) += velocity(1) * dt;
      position(2) += velocity(2) * dt;

      // -- Rotation toward movement direction --
      if (intent.hasMovement() && isGrounded) {
        const Vec3f lookDir = Vec3f{intent.direction(0), 0.0f, intent.direction(2)}.normalized();
        if (lookDir(0)*lookDir(0) + lookDir(2)*lookDir(2) > k_epsilon) {
          // Compute target yaw angle
          const f32 targetYaw = zs::atan2(lookDir(0), lookDir(2));
          const Quat4f targetRot = quat_from_axis_angle(Vec3f{0.0f, 1.0f, 0.0f}, targetYaw);
          // Smooth rotation
          const f32 t = 1.0f - zs::exp(-config.turnSmoothing * dt);
          rotation = slerp(rotation, targetRot, t);
        }
      }

      // -- Build animation tags --
      u32 tags = AnimTag::none;
      if (ground.grounded) tags |= AnimTag::grounded;
      else tags |= AnimTag::airborne;

      if (currentState == LocomotionState::jump_ascend) tags |= AnimTag::jump_rising;
      if (currentState == LocomotionState::fall) tags |= AnimTag::jump_falling;
      if (currentState == LocomotionState::land) tags |= AnimTag::landing;

      const f32 finalHorizSpeed = zs::sqrt(velocity(0)*velocity(0) + velocity(2)*velocity(2));
      if (intent.hasMovement() && finalHorizSpeed < maxSpeed - 0.1f)
        tags |= AnimTag::accelerating;
      if (!intent.hasMovement() && finalHorizSpeed > 0.1f)
        tags |= AnimTag::decelerating;

      // -- Build snapshot --
      CharacterSnapshot snap;
      snap.position = position;
      snap.velocity = velocity;
      snap.rotation = rotation;
      snap.state = currentState;
      snap.previousState = previousState;
      snap.timeInState = timeInState;
      snap.stateTransitionCount = transitionCount;
      snap.animTags = tags;
      snap.ground = ground;
      snap.speed = finalHorizSpeed;

      return snap;
    }
  };

}  // namespace zs::gameplay
