#pragma once
/// @file Camera.hpp
/// @brief Camera data types — Camera state, CameraOutput, mode identifiers.
///
/// Pure data structures. No update logic lives here; that is in CameraMode.hpp.
/// Conventions:
///   - Right-handed coordinate system, Y-up.
///   - Orientation stored as a unit quaternion (x, y, z, w).
///   - FOV is vertical, in radians.

#include "zensim/gameplay/Core.hpp"

namespace zs::gameplay {

  // ── Camera mode identifiers ─────────────────────────────────────────────

  /// Identifies the active camera behaviour mode.
  enum class CameraModeId : u8 {
    orbit,     ///< Orbits around a target pivot point
    follow,    ///< Follows a target with a spring-damped arm
    fps,       ///< First-person shooter (locked to character head)
    rail,      ///< Follows a pre-authored spline rail
    free_fly,  ///< Free-flying debug camera
    custom     ///< User-defined mode via callback
  };

  // ── Camera struct ───────────────────────────────────────────────────────

  /// Immutable snapshot of intrinsic + extrinsic camera parameters.
  /// Does NOT contain derived matrices — see CameraOutput for those.
  struct Camera {
    Vec3f  position    {0.0f, 0.0f, 0.0f};   ///< World-space eye position
    Quat4f orientation {0.0f, 0.0f, 0.0f, 1.0f}; ///< Unit quaternion (identity = look along -Z)
    f32    fovY        {60.0f * k_deg2rad};   ///< Vertical field of view (radians)
    f32    nearPlane   {0.1f};                ///< Near clip distance
    f32    farPlane    {1000.0f};             ///< Far clip distance
    f32    aspectRatio {16.0f / 9.0f};        ///< Width / height

    /// Derived: the forward direction (-Z in camera local frame, rotated).
    constexpr Vec3f forward() const noexcept {
      return quat_rotate(orientation, Vec3f{0.0f, 0.0f, -1.0f});
    }
    /// Derived: the right direction (+X in camera local frame, rotated).
    constexpr Vec3f right() const noexcept {
      return quat_rotate(orientation, Vec3f{1.0f, 0.0f, 0.0f});
    }
    /// Derived: the up direction (+Y in camera local frame, rotated).
    constexpr Vec3f up() const noexcept {
      return quat_rotate(orientation, Vec3f{0.0f, 1.0f, 0.0f});
    }
  };

  // ── CameraOutput ────────────────────────────────────────────────────────

  /// The complete output produced each frame by the camera system.
  /// Contains the final camera state plus pre-computed matrices.
  struct CameraOutput {
    Camera      camera;                          ///< Final camera state this frame
    Mat4f       view        {Mat4f::identity()};  ///< View matrix (world → camera)
    Mat4f       projection  {Mat4f::identity()};  ///< Projection matrix (camera → clip)
    CameraModeId activeMode {CameraModeId::orbit};
    CameraModeId blendFrom  {CameraModeId::orbit}; ///< Mode being blended away from
    f32         blendAlpha  {1.0f};               ///< 1.0 = fully in activeMode
  };

}  // namespace zs::gameplay
