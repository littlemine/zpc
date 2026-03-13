/// @file 3c_math_extensions.cpp
/// @brief Tests for slerp, nlerp, matrix2quaternion, damp, spring_damper, remap, lerp.

#include <cassert>
#include <cmath>
#include <cstdio>
#include "zensim/3c/MathExtensions.hpp"
#include "zensim/math/Rotation.hpp"

static bool near_eq(float a, float b, float eps = 1e-5f) {
  return std::fabs(a - b) < eps;
}

static float quat_dot(zs::vec<float, 4> const& a, zs::vec<float, 4> const& b) {
  return a(0) * b(0) + a(1) * b(1) + a(2) * b(2) + a(3) * b(3);
}

static float quat_length(zs::vec<float, 4> const& q) {
  return std::sqrt(q(0) * q(0) + q(1) * q(1) + q(2) * q(2) + q(3) * q(3));
}

int main() {
  using namespace zs;
  using Rot3 = Rotation<float, 3>;
  using Q = vec<float, 4>;
  using V3 = vec<float, 3>;

  // ═══════════════════════════════════════════════════════════════════════
  //  matrix2quaternion: round-trip test
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[matrix2quaternion] round-trip identity... ");
    auto identity_mat = Rot3{};  // identity matrix
    Q q = Rot3::matrix2quaternion(identity_mat);
    // Identity quaternion should be (0, 0, 0, 1) or (0, 0, 0, -1)
    assert(near_eq(std::fabs(q(3)), 1.0f, 1e-5f));
    assert(near_eq(q(0), 0.0f, 1e-5f));
    assert(near_eq(q(1), 0.0f, 1e-5f));
    assert(near_eq(q(2), 0.0f, 1e-5f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[matrix2quaternion] 90-degree Y rotation round-trip... ");
    // Create a 90-degree rotation around Y axis
    V3 yAxis{0.0f, 1.0f, 0.0f};
    float angle90 = static_cast<float>(g_half_pi);
    Rot3 rot90(yAxis, angle90);

    // Convert to quaternion and back
    Q q = Rot3::matrix2quaternion(rot90);
    assert(near_eq(quat_length(q), 1.0f, 1e-5f));  // unit quaternion

    auto mat_back = Rot3::quaternion2matrix(q);
    // Verify all 9 matrix entries match
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c)
        assert(near_eq(rot90(r, c), mat_back(r, c), 1e-4f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[matrix2quaternion] 180-degree Z rotation... ");
    V3 zAxis{0.0f, 0.0f, 1.0f};
    float angle180 = static_cast<float>(g_pi);
    Rot3 rot180(zAxis, angle180);
    Q q = Rot3::matrix2quaternion(rot180);
    assert(near_eq(quat_length(q), 1.0f, 1e-5f));

    auto mat_back = Rot3::quaternion2matrix(q);
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c)
        assert(near_eq(rot180(r, c), mat_back(r, c), 1e-4f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[matrix2quaternion] arbitrary rotation round-trip... ");
    V3 axis{1.0f, 1.0f, 1.0f};
    float angle = 1.23f;
    Rot3 rot(axis, angle);
    Q q = Rot3::matrix2quaternion(rot);
    assert(near_eq(quat_length(q), 1.0f, 1e-5f));

    auto mat_back = Rot3::quaternion2matrix(q);
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c)
        assert(near_eq(rot(r, c), mat_back(r, c), 1e-4f));
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  quaternionSlerp: basic tests
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[quaternionSlerp] t=0 returns q0... ");
    Q q0{0.0f, 0.0f, 0.0f, 1.0f};  // identity
    float halfAngle = 0.5f;
    float s = std::sin(halfAngle);
    float c = std::cos(halfAngle);
    Q q1{0.0f, s, 0.0f, c};  // ~57 deg around Y

    Q result = Rot3::quaternionSlerp(q0, q1, 0.0f);
    assert(near_eq(result(0), q0(0)));
    assert(near_eq(result(1), q0(1)));
    assert(near_eq(result(2), q0(2)));
    assert(near_eq(result(3), q0(3)));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[quaternionSlerp] t=1 returns q1... ");
    Q q0{0.0f, 0.0f, 0.0f, 1.0f};
    float halfAngle = 0.5f;
    float s = std::sin(halfAngle);
    float c = std::cos(halfAngle);
    Q q1{0.0f, s, 0.0f, c};

    Q result = Rot3::quaternionSlerp(q0, q1, 1.0f);
    assert(near_eq(result(0), q1(0)));
    assert(near_eq(result(1), q1(1)));
    assert(near_eq(result(2), q1(2)));
    assert(near_eq(result(3), q1(3)));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[quaternionSlerp] t=0.5 halfway rotation... ");
    Q q0{0.0f, 0.0f, 0.0f, 1.0f};
    // 90 degrees around Y: half-angle = pi/4
    float ha = static_cast<float>(g_half_pi) * 0.5f;
    Q q1{0.0f, std::sin(ha), 0.0f, std::cos(ha)};

    Q mid = Rot3::quaternionSlerp(q0, q1, 0.5f);
    assert(near_eq(quat_length(mid), 1.0f, 1e-5f));

    // The midpoint should be a 45-degree rotation around Y
    float expected_ha = ha * 0.5f;
    Q expected{0.0f, std::sin(expected_ha), 0.0f, std::cos(expected_ha)};
    float dot = std::fabs(quat_dot(mid, expected));
    assert(near_eq(dot, 1.0f, 1e-4f));  // same rotation (up to sign)
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[quaternionSlerp] antipodal quaternions (shortest path)... ");
    Q q0{0.0f, 0.0f, 0.0f, 1.0f};
    Q q1_neg{0.0f, 0.0f, 0.0f, -1.0f};  // same rotation, opposite sign
    Q result = Rot3::quaternionSlerp(q0, q1_neg, 0.5f);
    // Should still produce identity (or near-identity)
    assert(near_eq(std::fabs(result(3)), 1.0f, 1e-3f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[quaternionSlerp] unit length preserved... ");
    V3 axis1{1.0f, 0.0f, 0.0f};
    V3 axis2{0.0f, 0.0f, 1.0f};
    Rot3 r1(axis1, 0.7f);
    Rot3 r2(axis2, 2.1f);
    Q q0 = Rot3::matrix2quaternion(r1);
    Q q1 = Rot3::matrix2quaternion(r2);

    for (float t = 0.0f; t <= 1.01f; t += 0.1f) {
      Q result = Rot3::quaternionSlerp(q0, q1, t > 1.0f ? 1.0f : t);
      assert(near_eq(quat_length(result), 1.0f, 1e-5f));
    }
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  quaternionNlerp: basic tests
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[quaternionNlerp] endpoints and unit length... ");
    Q q0{0.0f, 0.0f, 0.0f, 1.0f};
    float ha = 0.3f;
    Q q1{std::sin(ha), 0.0f, 0.0f, std::cos(ha)};

    Q at0 = Rot3::quaternionNlerp(q0, q1, 0.0f);
    assert(near_eq(quat_dot(at0, q0), 1.0f, 1e-5f));

    Q at1 = Rot3::quaternionNlerp(q0, q1, 1.0f);
    assert(near_eq(std::fabs(quat_dot(at1, q1)), 1.0f, 1e-5f));

    // Always unit length
    for (float t = 0.0f; t <= 1.01f; t += 0.1f) {
      Q r = Rot3::quaternionNlerp(q0, q1, t > 1.0f ? 1.0f : t);
      assert(near_eq(quat_length(r), 1.0f, 1e-5f));
    }
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  Free-function slerp (MathExtensions.hpp)
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[slerp free-function] consistency with Rotation::quaternionSlerp... ");
    V3 axis{0.5f, 0.7f, 0.3f};
    Rot3 r1(axis, 0.8f);
    Rot3 r2(axis, 2.4f);
    Q q0 = Rot3::matrix2quaternion(r1);
    Q q1 = Rot3::matrix2quaternion(r2);

    for (float t = 0.0f; t <= 1.01f; t += 0.2f) {
      float tc = t > 1.0f ? 1.0f : t;
      Q from_method = Rot3::quaternionSlerp(q0, q1, tc);
      Q from_free = slerp(q0, q1, tc);
      float dot = std::fabs(quat_dot(from_method, from_free));
      assert(near_eq(dot, 1.0f, 1e-4f));
    }
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  slerp precision requirement: <= 1e-6
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[slerp] precision <= 1e-6... ");
    // 60 degrees around Y axis
    float ha = static_cast<float>(g_pi) / 6.0f;
    Q q0{0.0f, 0.0f, 0.0f, 1.0f};
    Q q1{0.0f, std::sin(ha), 0.0f, std::cos(ha)};

    // At t=0.5, the result should be a 30-degree rotation around Y
    Q mid = slerp(q0, q1, 0.5f);
    float expected_ha = ha * 0.5f;
    Q expected{0.0f, std::sin(expected_ha), 0.0f, std::cos(expected_ha)};

    // Check component-wise precision
    for (int i = 0; i < 4; ++i) {
      assert(near_eq(mid(i), expected(i), 1e-6f));
    }
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  damp (exponential damping)
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[damp scalar] moves toward target... ");
    float current = 0.0f;
    float target = 10.0f;
    float result = damp(current, target, 5.0f, 0.016f);
    assert(result > current && result < target);
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[damp scalar] converges over time... ");
    float value = 0.0f;
    float target = 1.0f;
    for (int i = 0; i < 1000; ++i) {
      value = damp(value, target, 10.0f, 0.016f);
    }
    assert(near_eq(value, target, 1e-4f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[damp vector] 3D converges... ");
    auto current = vec<float, 3>{0.0f, 0.0f, 0.0f};
    auto target = vec<float, 3>{5.0f, -3.0f, 7.0f};
    for (int i = 0; i < 1000; ++i) {
      current = damp(current, target, 10.0f, 0.016f);
    }
    for (int i = 0; i < 3; ++i) {
      assert(near_eq(current(i), target(i), 1e-3f));
    }
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  spring_damper (critically damped spring)
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[spring_damper] converges to target... ");
    auto pos = vec<float, 3>{0.0f, 0.0f, 0.0f};
    auto vel = vec<float, 3>{0.0f, 0.0f, 0.0f};
    auto target = vec<float, 3>{10.0f, -5.0f, 3.0f};
    for (int i = 0; i < 2000; ++i) {
      spring_damper(pos, vel, target, 20.0f, 1.0f, 0.016f);
    }
    for (int i = 0; i < 3; ++i) {
      assert(near_eq(pos(i), target(i), 0.1f));
    }
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[spring_damper] velocity approaches zero at rest... ");
    auto pos = vec<float, 3>{0.0f, 0.0f, 0.0f};
    auto vel = vec<float, 3>{100.0f, 0.0f, 0.0f};  // initial velocity
    auto target = vec<float, 3>{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 2000; ++i) {
      spring_damper(pos, vel, target, 20.0f, 1.0f, 0.016f);
    }
    for (int i = 0; i < 3; ++i) {
      assert(near_eq(vel(i), 0.0f, 0.1f));
    }
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  remap / remap_unclamped
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[remap] basic mapping... ");
    assert(near_eq(remap(0.5f, 0.0f, 1.0f, 0.0f, 100.0f), 50.0f));
    assert(near_eq(remap(0.0f, 0.0f, 1.0f, 10.0f, 20.0f), 10.0f));
    assert(near_eq(remap(1.0f, 0.0f, 1.0f, 10.0f, 20.0f), 20.0f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[remap] clamping... ");
    assert(near_eq(remap(-0.5f, 0.0f, 1.0f, 0.0f, 100.0f), 0.0f));   // below min
    assert(near_eq(remap(1.5f, 0.0f, 1.0f, 0.0f, 100.0f), 100.0f));  // above max
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[remap_unclamped] extrapolation... ");
    assert(near_eq(remap_unclamped(-0.5f, 0.0f, 1.0f, 0.0f, 100.0f), -50.0f));
    assert(near_eq(remap_unclamped(1.5f, 0.0f, 1.0f, 0.0f, 100.0f), 150.0f));
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  lerp
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[lerp] scalar... ");
    assert(near_eq(lerp(0.0f, 10.0f, 0.0f), 0.0f));
    assert(near_eq(lerp(0.0f, 10.0f, 0.5f), 5.0f));
    assert(near_eq(lerp(0.0f, 10.0f, 1.0f), 10.0f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[lerp] vector... ");
    auto a = vec<float, 3>{0.0f, 0.0f, 0.0f};
    auto b = vec<float, 3>{10.0f, 20.0f, 30.0f};
    auto mid = lerp(a, b, 0.5f);
    assert(near_eq(mid(0), 5.0f));
    assert(near_eq(mid(1), 10.0f));
    assert(near_eq(mid(2), 15.0f));
    fprintf(stderr, "ok\n");
  }

  fprintf(stderr, "\n=== All 3C math extension tests passed ===\n");
  return 0;
}
