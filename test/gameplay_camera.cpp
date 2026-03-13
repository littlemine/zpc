/// @file gameplay_camera.cpp
/// @brief Tests for Camera, MatrixBuilders, Frustum, OrbitMode, FollowMode.

#include <cassert>
#include <cmath>
#include <cstdio>
#include "zensim/gameplay/camera/Camera.hpp"
#include "zensim/gameplay/camera/MatrixBuilders.hpp"
#include "zensim/gameplay/camera/Frustum.hpp"
#include "zensim/gameplay/camera/CameraMode.hpp"

static bool near_eq(float a, float b, float eps = 1e-4f) {
  return std::fabs(a - b) < eps;
}

static bool vec3_eq(zs::gameplay::Vec3f a, zs::gameplay::Vec3f b, float eps = 1e-4f) {
  return near_eq(a(0), b(0), eps) && near_eq(a(1), b(1), eps) && near_eq(a(2), b(2), eps);
}

static float dot3(zs::gameplay::Vec3f a, zs::gameplay::Vec3f b) {
  return a(0) * b(0) + a(1) * b(1) + a(2) * b(2);
}

static float len3(zs::gameplay::Vec3f v) {
  return std::sqrt(dot3(v, v));
}

int main() {
  using namespace zs;
  using namespace zs::gameplay;

  // =====================================================================
  //  Camera: default construction
  // =====================================================================
  {
    fprintf(stderr, "[Camera] default construction... ");
    Camera cam;
    assert(near_eq(cam.position(0), 0.0f));
    assert(near_eq(cam.position(1), 0.0f));
    assert(near_eq(cam.position(2), 0.0f));
    // Identity quaternion: (0,0,0,1)
    assert(near_eq(cam.orientation(0), 0.0f));
    assert(near_eq(cam.orientation(1), 0.0f));
    assert(near_eq(cam.orientation(2), 0.0f));
    assert(near_eq(cam.orientation(3), 1.0f));
    assert(near_eq(cam.nearPlane, 0.1f));
    assert(near_eq(cam.farPlane, 1000.0f));
    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  Camera: direction queries (identity orientation)
  // =====================================================================
  {
    fprintf(stderr, "[Camera] direction queries (identity)... ");
    Camera cam;
    // Identity quat: look along -Z, right = +X, up = +Y
    Vec3f fwd = cam.forward();
    Vec3f rt = cam.right();
    Vec3f up = cam.up();
    assert(vec3_eq(fwd, Vec3f{0.0f, 0.0f, -1.0f}));
    assert(vec3_eq(rt,  Vec3f{1.0f, 0.0f,  0.0f}));
    assert(vec3_eq(up,  Vec3f{0.0f, 1.0f,  0.0f}));
    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  Camera: direction queries (90-degree yaw rotation)
  // =====================================================================
  {
    fprintf(stderr, "[Camera] direction queries (90deg yaw)... ");
    Camera cam;
    // Rotate 90 degrees around Y (yaw left): forward should become +X
    cam.orientation = quat_from_axis_angle(Vec3f{0.0f, 1.0f, 0.0f}, k_half_pi);
    Vec3f fwd = cam.forward();
    Vec3f rt = cam.right();
    // After 90-deg yaw: forward = +X, right = +Z  (RH convention)
    assert(vec3_eq(fwd, Vec3f{1.0f, 0.0f, 0.0f}, 1e-3f));
    assert(vec3_eq(rt,  Vec3f{0.0f, 0.0f, 1.0f}, 1e-3f));
    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  CameraOutput: default construction
  // =====================================================================
  {
    fprintf(stderr, "[CameraOutput] defaults... ");
    CameraOutput output;
    assert(output.activeMode == CameraModeId::orbit);
    assert(near_eq(output.blendAlpha, 1.0f));
    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  look_at: camera at (0,0,5) looking at origin
  // =====================================================================
  {
    fprintf(stderr, "[look_at] camera at (0,0,5) -> origin... ");
    Vec3f eye{0.0f, 0.0f, 5.0f};
    Vec3f target{0.0f, 0.0f, 0.0f};
    Vec3f up{0.0f, 1.0f, 0.0f};
    Mat4f V = look_at(eye, target, up);

    // The view matrix should transform the eye position to the origin
    // V * eye_h = (0, 0, 0, 1)
    // eye_h = (0, 0, 5, 1)
    float vx = V(0,0)*0.0f + V(0,1)*0.0f + V(0,2)*5.0f + V(0,3);
    float vy = V(1,0)*0.0f + V(1,1)*0.0f + V(1,2)*5.0f + V(1,3);
    float vz = V(2,0)*0.0f + V(2,1)*0.0f + V(2,2)*5.0f + V(2,3);
    assert(near_eq(vx, 0.0f, 1e-3f));
    assert(near_eq(vy, 0.0f, 1e-3f));
    assert(near_eq(vz, 0.0f, 1e-3f));

    // The origin (target) should map to (0, 0, -5) in camera space
    // (RH: objects in front have negative Z after view transform)
    // Wait: our look_at uses forward = eye - target = (0,0,5), which is +Z.
    // So the forward row is (0,0,1,...), and objects at origin have
    // vz = dot(f, origin) + (-dot(f, eye)) = 0 + (-5) = -5
    // Actually let's recheck: the forward vector in our look_at is eye - target normalized.
    // That means the third row of V encodes the "forward" direction.
    // For the origin: V * (0,0,0,1) = (V(0,3), V(1,3), V(2,3), 1)
    float ox = V(0,3);
    float oy = V(1,3);
    float oz = V(2,3);
    assert(near_eq(ox, 0.0f, 1e-3f));
    assert(near_eq(oy, 0.0f, 1e-3f));
    assert(near_eq(oz, -5.0f, 1e-3f));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  look_at: off-axis camera
  // =====================================================================
  {
    fprintf(stderr, "[look_at] off-axis camera... ");
    Vec3f eye{3.0f, 4.0f, 5.0f};
    Vec3f target{0.0f, 0.0f, 0.0f};
    Vec3f up{0.0f, 1.0f, 0.0f};
    Mat4f V = look_at(eye, target, up);

    // V * eye should give (0,0,0)
    float vx = V(0,0)*eye(0) + V(0,1)*eye(1) + V(0,2)*eye(2) + V(0,3);
    float vy = V(1,0)*eye(0) + V(1,1)*eye(1) + V(1,2)*eye(2) + V(1,3);
    float vz = V(2,0)*eye(0) + V(2,1)*eye(1) + V(2,2)*eye(2) + V(2,3);
    assert(near_eq(vx, 0.0f, 1e-3f));
    assert(near_eq(vy, 0.0f, 1e-3f));
    assert(near_eq(vz, 0.0f, 1e-3f));

    // V should be orthonormal (upper-left 3x3 rows are orthogonal unit vectors)
    for (int r = 0; r < 3; ++r) {
      float rowLen = std::sqrt(V(r,0)*V(r,0) + V(r,1)*V(r,1) + V(r,2)*V(r,2));
      assert(near_eq(rowLen, 1.0f, 1e-3f));
    }
    // Rows should be mutually orthogonal
    float d01 = V(0,0)*V(1,0) + V(0,1)*V(1,1) + V(0,2)*V(1,2);
    float d02 = V(0,0)*V(2,0) + V(0,1)*V(2,1) + V(0,2)*V(2,2);
    float d12 = V(1,0)*V(2,0) + V(1,1)*V(2,1) + V(1,2)*V(2,2);
    assert(near_eq(d01, 0.0f, 1e-3f));
    assert(near_eq(d02, 0.0f, 1e-3f));
    assert(near_eq(d12, 0.0f, 1e-3f));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  perspective: basic properties
  // =====================================================================
  {
    fprintf(stderr, "[perspective] basic properties... ");
    float fovY = 60.0f * k_deg2rad;
    float aspect = 16.0f / 9.0f;
    float zNear = 0.1f;
    float zFar = 100.0f;
    Mat4f P = perspective(fovY, aspect, zNear, zFar);

    // P should have zeros in the off-diagonal positions except (3,2)
    assert(near_eq(P(0,1), 0.0f));
    assert(near_eq(P(0,2), 0.0f));
    assert(near_eq(P(1,0), 0.0f));
    assert(near_eq(P(1,2), 0.0f));

    // P(3,2) should be -1 (perspective divide)
    assert(near_eq(P(3,2), -1.0f));
    // P(3,3) should be 0
    assert(near_eq(P(3,3), 0.0f));

    // Y is negated for Vulkan (P(1,1) < 0)
    assert(P(1,1) < 0.0f);

    // A point on the near plane along -Z should map to z=0 in clip space
    // Point: (0, 0, -zNear, 1) in camera space
    // clip = P * point
    float cz = P(2,0)*0.0f + P(2,1)*0.0f + P(2,2)*(-zNear) + P(2,3);
    float cw = P(3,0)*0.0f + P(3,1)*0.0f + P(3,2)*(-zNear) + P(3,3);
    // NDC z = cz / cw should be 0 (near plane maps to Z=0 in Vulkan)
    assert(near_eq(cz / cw, 0.0f, 1e-3f));

    // A point on the far plane should map to z=1
    float fz = P(2,2)*(-zFar) + P(2,3);
    float fw = P(3,2)*(-zFar) + P(3,3);
    assert(near_eq(fz / fw, 1.0f, 1e-3f));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  perspective_reversed_z: near->1, far->0
  // =====================================================================
  {
    fprintf(stderr, "[perspective_reversed_z] depth mapping... ");
    float fovY = 60.0f * k_deg2rad;
    float aspect = 16.0f / 9.0f;
    float zNear = 0.1f;
    float zFar = 100.0f;
    Mat4f P = perspective_reversed_z(fovY, aspect, zNear, zFar);

    // Near plane point -> NDC z = 1
    float cz_near = P(2,2)*(-zNear) + P(2,3);
    float cw_near = P(3,2)*(-zNear) + P(3,3);
    assert(near_eq(cz_near / cw_near, 1.0f, 1e-3f));

    // Far plane point -> NDC z = 0
    float cz_far = P(2,2)*(-zFar) + P(2,3);
    float cw_far = P(3,2)*(-zFar) + P(3,3);
    assert(near_eq(cz_far / cw_far, 0.0f, 1e-3f));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  orthographic: basic properties
  // =====================================================================
  {
    fprintf(stderr, "[orthographic] basic properties... ");
    float left = -10.0f, right_ = 10.0f;
    float bottom = -5.0f, top_ = 5.0f;
    float zNear = 0.1f, zFar = 100.0f;
    Mat4f O = orthographic(left, right_, bottom, top_, zNear, zFar);

    // Orthographic: no perspective divide (row 3 = 0,0,0,1)
    assert(near_eq(O(3,0), 0.0f));
    assert(near_eq(O(3,1), 0.0f));
    assert(near_eq(O(3,2), 0.0f));
    assert(near_eq(O(3,3), 1.0f));

    // Center of the ortho box (0,0,-zNear) should map to (0, 0, 0)
    // x: O(0,0)*0 + O(0,3) should be 0 when input x = (right+left)/2 = 0
    float cx = O(0,0)*0.0f + O(0,3);
    assert(near_eq(cx, 0.0f, 1e-3f));

    // Near plane z -> 0
    float nz = O(2,2)*(-zNear) + O(2,3);
    assert(near_eq(nz, 0.0f, 1e-3f));

    // Far plane z -> 1
    float fz = O(2,2)*(-zFar) + O(2,3);
    assert(near_eq(fz, 1.0f, 1e-3f));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  view_from_camera: identity orientation = look_at along -Z
  // =====================================================================
  {
    fprintf(stderr, "[view_from_camera] identity at origin... ");
    Vec3f pos{0.0f, 0.0f, 0.0f};
    Quat4f ori = identity_quat();
    Mat4f V = view_from_camera(pos, ori);

    // With identity orientation, camera axes are +X, +Y, +Z
    // Row 0 = right = (1,0,0), Row 1 = up = (0,1,0), Row 2 = forward = (0,0,1)
    assert(near_eq(V(0,0), 1.0f, 1e-3f));
    assert(near_eq(V(1,1), 1.0f, 1e-3f));
    assert(near_eq(V(2,2), 1.0f, 1e-3f));

    // Translation should be zero (position at origin)
    assert(near_eq(V(0,3), 0.0f, 1e-3f));
    assert(near_eq(V(1,3), 0.0f, 1e-3f));
    assert(near_eq(V(2,3), 0.0f, 1e-3f));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  view_from_camera: translated camera
  // =====================================================================
  {
    fprintf(stderr, "[view_from_camera] translated camera... ");
    Vec3f pos{5.0f, 3.0f, -2.0f};
    Quat4f ori = identity_quat();
    Mat4f V = view_from_camera(pos, ori);

    // V * pos should give origin
    float vx = V(0,0)*pos(0) + V(0,1)*pos(1) + V(0,2)*pos(2) + V(0,3);
    float vy = V(1,0)*pos(0) + V(1,1)*pos(1) + V(1,2)*pos(2) + V(1,3);
    float vz = V(2,0)*pos(0) + V(2,1)*pos(1) + V(2,2)*pos(2) + V(2,3);
    assert(near_eq(vx, 0.0f, 1e-3f));
    assert(near_eq(vy, 0.0f, 1e-3f));
    assert(near_eq(vz, 0.0f, 1e-3f));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  view_from_camera: consistency with look_at
  // =====================================================================
  {
    fprintf(stderr, "[view_from_camera] consistency with look_at... ");
    Vec3f eye{0.0f, 0.0f, 5.0f};
    Vec3f target{0.0f, 0.0f, 0.0f};
    Vec3f worldUp{0.0f, 1.0f, 0.0f};

    Mat4f V_lookat = look_at(eye, target, worldUp);

    // Build equivalent view from camera state
    // Camera at (0,0,5) looking at origin: forward = (0,0,-1), identity orientation
    Quat4f ori = identity_quat();
    Mat4f V_cam = view_from_camera(eye, ori);

    // Both should produce the same result for this simple case
    // (camera looking along -Z from (0,0,5) with identity orientation)
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 4; ++c)
        assert(near_eq(V_lookat(r,c), V_cam(r,c), 1e-3f));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  Frustum: point inside/outside
  // =====================================================================
  {
    fprintf(stderr, "[Frustum] point containment... ");
    float fovY = 60.0f * k_deg2rad;
    float aspect = 16.0f / 9.0f;
    float zNear = 0.1f;
    float zFar = 100.0f;

    // Camera at origin looking along -Z
    Mat4f V = look_at(Vec3f{0.0f, 0.0f, 0.0f}, Vec3f{0.0f, 0.0f, -1.0f},
                      Vec3f{0.0f, 1.0f, 0.0f});
    Mat4f P = perspective(fovY, aspect, zNear, zFar);

    // VP = P * V (but since our matrices are row-major and apply as M * col_vec,
    // the composite is applied as P * (V * p).  For Gribb-Hartmann, VP = P * V
    // as a matrix multiply.  With row-major storage:
    //   VP(i,j) = sum_k P(i,k) * V(k,j)
    // Let's compute VP manually.
    Mat4f VP = Mat4f::zeros();
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j)
        for (int k = 0; k < 4; ++k)
          VP(i,j) += P(i,k) * V(k,j);

    Frustum fr = Frustum::from_view_projection(VP);

    // A point directly in front at medium distance should be inside
    assert(fr.contains_point(Vec3f{0.0f, 0.0f, -10.0f}));

    // A point behind the camera should be outside
    assert(!fr.contains_point(Vec3f{0.0f, 0.0f, 10.0f}));

    // A point way to the right should be outside
    assert(!fr.contains_point(Vec3f{1000.0f, 0.0f, -10.0f}));

    // A point beyond the far plane should be outside
    assert(!fr.contains_point(Vec3f{0.0f, 0.0f, -200.0f}));

    // A point closer than near plane should be outside
    assert(!fr.contains_point(Vec3f{0.0f, 0.0f, -0.01f}));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  Frustum: sphere containment
  // =====================================================================
  {
    fprintf(stderr, "[Frustum] sphere containment... ");
    float fovY = 60.0f * k_deg2rad;
    float aspect = 16.0f / 9.0f;
    float zNear = 0.1f;
    float zFar = 100.0f;

    Mat4f V = look_at(Vec3f{0.0f, 0.0f, 0.0f}, Vec3f{0.0f, 0.0f, -1.0f},
                      Vec3f{0.0f, 1.0f, 0.0f});
    Mat4f P = perspective(fovY, aspect, zNear, zFar);

    Mat4f VP = Mat4f::zeros();
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j)
        for (int k = 0; k < 4; ++k)
          VP(i,j) += P(i,k) * V(k,j);

    Frustum fr = Frustum::from_view_projection(VP);

    // Small sphere at center of frustum: inside
    auto r1 = fr.contains_sphere(Vec3f{0.0f, 0.0f, -50.0f}, 1.0f);
    assert(r1 == Frustum::SphereResult::inside);

    // Sphere fully behind camera: outside
    auto r2 = fr.contains_sphere(Vec3f{0.0f, 0.0f, 50.0f}, 1.0f);
    assert(r2 == Frustum::SphereResult::outside);

    // Large sphere straddling the near plane
    auto r3 = fr.contains_sphere(Vec3f{0.0f, 0.0f, -0.1f}, 0.5f);
    assert(r3 == Frustum::SphereResult::intersecting);

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  orientation_from_look: looking along -Z should give identity
  // =====================================================================
  {
    fprintf(stderr, "[orientation_from_look] -Z forward -> identity... ");
    Vec3f forward{0.0f, 0.0f, -1.0f};
    Vec3f worldUp{0.0f, 1.0f, 0.0f};
    Quat4f q = orientation_from_look(forward, worldUp);

    // Should be (near) identity quaternion
    // Identity quat = (0,0,0,1)
    assert(near_eq(std::fabs(q(3)), 1.0f, 1e-3f));  // |w| ~ 1
    assert(near_eq(q(0)*q(0) + q(1)*q(1) + q(2)*q(2), 0.0f, 1e-3f));  // xyz ~ 0

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  orientation_from_look: looking along +X
  // =====================================================================
  {
    fprintf(stderr, "[orientation_from_look] +X forward... ");
    Vec3f forward{1.0f, 0.0f, 0.0f};
    Vec3f worldUp{0.0f, 1.0f, 0.0f};
    Quat4f q = orientation_from_look(forward, worldUp);

    // The orientation should rotate -Z to +X (90-degree yaw around Y)
    Vec3f result = quat_rotate(q, Vec3f{0.0f, 0.0f, -1.0f});
    assert(vec3_eq(result, Vec3f{1.0f, 0.0f, 0.0f}, 1e-3f));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  orientation_from_look: round-trip with Camera::forward()
  // =====================================================================
  {
    fprintf(stderr, "[orientation_from_look] round-trip with Camera::forward()... ");
    Vec3f fwd = Vec3f{1.0f, 1.0f, -1.0f}.normalized();
    Vec3f worldUp{0.0f, 1.0f, 0.0f};
    Quat4f q = orientation_from_look(fwd, worldUp);

    Camera cam;
    cam.orientation = q;
    Vec3f camFwd = cam.forward();

    // Camera::forward() should return the same direction we gave
    assert(vec3_eq(camFwd, fwd, 1e-3f));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  OrbitMode: reset + update
  // =====================================================================
  {
    fprintf(stderr, "[OrbitMode] reset and initial update... ");
    OrbitMode orbit;
    orbit.config.distance = 10.0f;
    orbit.config.smoothing = 1000.0f;  // Very high smoothing = nearly instant
    orbit.pivot = Vec3f{0.0f, 0.0f, 0.0f};
    orbit.reset();

    auto ctx = FrameContext::first(0.016f);
    Camera cam = orbit.update(ctx);

    // After reset with zero yaw/pitch, camera should be at (0, 0, distance)
    assert(near_eq(cam.position(0), 0.0f, 0.5f));
    assert(near_eq(cam.position(1), 0.0f, 0.5f));
    assert(near_eq(cam.position(2), 10.0f, 0.5f));

    // Camera should be looking toward the pivot (forward ~ -Z direction from pos toward origin)
    Vec3f fwd = cam.forward();
    assert(fwd(2) < 0.0f);  // Looking in -Z direction (toward origin from +Z)

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  OrbitMode: yaw rotation
  // =====================================================================
  {
    fprintf(stderr, "[OrbitMode] yaw rotation... ");
    OrbitMode orbit;
    orbit.config.distance = 10.0f;
    orbit.config.smoothing = 1000.0f;  // Near-instant smoothing
    orbit.pivot = Vec3f{0.0f, 0.0f, 0.0f};
    orbit.reset();

    // Apply 90-degree yaw
    orbit.yawInput = k_half_pi;
    auto ctx = FrameContext::first(0.016f);
    Camera cam = orbit.update(ctx);
    orbit.clear_input();

    // After 90-deg yaw, camera should be at approximately (+10, 0, 0)
    // x = dist * cos(pitch) * sin(yaw) = 10 * 1 * 1 = 10
    // z = dist * cos(pitch) * cos(yaw) = 10 * 1 * 0 = 0
    assert(near_eq(cam.position(0), 10.0f, 1.0f));
    assert(near_eq(cam.position(2), 0.0f, 1.0f));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  OrbitMode: pitch clamping
  // =====================================================================
  {
    fprintf(stderr, "[OrbitMode] pitch clamping... ");
    OrbitMode orbit;
    orbit.config.maxPitch = 45.0f * k_deg2rad;
    orbit.config.minPitch = -45.0f * k_deg2rad;
    orbit.config.smoothing = 1000.0f;
    orbit.reset();

    // Try to pitch beyond max
    orbit.pitchInput = 90.0f * k_deg2rad;
    auto ctx = FrameContext::first(0.016f);
    orbit.update(ctx);
    orbit.clear_input();

    // Internal pitch should be clamped to maxPitch
    assert(orbit.pitch <= orbit.config.maxPitch + 1e-5f);

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  OrbitMode: zoom clamping
  // =====================================================================
  {
    fprintf(stderr, "[OrbitMode] zoom clamping... ");
    OrbitMode orbit;
    orbit.config.distance = 10.0f;
    orbit.config.minDistance = 2.0f;
    orbit.config.maxDistance = 20.0f;
    orbit.config.smoothing = 1000.0f;
    orbit.reset();

    // Zoom in beyond minimum
    orbit.zoomInput = 100.0f;  // Huge zoom-in
    auto ctx = FrameContext::first(0.016f);
    orbit.update(ctx);
    orbit.clear_input();

    // Target distance should be clamped to minDistance
    assert(orbit.targetDist >= orbit.config.minDistance - 1e-5f);
    assert(orbit.targetDist <= orbit.config.maxDistance + 1e-5f);

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  FollowMode: reset + update
  // =====================================================================
  {
    fprintf(stderr, "[FollowMode] reset and initial update... ");
    FollowMode follow;
    follow.config.offset = Vec3f{0.0f, 2.0f, -5.0f};
    follow.config.lookAtOffset = Vec3f{0.0f, 1.0f, 0.0f};
    follow.config.positionSmoothing = 1000.0f;  // Near-instant
    follow.config.rotationSmoothing = 1000.0f;

    follow.targetPosition = Vec3f{0.0f, 0.0f, 0.0f};
    follow.targetOrientation = identity_quat();
    follow.reset();

    auto ctx = FrameContext::first(0.016f);
    Camera cam = follow.update(ctx);

    // With identity target orientation and offset (0,2,-5),
    // camera should be near (0, 2, -5)
    assert(near_eq(cam.position(0), 0.0f, 0.5f));
    assert(near_eq(cam.position(1), 2.0f, 0.5f));
    assert(near_eq(cam.position(2), -5.0f, 0.5f));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  FollowMode: following a moving target
  // =====================================================================
  {
    fprintf(stderr, "[FollowMode] following moving target... ");
    FollowMode follow;
    follow.config.positionSmoothing = 1000.0f;  // Near-instant
    follow.config.rotationSmoothing = 1000.0f;

    follow.targetPosition = Vec3f{0.0f, 0.0f, 0.0f};
    follow.targetOrientation = identity_quat();
    follow.reset();

    // Move target forward
    follow.targetPosition = Vec3f{0.0f, 0.0f, 10.0f};

    auto ctx = FrameContext::first(0.016f);
    Camera cam = follow.update(ctx);

    // Camera should track the target (be near target + offset)
    // With identity orientation, offset (0,2,-5) in world space:
    // expected ~= (0, 2, 10-5) = (0, 2, 5)
    assert(near_eq(cam.position(2), 5.0f, 1.0f));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  FollowMode: target rotation changes camera position
  // =====================================================================
  {
    fprintf(stderr, "[FollowMode] target rotation... ");
    FollowMode follow;
    follow.config.offset = Vec3f{0.0f, 0.0f, -5.0f};  // Directly behind
    follow.config.lookAtOffset = Vec3f{0.0f, 0.0f, 0.0f};
    follow.config.positionSmoothing = 1000.0f;
    follow.config.rotationSmoothing = 1000.0f;

    follow.targetPosition = Vec3f{0.0f, 0.0f, 0.0f};
    // Target faces +X (90-deg yaw around Y)
    follow.targetOrientation = quat_from_axis_angle(Vec3f{0.0f, 1.0f, 0.0f}, k_half_pi);
    follow.reset();

    auto ctx = FrameContext::first(0.016f);
    Camera cam = follow.update(ctx);

    // Offset (0,0,-5) rotated 90deg around Y should become (-5, 0, 0)
    // So camera should be at approximately (-5, 0, 0)
    assert(near_eq(cam.position(0), -5.0f, 1.0f));
    assert(near_eq(cam.position(2), 0.0f, 1.0f));

    fprintf(stderr, "ok\n");
  }

  // =====================================================================
  //  Integration: OrbitMode -> CameraOutput with view/projection
  // =====================================================================
  {
    fprintf(stderr, "[Integration] OrbitMode -> CameraOutput pipeline... ");
    OrbitMode orbit;
    orbit.config.distance = 10.0f;
    orbit.config.smoothing = 1000.0f;
    orbit.pivot = Vec3f{0.0f, 0.0f, 0.0f};
    orbit.reset();

    auto ctx = FrameContext::first(0.016f);
    Camera cam = orbit.update(ctx);

    // Build CameraOutput
    CameraOutput output;
    output.camera = cam;
    output.view = view_from_camera(cam.position, cam.orientation);
    output.projection = perspective(cam.fovY, cam.aspectRatio, cam.nearPlane, cam.farPlane);
    output.activeMode = CameraModeId::orbit;
    output.blendAlpha = 1.0f;

    // View matrix should transform camera position to origin
    float vx = output.view(0,0)*cam.position(0) + output.view(0,1)*cam.position(1)
             + output.view(0,2)*cam.position(2) + output.view(0,3);
    float vy = output.view(1,0)*cam.position(0) + output.view(1,1)*cam.position(1)
             + output.view(1,2)*cam.position(2) + output.view(1,3);
    float vz = output.view(2,0)*cam.position(0) + output.view(2,1)*cam.position(1)
             + output.view(2,2)*cam.position(2) + output.view(2,3);
    assert(near_eq(vx, 0.0f, 1e-2f));
    assert(near_eq(vy, 0.0f, 1e-2f));
    assert(near_eq(vz, 0.0f, 1e-2f));

    // Build frustum from VP and verify the pivot is visible
    Mat4f VP = Mat4f::zeros();
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j)
        for (int k = 0; k < 4; ++k)
          VP(i,j) += output.projection(i,k) * output.view(k,j);

    Frustum fr = Frustum::from_view_projection(VP);
    assert(fr.contains_point(orbit.pivot));

    fprintf(stderr, "ok\n");
  }

  fprintf(stderr, "\n=== All camera tests passed ===\n");
  return 0;
}
