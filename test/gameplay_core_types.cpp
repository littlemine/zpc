/// @file gameplay_core_types.cpp
/// @brief Tests for Core.hpp (FrameContext, quaternion helpers) and Telemetry.hpp.

#include <cassert>
#include <cmath>
#include <cstdio>
#include "zensim/gameplay/Core.hpp"
#include "zensim/gameplay/Telemetry.hpp"

static bool near_eq(float a, float b, float eps = 1e-5f) {
  return std::fabs(a - b) < eps;
}

int main() {
  using namespace zs::gameplay;

  // ═══════════════════════════════════════════════════════════════════════
  //  Type aliases sanity checks
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[Core] type aliases... ");
    Vec2f v2{1.0f, 2.0f};
    assert(near_eq(v2(0), 1.0f));
    assert(near_eq(v2(1), 2.0f));

    Vec3f v3{1.0f, 2.0f, 3.0f};
    assert(near_eq(v3(2), 3.0f));

    Vec4f v4{1.0f, 2.0f, 3.0f, 4.0f};
    assert(near_eq(v4(3), 4.0f));

    Mat3f m3 = Mat3f::identity();
    assert(near_eq(m3(0, 0), 1.0f));
    assert(near_eq(m3(0, 1), 0.0f));

    Mat4f m4 = Mat4f::identity();
    assert(near_eq(m4(3, 3), 1.0f));
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  Constants
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[Core] constants... ");
    assert(near_eq(k_pi, 3.14159265f, 1e-5f));
    assert(near_eq(k_half_pi, 1.5707963f, 1e-5f));
    assert(near_eq(k_two_pi, 6.2831853f, 1e-5f));
    assert(near_eq(k_deg2rad * 180.0f, k_pi, 1e-5f));
    assert(near_eq(k_rad2deg * k_pi, 180.0f, 1e-3f));
    assert(k_epsilon > 0.0f);
    assert(k_epsilon < 1e-4f);
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  FrameContext
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[FrameContext] first frame... ");
    auto ctx = FrameContext::first(1.0f / 60.0f);
    assert(near_eq(static_cast<float>(ctx.time), 0.0f));
    assert(near_eq(ctx.dt, 1.0f / 60.0f));
    assert(ctx.frameNumber == 0);
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[FrameContext] next frame... ");
    auto ctx = FrameContext::first(1.0f / 60.0f);
    auto next = ctx.next(1.0f / 60.0f);
    assert(next.frameNumber == 1);
    assert(near_eq(static_cast<float>(next.time), 1.0f / 60.0f, 1e-6f));
    assert(near_eq(next.dt, 1.0f / 60.0f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[FrameContext] accumulating frames... ");
    auto ctx = FrameContext::first(0.016f);
    for (int i = 0; i < 100; ++i) {
      ctx = ctx.next(0.016f);
    }
    assert(ctx.frameNumber == 100);
    assert(near_eq(static_cast<float>(ctx.time), 100.0f * 0.016f, 1e-3f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[FrameContext] variable dt... ");
    auto ctx = FrameContext::first(0.016f);
    ctx = ctx.next(0.033f);  // 30 FPS frame
    assert(ctx.frameNumber == 1);
    assert(near_eq(ctx.dt, 0.033f));
    assert(near_eq(static_cast<float>(ctx.time), 0.016f, 1e-6f));
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  Quaternion helpers
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[identity_quat]... ");
    Quat4f q = identity_quat();
    assert(near_eq(q(0), 0.0f));
    assert(near_eq(q(1), 0.0f));
    assert(near_eq(q(2), 0.0f));
    assert(near_eq(q(3), 1.0f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[quat_from_axis_angle] 90 deg around Y... ");
    Vec3f yAxis{0.0f, 1.0f, 0.0f};
    Quat4f q = quat_from_axis_angle(yAxis, k_half_pi);
    float expected_s = std::sin(k_half_pi * 0.5f);
    float expected_c = std::cos(k_half_pi * 0.5f);
    assert(near_eq(q(0), 0.0f));
    assert(near_eq(q(1), expected_s, 1e-5f));
    assert(near_eq(q(2), 0.0f));
    assert(near_eq(q(3), expected_c, 1e-5f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[quat_conjugate]... ");
    Quat4f q{0.1f, 0.2f, 0.3f, 0.9f};
    Quat4f conj = quat_conjugate(q);
    assert(near_eq(conj(0), -0.1f));
    assert(near_eq(conj(1), -0.2f));
    assert(near_eq(conj(2), -0.3f));
    assert(near_eq(conj(3), 0.9f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[quat_multiply] identity * q = q... ");
    Quat4f id = identity_quat();
    Quat4f q = quat_from_axis_angle(Vec3f{0.0f, 1.0f, 0.0f}, 0.5f);
    Quat4f result = quat_multiply(id, q);
    for (int i = 0; i < 4; ++i) {
      assert(near_eq(result(i), q(i), 1e-5f));
    }
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[quat_multiply] q * conj(q) = identity... ");
    Quat4f q = quat_from_axis_angle(Vec3f{1.0f, 0.0f, 0.0f}, 1.2f);
    q = q.normalized();
    Quat4f conj = quat_conjugate(q);
    Quat4f result = quat_multiply(q, conj);
    assert(near_eq(std::fabs(result(3)), 1.0f, 1e-5f));
    assert(near_eq(result(0), 0.0f, 1e-5f));
    assert(near_eq(result(1), 0.0f, 1e-5f));
    assert(near_eq(result(2), 0.0f, 1e-5f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[quat_rotate] rotate +X by 90 deg around Z... ");
    Quat4f q = quat_from_axis_angle(Vec3f{0.0f, 0.0f, 1.0f}, k_half_pi);
    Vec3f v{1.0f, 0.0f, 0.0f};
    Vec3f result = quat_rotate(q, v);
    // +X rotated 90 deg around Z should give +Y
    assert(near_eq(result(0), 0.0f, 1e-4f));
    assert(near_eq(result(1), 1.0f, 1e-4f));
    assert(near_eq(result(2), 0.0f, 1e-4f));
    fprintf(stderr, "ok\n");
  }

  {
    fprintf(stderr, "[quat_rotate] identity rotation preserves vector... ");
    Quat4f id = identity_quat();
    Vec3f v{3.0f, -2.0f, 7.0f};
    Vec3f result = quat_rotate(id, v);
    for (int i = 0; i < 3; ++i) {
      assert(near_eq(result(i), v(i), 1e-5f));
    }
    fprintf(stderr, "ok\n");
  }

  // ═══════════════════════════════════════════════════════════════════════
  //  FrameTelemetry
  // ═══════════════════════════════════════════════════════════════════════
  {
    fprintf(stderr, "[FrameTelemetry] zero initialization... ");
    auto telem = FrameTelemetry::zero(42);
    assert(telem.frameNumber == 42);
    assert(near_eq(static_cast<float>(telem.inputPhaseMs), 0.0f));
    assert(near_eq(static_cast<float>(telem.characterPhaseMs), 0.0f));
    assert(near_eq(static_cast<float>(telem.cameraPhaseMs), 0.0f));
    assert(near_eq(static_cast<float>(telem.totalPhaseMs), 0.0f));
    assert(near_eq(telem.inputToScreenLatencyMs, 0.0f));
    assert(near_eq(telem.cameraJitterPx, 0.0f));
    assert(near_eq(telem.cameraSmoothness, 1.0f));
    assert(telem.stateTransitionsThisFrame == 0);
    assert(near_eq(telem.velocityMagnitude, 0.0f));
    fprintf(stderr, "ok\n");
  }

  fprintf(stderr, "\n=== All gameplay core type tests passed ===\n");
  return 0;
}
