#pragma once
/// @file CameraMode.hpp
/// @brief Camera behaviour modes — OrbitMode and FollowMode.
///
/// Each mode implements `update()` which takes a FrameContext and produces
/// a Camera state.  Modes are plain structs with public tuning parameters;
/// they own no heap allocations and are designed for canary-scenario tuning.

#include "zensim/3c/Core.hpp"
#include "zensim/3c/MathExtensions.hpp"
#include "zensim/3c/camera/Camera.hpp"
#include "zensim/3c/camera/MatrixBuilders.hpp"
#include "zensim/ZpcMathUtils.hpp"

namespace zs::threeC {

  // ════════════════════════════════════════════════════════════════════════
  //  Utility: orientation from look direction
  // ════════════════════════════════════════════════════════════════════════

  /// Build an orientation quaternion from a forward direction and world up vector.
  ///
  /// Convention: camera looks along -Z in local space, so the returned
  /// quaternion maps local -Z to the given forward direction.
  ///
  /// @param forward  Normalized forward direction (camera looks this way)
  /// @param worldUp  World up vector (typically {0,1,0})
  /// @return         Unit quaternion representing the camera orientation
  inline constexpr Quat4f orientation_from_look(Vec3f forward, Vec3f worldUp) noexcept {
    // Build an orthonormal basis: right, up, back
    // Camera looks along -Z, so "back" = -forward
    const Vec3f back = Vec3f{-forward(0), -forward(1), -forward(2)};
    const Vec3f right = worldUp.cross(back).normalized();
    const Vec3f up = back.cross(right);

    // Convert rotation matrix to quaternion.
    // The rotation matrix maps local→world:
    //   column 0 = right,  column 1 = up,  column 2 = back
    // But we store it as m_{ij} where i=row, j=col for the Shepperd extraction.
    // The matrix M has columns = basis vectors, so M_{ij} = basis_j(i).
    const f32 m00 = right(0), m01 = up(0), m02 = back(0);
    const f32 m10 = right(1), m11 = up(1), m12 = back(1);
    const f32 m20 = right(2), m21 = up(2), m22 = back(2);

    const f32 trace = m00 + m11 + m22;
    Quat4f q;

    if (trace > 0.0f) {
      const f32 s = 0.5f / zs::sqrt(trace + 1.0f);
      q = Quat4f{
        (m21 - m12) * s,   // x
        (m02 - m20) * s,   // y
        (m10 - m01) * s,   // z
        0.25f / s           // w
      };
    } else if (m00 > m11 && m00 > m22) {
      const f32 s = 2.0f * zs::sqrt(1.0f + m00 - m11 - m22);
      q = Quat4f{
        0.25f * s,
        (m01 + m10) / s,
        (m02 + m20) / s,
        (m21 - m12) / s
      };
    } else if (m11 > m22) {
      const f32 s = 2.0f * zs::sqrt(1.0f + m11 - m00 - m22);
      q = Quat4f{
        (m01 + m10) / s,
        0.25f * s,
        (m12 + m21) / s,
        (m02 - m20) / s
      };
    } else {
      const f32 s = 2.0f * zs::sqrt(1.0f + m22 - m00 - m11);
      q = Quat4f{
        (m02 + m20) / s,
        (m12 + m21) / s,
        0.25f * s,
        (m10 - m01) / s
      };
    }

    // Ensure w > 0 for consistent hemisphere
    if (q(3) < 0.0f) {
      q = Quat4f{-q(0), -q(1), -q(2), -q(3)};
    }

    return q;
  }

  // ════════════════════════════════════════════════════════════════════════
  //  OrbitMode — orbits around a pivot point
  // ════════════════════════════════════════════════════════════════════════

  /// Configuration / tuning parameters for orbit camera.
  struct OrbitConfig {
    f32 distance       {5.0f};     ///< Arm length (world units)
    f32 minDistance     {1.0f};     ///< Minimum zoom distance
    f32 maxDistance     {50.0f};    ///< Maximum zoom distance
    f32 minPitch       {-85.0f * k_deg2rad}; ///< Pitch floor (radians, negative = look up)
    f32 maxPitch       { 85.0f * k_deg2rad}; ///< Pitch ceiling (radians)
    f32 yawSpeed       {1.0f};     ///< Yaw sensitivity multiplier
    f32 pitchSpeed     {1.0f};     ///< Pitch sensitivity multiplier
    f32 zoomSpeed      {1.0f};     ///< Zoom sensitivity multiplier
    f32 smoothing      {10.0f};    ///< Exponential smoothing factor (higher = snappier)
  };

  /// Orbit camera state.  Call `update()` each frame.
  struct OrbitMode {
    OrbitConfig config{};

    // ── Inputs (set before update) ────────────────────────────────────────
    Vec3f  pivot       {0.0f, 0.0f, 0.0f};  ///< World-space orbit pivot
    f32    yawInput    {0.0f};               ///< Yaw delta this frame (radians, + = right)
    f32    pitchInput  {0.0f};               ///< Pitch delta this frame (radians, + = up)
    f32    zoomInput   {0.0f};               ///< Zoom delta this frame (+ = zoom in)

