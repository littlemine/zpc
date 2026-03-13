#pragma once
/// @file MatrixBuilders.hpp
/// @brief View and projection matrix construction — lookAt, perspective, orthographic.
///
/// Conventions:
///   - Right-handed coordinate system, Y-up.
///   - Column-vector convention: translation lives in the last COLUMN of the 4×4 matrix.
///   - ZPC stores matrices ROW-MAJOR: m(row, col). So translation is m(0,3), m(1,3), m(2,3).
///   - Projection uses Vulkan clip space: Y-down, Z in [0, 1].
///
/// These are free functions in zs::gameplay, not methods, so they compose cleanly.

#include "zensim/gameplay/Core.hpp"
#include "zensim/ZpcMathUtils.hpp"

namespace zs::gameplay {

  // ── lookAt ──────────────────────────────────────────────────────────────

  /// Build a right-handed look-at view matrix.
  ///
  /// @param eye     Camera position in world space
  /// @param target  Point the camera looks at
  /// @param worldUp World up vector (typically {0,1,0})
  /// @return        4×4 view matrix that transforms world coords → camera coords
  ///
  /// Camera local axes:
  ///   forward = normalize(eye - target)  (points toward viewer, RH convention)
  ///   right   = normalize(cross(worldUp, forward))
  ///   up      = cross(forward, right)
  ///
  /// The matrix is:
  ///   | rx  ry  rz  -dot(r, eye) |
  ///   | ux  uy  uz  -dot(u, eye) |
  ///   | fx  fy  fz  -dot(f, eye) |
  ///   |  0   0   0       1       |
  inline constexpr Mat4f look_at(Vec3f eye, Vec3f target, Vec3f worldUp) noexcept {
    // Forward vector (from target toward eye, RH convention)
    const Vec3f f = (eye - target).normalized();
    // Right vector
    const Vec3f r = worldUp.cross(f).normalized();
    // Recomputed up vector
    const Vec3f u = f.cross(r);

    Mat4f m = Mat4f::zeros();
    // Row 0: right
    m(0, 0) = r(0);  m(0, 1) = r(1);  m(0, 2) = r(2);
    m(0, 3) = -(r(0) * eye(0) + r(1) * eye(1) + r(2) * eye(2));
    // Row 1: up
    m(1, 0) = u(0);  m(1, 1) = u(1);  m(1, 2) = u(2);
    m(1, 3) = -(u(0) * eye(0) + u(1) * eye(1) + u(2) * eye(2));
    // Row 2: forward (into screen)
    m(2, 0) = f(0);  m(2, 1) = f(1);  m(2, 2) = f(2);
    m(2, 3) = -(f(0) * eye(0) + f(1) * eye(1) + f(2) * eye(2));
    // Row 3: homogeneous
    m(3, 3) = 1.0f;
    return m;
  }

  // ── perspective ─────────────────────────────────────────────────────────

  /// Build a right-handed perspective projection matrix for Vulkan clip space.
  ///
  /// Vulkan conventions applied:
  ///   - Z maps to [0, 1] (not [-1, 1] like OpenGL).
  ///   - Y is flipped (row 1 negated) so clip-space Y points downward.
  ///
  /// @param fovY   Vertical field-of-view in radians
  /// @param aspect Aspect ratio (width / height)
  /// @param zNear  Near clip plane distance (must be > 0)
  /// @param zFar   Far clip plane distance (must be > zNear)
  /// @return       4×4 projection matrix
  inline constexpr Mat4f perspective(f32 fovY, f32 aspect, f32 zNear, f32 zFar) noexcept {
    const f32 halfFov = fovY * 0.5f;
    const f32 tanHalfFov = zs::sin(halfFov) / zs::cos(halfFov);

    Mat4f m = Mat4f::zeros();

    // X scale
    m(0, 0) = 1.0f / (aspect * tanHalfFov);
    // Y scale — negated for Vulkan Y-down
    m(1, 1) = -1.0f / tanHalfFov;
    // Z remap to [0, 1]: z' = zFar / (zNear - zFar) * z + zNear * zFar / (zNear - zFar)
    m(2, 2) = zFar / (zNear - zFar);
    m(2, 3) = zNear * zFar / (zNear - zFar);
    // Perspective divide
    m(3, 2) = -1.0f;

    return m;
  }

