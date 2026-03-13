#pragma once
/// @file Frustum.hpp
/// @brief View-frustum extraction and containment tests.
///
/// Extracts 6 frustum planes from a view-projection matrix and provides
/// fast sphere/point containment checks for culling.
///
/// Plane representation: Vec4f where (x, y, z) is the inward-facing normal
/// and w is the signed distance. A point p is inside the half-space when
/// dot(normal, p) + w >= 0.

#include "zensim/3c/Core.hpp"

namespace zs::threeC {

  /// Frustum plane indices.
  enum class FrustumPlane : u8 {
    left   = 0,
    right  = 1,
    bottom = 2,
    top    = 3,
    near_  = 4,  ///< trailing underscore avoids conflict with `near` macro on MSVC
    far_   = 5
  };

  /// A view frustum defined by 6 half-space planes.
  struct Frustum {
    Vec4f planes[6];  ///< Each plane: (nx, ny, nz, d).  Inside when dot(n,p)+d >= 0.

    // ── Extraction ────────────────────────────────────────────────────────

    /// Extract frustum planes from a composite view-projection matrix (VP = P * V).
    ///
    /// Uses the Gribb-Hartmann method: each plane is a linear combination of
    /// the rows of the VP matrix.  Planes are normalised so that
    /// |normal| == 1, which lets distance tests return world-space distances.
    ///
    /// ZPC matrices are ROW-MAJOR: vp(row, col).  The VP matrix multiplies a
    /// column-vector point: clip = VP * p_world.  Gribb-Hartmann uses the
    /// ROWS of this matrix:
    ///   row_i = (vp(i,0), vp(i,1), vp(i,2), vp(i,3))
    static constexpr Frustum from_view_projection(Mat4f const& vp) noexcept {
      Frustum fr{};

      // Helper: row i of the matrix as Vec4f
      auto row = [&](int i) -> Vec4f {
        return Vec4f{vp(i, 0), vp(i, 1), vp(i, 2), vp(i, 3)};
      };

      const Vec4f r0 = row(0);
      const Vec4f r1 = row(1);
      const Vec4f r2 = row(2);
      const Vec4f r3 = row(3);

      // Gribb-Hartmann plane extraction (Vulkan Z [0,1])
      // Left:   row3 + row0
      // Right:  row3 - row0
      // Bottom: row3 + row1
      // Top:    row3 - row1
      // Near:   row2           (Vulkan: z >= 0)
      // Far:    row3 - row2    (Vulkan: z <= 1)
      auto add4 = [](Vec4f a, Vec4f b) -> Vec4f {
        return Vec4f{a(0)+b(0), a(1)+b(1), a(2)+b(2), a(3)+b(3)};
      };
      auto sub4 = [](Vec4f a, Vec4f b) -> Vec4f {
        return Vec4f{a(0)-b(0), a(1)-b(1), a(2)-b(2), a(3)-b(3)};
      };

      fr.planes[0] = add4(r3, r0); // left
      fr.planes[1] = sub4(r3, r0); // right
      fr.planes[2] = add4(r3, r1); // bottom
      fr.planes[3] = sub4(r3, r1); // top
      fr.planes[4] = r2;           // near  (Vulkan Z [0,1])
      fr.planes[5] = sub4(r3, r2); // far

      // Normalise each plane so |n| = 1
      for (int i = 0; i < 6; ++i) {
        const Vec4f& p = fr.planes[i];
        const f32 len = zs::sqrt(p(0)*p(0) + p(1)*p(1) + p(2)*p(2));
        if (len > k_epsilon) {
          const f32 inv = 1.0f / len;
          fr.planes[i] = Vec4f{p(0)*inv, p(1)*inv, p(2)*inv, p(3)*inv};
        }
      }

      return fr;
    }

    // ── Containment tests ─────────────────────────────────────────────────

    /// Test whether a point is inside (or on the boundary of) all 6 planes.
    constexpr bool contains_point(Vec3f const& p) const noexcept {
      for (int i = 0; i < 6; ++i) {
        const Vec4f& pl = planes[i];
        const f32 dist = pl(0)*p(0) + pl(1)*p(1) + pl(2)*p(2) + pl(3);
        if (dist < 0.0f) return false;
      }
      return true;
    }

    /// Result of a sphere-frustum test.
    enum class SphereResult : u8 {
      outside,      ///< Sphere is fully outside at least one plane
      intersecting, ///< Sphere straddles at least one plane
      inside        ///< Sphere is fully inside all planes
    };

    /// Test a bounding sphere against the frustum.
    ///
    /// @param center  Sphere center in world space
    /// @param radius  Sphere radius (must be >= 0)
    /// @return        outside / intersecting / inside
    constexpr SphereResult contains_sphere(Vec3f const& center, f32 radius) const noexcept {
      bool allInside = true;
      for (int i = 0; i < 6; ++i) {
        const Vec4f& pl = planes[i];
        const f32 dist = pl(0)*center(0) + pl(1)*center(1) + pl(2)*center(2) + pl(3);
        if (dist < -radius) return SphereResult::outside;
        if (dist <  radius) allInside = false;
      }
      return allInside ? SphereResult::inside : SphereResult::intersecting;
    }
  };

}  // namespace zs::threeC