    // ── Internal state ────────────────────────────────────────────────────
    f32    yaw         {0.0f};               ///< Current yaw angle (radians)
    f32    pitch       {0.0f};               ///< Current pitch angle (radians)
    f32    currentDist {5.0f};               ///< Current arm distance (smoothed)
    f32    targetDist  {5.0f};               ///< Target arm distance

    // ── Smoothed camera output ────────────────────────────────────────────
    Vec3f  smoothedPos {0.0f, 0.0f, 5.0f};  ///< Smoothed camera position

    /// Reset orbit state to defaults based on config.
    constexpr void reset() noexcept {
      yaw = 0.0f;
      pitch = 0.0f;
      currentDist = config.distance;
      targetDist  = config.distance;
      smoothedPos = pivot + Vec3f{0.0f, 0.0f, currentDist};
    }

    /// Update the orbit camera for this frame.
    ///
    /// @param ctx  Frame timing context
    /// @return     Updated Camera state
    constexpr Camera update(FrameContext const& ctx) noexcept {
      // Apply input deltas
      yaw   += yawInput   * config.yawSpeed;
      pitch += pitchInput * config.pitchSpeed;

      // Clamp pitch
      pitch = zs::math::clamp(pitch, config.minPitch, config.maxPitch);

      // Wrap yaw to [-pi, pi]
      if (yaw > k_pi)      yaw -= k_two_pi;
      if (yaw < -k_pi)     yaw += k_two_pi;

      // Zoom
      targetDist -= zoomInput * config.zoomSpeed;
      targetDist = zs::math::clamp(targetDist, config.minDistance, config.maxDistance);

      // Smooth distance
      currentDist = damp(currentDist, targetDist, config.smoothing, ctx.dt);

      // Compute desired camera position from spherical coordinates
      //   x = dist * cos(pitch) * sin(yaw)
      //   y = dist * sin(pitch)
      //   z = dist * cos(pitch) * cos(yaw)
      const f32 cp = zs::cos(pitch);
      const f32 sp = zs::sin(pitch);
      const f32 cy = zs::cos(yaw);
      const f32 sy = zs::sin(yaw);

      const Vec3f desiredPos = pivot + Vec3f{
        currentDist * cp * sy,
        currentDist * sp,
        currentDist * cp * cy
      };

      // Smooth position
      smoothedPos = damp(smoothedPos, desiredPos, config.smoothing, ctx.dt);

      // Build orientation quaternion from look_at direction
      const Vec3f fwd = (pivot - smoothedPos).normalized();
      const Vec3f worldUp{0.0f, 1.0f, 0.0f};

      Camera cam;
      cam.position = smoothedPos;
      cam.orientation = orientation_from_look(fwd, worldUp);
      return cam;
    }

    /// Clear per-frame inputs (call after update to prepare for next frame).
    constexpr void clear_input() noexcept {
      yawInput   = 0.0f;
      pitchInput = 0.0f;
      zoomInput  = 0.0f;
    }
  };

  // ════════════════════════════════════════════════════════════════════════
  //  FollowMode — spring-damped follow behind a target
  // ════════════════════════════════════════════════════════════════════════

  /// Configuration / tuning parameters for follow camera.
  struct FollowConfig {
    Vec3f  offset          {0.0f, 2.0f, -5.0f}; ///< Offset in target-local space (behind & above)
    Vec3f  lookAtOffset    {0.0f, 1.0f, 0.0f};  ///< Point to look at relative to target position
    f32    positionSmoothing{8.0f};   ///< Position damping (higher = snappier)
    f32    rotationSmoothing{6.0f};   ///< Orientation damping
  };

  /// Follow camera state.  Call `update()` each frame.
  struct FollowMode {
    FollowConfig config{};

    // ── Inputs (set before update) ────────────────────────────────────────
    Vec3f  targetPosition    {0.0f, 0.0f, 0.0f}; ///< World-space target position
    Quat4f targetOrientation {0.0f, 0.0f, 0.0f, 1.0f}; ///< Target facing (unit quat)

    // ── Internal state ────────────────────────────────────────────────────
    Vec3f  smoothedPos {0.0f, 2.0f, -5.0f};
    Quat4f smoothedOri {0.0f, 0.0f, 0.0f, 1.0f};

    /// Reset follow state to be immediately at the desired position.
    constexpr void reset() noexcept {
      const Vec3f desiredPos = targetPosition + quat_rotate(targetOrientation, config.offset);
      smoothedPos = desiredPos;
      const Vec3f lookTarget = targetPosition + config.lookAtOffset;
      const Vec3f fwd = (lookTarget - smoothedPos).normalized();
      smoothedOri = orientation_from_look(fwd, Vec3f{0.0f, 1.0f, 0.0f});
    }