  // ── perspective (reversed-Z) ────────────────────────────────────────────

  /// Reversed-Z perspective projection: near plane maps to Z=1, far to Z=0.
  /// Provides better depth precision for large scenes.
  inline constexpr Mat4f perspective_reversed_z(f32 fovY, f32 aspect,
                                                 f32 zNear, f32 zFar) noexcept {
    const f32 halfFov = fovY * 0.5f;
    const f32 tanHalfFov = zs::sin(halfFov) / zs::cos(halfFov);

    Mat4f m = Mat4f::zeros();

    m(0, 0) = 1.0f / (aspect * tanHalfFov);
    m(1, 1) = -1.0f / tanHalfFov;   // Vulkan Y-down
    // Reversed-Z: z' = zNear / (zFar - zNear) * z + zFar * zNear / (zFar - zNear)
    m(2, 2) = zNear / (zFar - zNear);
    m(2, 3) = zFar * zNear / (zFar - zNear);
    m(3, 2) = -1.0f;

    return m;
  }

  // ── orthographic ────────────────────────────────────────────────────────

  /// Build a right-handed orthographic projection matrix for Vulkan clip space.
  ///
  /// @param left    Left clip plane
  /// @param right   Right clip plane
  /// @param bottom  Bottom clip plane
  /// @param top     Top clip plane
  /// @param zNear   Near clip plane
  /// @param zFar    Far clip plane
  /// @return        4×4 orthographic projection matrix (Y-down, Z [0,1])
  inline constexpr Mat4f orthographic(f32 left, f32 right, f32 bottom, f32 top,
                                       f32 zNear, f32 zFar) noexcept {
    Mat4f m = Mat4f::zeros();

    const f32 rl = right - left;
    const f32 tb = top - bottom;
    const f32 fn = zFar - zNear;

    m(0, 0) =  2.0f / rl;
    m(0, 3) = -(right + left) / rl;
    // Y negated for Vulkan Y-down
    m(1, 1) = -2.0f / tb;
    m(1, 3) =  (top + bottom) / tb;
    // Z maps to [0, 1]
    m(2, 2) = -1.0f / fn;
    m(2, 3) = -zNear / fn;
    m(3, 3) =  1.0f;

    return m;
  }

  // ── view_from_camera ────────────────────────────────────────────────────

  /// Construct a view matrix from Camera state (position + orientation quaternion).
  ///
  /// The camera orientation quaternion rotates from camera-local to world space.
  /// The view matrix is the inverse of that transform:
  ///   V = R^T * T^{-1}
  /// where R is the 3×3 rotation and T is the translation.
  inline constexpr Mat4f view_from_camera(Vec3f position, Quat4f orientation) noexcept {
    // Camera axes in world space
    const Vec3f r = quat_rotate(orientation, Vec3f{1.0f, 0.0f, 0.0f});
    const Vec3f u = quat_rotate(orientation, Vec3f{0.0f, 1.0f, 0.0f});
    const Vec3f f = quat_rotate(orientation, Vec3f{0.0f, 0.0f, 1.0f}); // +Z (forward points into screen for RH)

    Mat4f m = Mat4f::zeros();
    // Rows are transposed camera axes (R^T)
    m(0, 0) = r(0);  m(0, 1) = r(1);  m(0, 2) = r(2);
    m(0, 3) = -(r(0) * position(0) + r(1) * position(1) + r(2) * position(2));
    m(1, 0) = u(0);  m(1, 1) = u(1);  m(1, 2) = u(2);
    m(1, 3) = -(u(0) * position(0) + u(1) * position(1) + u(2) * position(2));
    m(2, 0) = f(0);  m(2, 1) = f(1);  m(2, 2) = f(2);
    m(2, 3) = -(f(0) * position(0) + f(1) * position(1) + f(2) * position(2));
    m(3, 3) = 1.0f;
    return m;
  }

}  // namespace zs::gameplay
