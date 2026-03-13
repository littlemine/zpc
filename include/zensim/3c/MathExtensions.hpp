#pragma once
/// @file MathExtensions.hpp
/// @brief Math utilities missing from ZPC core: slerp, damping, spring, remap.
///
/// These are general-purpose math functions placed in the zs:: namespace
/// since they extend ZPC's math library and are useful beyond the 3C domain.

#include "zensim/3c/Core.hpp"
#include "zensim/ZpcMathUtils.hpp"

namespace zs {

  // ── Quaternion slerp ───────────────────────────────────────────────────

  /// Spherical linear interpolation between two unit quaternions.
  /// @param a  Start quaternion (unit length)
  /// @param b  End quaternion (unit length)
  /// @param t  Interpolation factor [0, 1]
  /// @return   Interpolated quaternion (unit length)
  ///
  /// Handles the antipodal case: if dot(a, b) < 0, negates b to take
  /// the shortest arc. Falls back to nlerp when the angle is very small
  /// to avoid division by near-zero sin(theta).
  template <typename T>
  constexpr auto slerp(vec<T, 4> const& a, vec<T, 4> const& b, T t) noexcept -> vec<T, 4> {
    // Compute cosine of the angle between quaternions
    T cosTheta = a(0) * b(0) + a(1) * b(1) + a(2) * b(2) + a(3) * b(3);

    // If dot is negative, negate one quaternion to take shortest path
    vec<T, 4> b2 = b;
    if (cosTheta < T(0)) {
      b2 = vec<T, 4>{-b(0), -b(1), -b(2), -b(3)};
      cosTheta = -cosTheta;
    }

    // If quaternions are very close, use normalized linear interpolation
    if (cosTheta > T(1) - T(1e-6)) {
      vec<T, 4> result{
        a(0) + t * (b2(0) - a(0)),
        a(1) + t * (b2(1) - a(1)),
        a(2) + t * (b2(2) - a(2)),
        a(3) + t * (b2(3) - a(3))
      };
      return result.normalized();
    }

    // Standard slerp
    const T theta = zs::acos(cosTheta);
    const T sinTheta = zs::sin(theta);
    const T wa = zs::sin((T(1) - t) * theta) / sinTheta;
    const T wb = zs::sin(t * theta) / sinTheta;

    return vec<T, 4>{
      wa * a(0) + wb * b2(0),
      wa * a(1) + wb * b2(1),
      wa * a(2) + wb * b2(2),
      wa * a(3) + wb * b2(3)
    };
  }

  // ── Exponential damping ────────────────────────────────────────────────

  /// Frame-rate-independent exponential damping (smoothing).
  /// Moves `current` toward `target` with a time constant of `1/smoothing`.
  ///
  /// @param current   Current value
  /// @param target    Target value
  /// @param smoothing Smoothing factor (higher = faster convergence)
  /// @param dt        Delta time (seconds)
  /// @return          New value closer to target
  ///
  /// Formula: current + (target - current) * (1 - exp(-smoothing * dt))
  template <typename T, int N>
  constexpr auto damp(vec<T, N> const& current, vec<T, N> const& target,
                      T smoothing, T dt) noexcept -> vec<T, N> {
    const T factor = T(1) - zs::exp(-smoothing * dt);
    vec<T, N> result{};
    for (int i = 0; i < N; ++i) {
      result(i) = current(i) + (target(i) - current(i)) * factor;
    }
    return result;
  }

  /// Scalar version of exponential damping.
  template <typename T>
  constexpr auto damp(T current, T target, T smoothing, T dt) noexcept -> T {
    const T factor = T(1) - zs::exp(-smoothing * dt);
    return current + (target - current) * factor;
  }

  // ── Critically damped spring ───────────────────────────────────────────

  /// Semi-implicit Euler integration of a critically damped spring.
  /// Updates position and velocity in-place toward the target.
  ///
  /// @param position   Current position (modified in-place)
  /// @param velocity   Current velocity (modified in-place)
  /// @param target     Target position
  /// @param omega      Angular frequency (2 * pi / period). Higher = stiffer.
  /// @param damping    Damping ratio (1.0 = critically damped, <1 = underdamped)
  /// @param dt         Delta time (seconds)
  template <typename T, int N>
  constexpr void spring_damper(vec<T, N>& position, vec<T, N>& velocity,
                               vec<T, N> const& target,
                               T omega, T zeta, T dt) noexcept {
    // Semi-implicit Euler for damped harmonic oscillator:
    //   acceleration = -2 * zeta * omega * velocity - omega^2 * (position - target)
    //   velocity += acceleration * dt
    //   position += velocity * dt
    const T omega_sq = omega * omega;
    const T damping_coeff = T(2) * zeta * omega;

    for (int i = 0; i < N; ++i) {
      const T displacement = position(i) - target(i);
      const T accel = -damping_coeff * velocity(i) - omega_sq * displacement;
      velocity(i) += accel * dt;
      position(i) += velocity(i) * dt;
    }
  }

  // ── Remap ──────────────────────────────────────────────────────────────

  /// Remap a value from [inMin, inMax] to [outMin, outMax] with clamping.
  template <typename T>
  constexpr auto remap(T value, T inMin, T inMax, T outMin, T outMax) noexcept -> T {
    const T t = zs::math::clamp((value - inMin) / (inMax - inMin), T(0), T(1));
    return outMin + t * (outMax - outMin);
  }

  /// Remap without clamping (allows extrapolation).
  template <typename T>
  constexpr auto remap_unclamped(T value, T inMin, T inMax, T outMin, T outMax) noexcept -> T {
    const T t = (value - inMin) / (inMax - inMin);
    return outMin + t * (outMax - outMin);
  }

  // ── Lerp ───────────────────────────────────────────────────────────────

  /// Linear interpolation between two values.
  template <typename T>
  constexpr auto lerp(T a, T b, T t) noexcept -> T {
    return a + t * (b - a);
  }

  /// Linear interpolation between two vectors.
  template <typename T, int N>
  constexpr auto lerp(vec<T, N> const& a, vec<T, N> const& b, T t) noexcept -> vec<T, N> {
    vec<T, N> result{};
    for (int i = 0; i < N; ++i) {
      result(i) = a(i) + t * (b(i) - a(i));
    }
    return result;
  }

}  // namespace zs