    /// Update the follow camera for this frame.
    constexpr Camera update(FrameContext const& ctx) noexcept {
      // Desired position: target pos + rotated offset
      const Vec3f desiredPos = targetPosition + quat_rotate(targetOrientation, config.offset);

      // Smooth position
      smoothedPos = damp(smoothedPos, desiredPos, config.positionSmoothing, ctx.dt);

      // Desired look-at point
      const Vec3f lookTarget = targetPosition + config.lookAtOffset;
      const Vec3f fwd = (lookTarget - smoothedPos).normalized();
      const Vec3f worldUp{0.0f, 1.0f, 0.0f};

      // Desired orientation
      const Quat4f desiredOri = orientation_from_look(fwd, worldUp);

      // Smooth orientation via slerp
      smoothedOri = slerp(smoothedOri, desiredOri,
                          1.0f - zs::exp(-config.rotationSmoothing * ctx.dt));

      Camera cam;
      cam.position = smoothedPos;
      cam.orientation = smoothedOri;
      return cam;
    }
  };

  // ════════════════════════════════════════════════════════════════════════
  //  FpsMode — first-person mouse-look camera
  // ════════════════════════════════════════════════════════════════════════

  /// Configuration for first-person camera mode.
  struct FpsConfig {
    Vec3f  eyeOffset       {0.0f, 1.7f, 0.0f};  ///< Offset from character root to eye level
    f32    minPitch        {-85.0f * k_deg2rad};  ///< Pitch floor (radians)
    f32    maxPitch        { 85.0f * k_deg2rad};  ///< Pitch ceiling (radians)
    f32    yawSpeed        {1.0f};                ///< Yaw input sensitivity
    f32    pitchSpeed      {1.0f};                ///< Pitch input sensitivity
    f32    smoothing       {15.0f};               ///< Orientation smoothing (higher = snappier)
  };

  /// First-person camera state.  Call `update()` each frame.
  ///
  /// The FPS camera is attached to the character's position with an eye offset.
  /// Yaw and pitch are controlled by look input (mouse/stick), with pitch clamped.
  /// The character's yaw rotation is kept in sync with the camera yaw.
  struct FpsMode {
    FpsConfig config{};

    // ── Inputs (set before update) ────────────────────────────────────────
    Vec3f  characterPosition {0.0f, 0.0f, 0.0f};  ///< Character root world position
    f32    yawInput   {0.0f};  ///< Yaw delta this frame (radians, + = right)
    f32    pitchInput {0.0f};  ///< Pitch delta this frame (radians, + = up)

    // ── Internal state ────────────────────────────────────────────────────
    f32    yaw   {0.0f};  ///< Accumulated yaw (radians)
    f32    pitch {0.0f};  ///< Accumulated pitch (radians, clamped)
    Quat4f smoothedOri {0.0f, 0.0f, 0.0f, 1.0f};  ///< Smoothed orientation

    /// Reset to look along -Z with the character at given position.
    constexpr void reset(Vec3f charPos) noexcept {
      characterPosition = charPos;
      yaw = 0.0f;
      pitch = 0.0f;
      smoothedOri = identity_quat();
    }

    /// Update the FPS camera for this frame.
    ///
    /// @param ctx  Frame timing context
    /// @return     Updated Camera state
    constexpr Camera update(FrameContext const& ctx) noexcept {
      // Apply input deltas
      yaw   += yawInput   * config.yawSpeed;
      pitch += pitchInput * config.pitchSpeed;

      // Clamp pitch
      pitch = zs::math::clamp(pitch, config.minPitch, config.maxPitch);

      // Wrap yaw to [-pi, pi]
      if (yaw > k_pi)   yaw -= k_two_pi;
      if (yaw < -k_pi)  yaw += k_two_pi;

      // Build target orientation from yaw and pitch
      // Yaw around Y, then pitch around local X
      const Quat4f yawQ   = quat_from_axis_angle(Vec3f{0.0f, 1.0f, 0.0f}, yaw);
      const Quat4f pitchQ = quat_from_axis_angle(Vec3f{1.0f, 0.0f, 0.0f}, pitch);
      const Quat4f targetOri = quat_multiply(yawQ, pitchQ);

      // Smooth orientation
      const f32 t = 1.0f - zs::exp(-config.smoothing * ctx.dt);
      smoothedOri = slerp(smoothedOri, targetOri, t);

      // Eye position = character position + eye offset (world-space, not rotated)
      Camera cam;
      cam.position = Vec3f{
        characterPosition(0) + config.eyeOffset(0),
        characterPosition(1) + config.eyeOffset(1),
        characterPosition(2) + config.eyeOffset(2)
      };
      cam.orientation = smoothedOri;
      return cam;
    }

    /// Clear per-frame inputs.
    constexpr void clear_input() noexcept {
      yawInput   = 0.0f;
      pitchInput = 0.0f;
    }

    /// Get the current yaw as a quaternion (for syncing character rotation).
    constexpr Quat4f yaw_quaternion() const noexcept {
      return quat_from_axis_angle(Vec3f{0.0f, 1.0f, 0.0f}, yaw);
    }
  };

}  // namespace zs::threeC
