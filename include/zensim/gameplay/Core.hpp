#pragma once
/// @file Core.hpp
/// @brief Gameplay system core types — shared aliases, FrameContext, constants.
///
/// This header is the root dependency for all Gameplay modules. It provides
/// vector/matrix type aliases, the per-frame context structure, and
/// common constants used throughout the Camera, Character, Control system.

#include "zensim/TypeAlias.hpp"
#include "zensim/math/Vec.h"

namespace zs::gameplay {

  // ── Type aliases ───────────────────────────────────────────────────────

  using Vec2f = zs::vec<f32, 2>;
  using Vec3f = zs::vec<f32, 3>;
  using Vec4f = zs::vec<f32, 4>;
  using Mat3f = zs::vec<f32, 3, 3>;
  using Mat4f = zs::vec<f32, 4, 4>;

  /// Quaternion stored as vec<f32, 4> with (x, y, z, w) layout.
  /// Use Rotation<f32, 3>::quaternionMultiply() for multiplication,
  /// and Rotation<f32, 3>::quaternion2matrix() for matrix conversion.
  using Quat4f = zs::vec<f32, 4>;

  // ── Constants ──────────────────────────────────────────────────────────

  inline constexpr f32 k_pi = static_cast<f32>(zs::g_pi);
  inline constexpr f32 k_half_pi = static_cast<f32>(zs::g_half_pi);
  inline constexpr f32 k_two_pi = k_pi * 2.0f;
  inline constexpr f32 k_deg2rad = k_pi / 180.0f;
  inline constexpr f32 k_rad2deg = 180.0f / k_pi;
  inline constexpr f32 k_epsilon = 1e-6f;

  // ── FrameContext ───────────────────────────────────────────────────────

  /// Per-frame timing and sequencing information, passed to every Gameplay update phase.
  struct FrameContext {
    f64 time;          ///< Absolute time in seconds since simulation start
    f32 dt;            ///< Delta time this frame (seconds)
    u64 frameNumber;   ///< Monotonically increasing frame counter

    /// Construct a FrameContext for the first frame
    static constexpr FrameContext first(f32 dt_) noexcept {
      return FrameContext{0.0, dt_, 0};
    }

    /// Advance to the next frame
    constexpr FrameContext next(f32 dt_) const noexcept {
      return FrameContext{time + static_cast<f64>(dt_), dt_, frameNumber + 1};
    }
  };

  // ── Utility: identity quaternion ───────────────────────────────────────

  /// Returns the identity quaternion (0, 0, 0, 1) representing no rotation.
  inline constexpr Quat4f identity_quat() noexcept {
    return Quat4f{0.0f, 0.0f, 0.0f, 1.0f};
  }

  /// Returns a quaternion from axis-angle representation.
  /// @param axis Normalized rotation axis
  /// @param angle Rotation angle in radians
  inline constexpr Quat4f quat_from_axis_angle(Vec3f axis, f32 angle) noexcept {
    const f32 half = angle * 0.5f;
    const f32 s = zs::sin(half);
    const f32 c = zs::cos(half);
    return Quat4f{axis(0) * s, axis(1) * s, axis(2) * s, c};
  }

  /// Conjugate of a quaternion (negates xyz, keeps w).
  inline constexpr Quat4f quat_conjugate(Quat4f const& q) noexcept {
    return Quat4f{-q(0), -q(1), -q(2), q(3)};
  }

  /// Multiply two quaternions: a * b.
  /// Implemented directly because Rotation::quaternionMultiply has a
  /// const-correctness issue with MSVC (the w/x/y/z accessors are non-const).
  /// Layout: (0=x, 1=y, 2=z, 3=w).
  inline constexpr Quat4f quat_multiply(Quat4f const& a, Quat4f const& b) noexcept {
    const f32 ax = a(0), ay = a(1), az = a(2), aw = a(3);
    const f32 bx = b(0), by = b(1), bz = b(2), bw = b(3);
    return Quat4f{
      aw * bx + ax * bw + ay * bz - az * by,
      aw * by - ax * bz + ay * bw + az * bx,
      aw * bz + ax * by - ay * bx + az * bw,
      aw * bw - ax * bx - ay * by - az * bz
    };
  }

  /// Rotate a 3D vector by a quaternion: q * v * q_conjugate
  inline constexpr Vec3f quat_rotate(Quat4f const& q, Vec3f const& v) noexcept {
    // Expand q * (vx, vy, vz, 0) * conj(q) using the standard formula
    const f32 qx = q(0), qy = q(1), qz = q(2), qw = q(3);
    // t = 2 * cross(q.xyz, v)
    const f32 tx = 2.0f * (qy * v(2) - qz * v(1));
    const f32 ty = 2.0f * (qz * v(0) - qx * v(2));
    const f32 tz = 2.0f * (qx * v(1) - qy * v(0));
    // result = v + qw * t + cross(q.xyz, t)
    return Vec3f{
      v(0) + qw * tx + (qy * tz - qz * ty),
      v(1) + qw * ty + (qz * tx - qx * tz),
      v(2) + qw * tz + (qx * ty - qy * tx)
    };
  }

}  // namespace zs::gameplay
