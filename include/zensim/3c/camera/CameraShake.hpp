#pragma once
/// @file CameraShake.hpp
/// @brief Trauma-based camera shake system with configurable decay.
///
/// The shake system uses a "trauma" model:
///   - Trauma is a [0, 1] value representing shake intensity.
///   - Shake magnitude = trauma^2 (quadratic falloff for natural feel).
///   - Trauma decays exponentially over time.
///   - Offsets are computed using hash-based noise (deterministic, no Perlin tables).
///
/// Usage:
///   CameraShake shake;
///   shake.addTrauma(0.5f);        // impact!
///   Camera cam = modeCamera;
///   cam = shake.apply(cam, ctx);  // adds shake offsets

#include "zensim/3c/Core.hpp"
#include "zensim/3c/MathExtensions.hpp"
#include "zensim/3c/camera/Camera.hpp"

namespace zs::threeC {

  /// Configuration for camera shake behaviour.
  struct ShakeConfig {
    f32 maxYaw        {0.05f};     ///< Max yaw offset (radians) at trauma=1
    f32 maxPitch      {0.04f};     ///< Max pitch offset (radians) at trauma=1
    f32 maxRoll       {0.02f};     ///< Max roll offset (radians) at trauma=1
    f32 maxOffsetX    {0.1f};      ///< Max position offset X (world units) at trauma=1
    f32 maxOffsetY    {0.08f};     ///< Max position offset Y (world units) at trauma=1
    f32 maxOffsetZ    {0.0f};      ///< Max position offset Z (world units) at trauma=1
    f32 frequency     {25.0f};     ///< Shake oscillation frequency (Hz)
    f32 decayRate     {3.0f};      ///< Trauma decay rate (higher = faster decay)
  };

  /// Camera shake state.  Call `addTrauma()` on impacts, then `apply()` each frame.
  struct CameraShake {
    ShakeConfig config{};

    f32 trauma    {0.0f};   ///< Current trauma level [0, 1]
    f32 elapsed   {0.0f};   ///< Accumulated time for noise sampling

    /// Add trauma (clamped to [0, 1]).
    constexpr void addTrauma(f32 amount) noexcept {
      trauma += amount;
      if (trauma > 1.0f) trauma = 1.0f;
    }

    /// Reset shake state.
    constexpr void reset() noexcept {
      trauma = 0.0f;
      elapsed = 0.0f;
    }

    /// Get current shake intensity (trauma^2).
    constexpr f32 intensity() const noexcept {
      return trauma * trauma;
    }

    /// Apply shake offsets to a camera and decay trauma.
    ///
    /// @param cam  Base camera state (unshaken)
    /// @param ctx  Frame timing
    /// @return     Camera with shake offsets applied
    constexpr Camera apply(Camera cam, FrameContext const& ctx) noexcept {
      // Decay trauma
      trauma -= config.decayRate * ctx.dt;
      if (trauma < 0.0f) trauma = 0.0f;

      if (trauma < 1e-5f) return cam;

      elapsed += ctx.dt;
      const f32 shake = intensity();

      // Hash-based noise: deterministic, no tables needed.
      // Uses different seed offsets for each axis to decorrelate.
      const f32 t = elapsed * config.frequency;
      const f32 noiseYaw   = hash_noise(t, 0u);
      const f32 noisePitch = hash_noise(t, 1u);
      const f32 noiseRoll  = hash_noise(t, 2u);
      const f32 noiseX     = hash_noise(t, 3u);
      const f32 noiseY     = hash_noise(t, 4u);
      const f32 noiseZ     = hash_noise(t, 5u);

      // Rotational shake: apply yaw, pitch, roll offsets
      const f32 dyaw   = shake * config.maxYaw   * noiseYaw;
      const f32 dpitch = shake * config.maxPitch  * noisePitch;
      const f32 droll  = shake * config.maxRoll   * noiseRoll;

      // Build rotation offset: roll around Z, pitch around X, yaw around Y
      const Quat4f qYaw   = quat_from_axis_angle(Vec3f{0.0f, 1.0f, 0.0f}, dyaw);
      const Quat4f qPitch = quat_from_axis_angle(Vec3f{1.0f, 0.0f, 0.0f}, dpitch);
      const Quat4f qRoll  = quat_from_axis_angle(Vec3f{0.0f, 0.0f, 1.0f}, droll);
      const Quat4f shakeRot = quat_multiply(quat_multiply(qYaw, qPitch), qRoll);
      cam.orientation = quat_multiply(cam.orientation, shakeRot);

      // Positional shake
      cam.position(0) += shake * config.maxOffsetX * noiseX;
      cam.position(1) += shake * config.maxOffsetY * noiseY;
      cam.position(2) += shake * config.maxOffsetZ * noiseZ;

      return cam;
    }

  private:
    /// Simple hash-based noise in [-1, 1], deterministic from (time, seed).
    /// Uses integer hashing of the time value with different seeds to produce
    /// decorrelated pseudo-random values that vary smoothly-ish over time.
    static constexpr f32 hash_noise(f32 t, u32 seed) noexcept {
      // Interpolate between two integer hash values for smoothness
      const f32 floored = static_cast<f32>(static_cast<i32>(t >= 0.0f ? t : t - 1.0f));
      const f32 frac = t - floored;

      const u32 i0 = static_cast<u32>(static_cast<i32>(floored)) + seed * 7919u;
      const u32 i1 = i0 + 1u;

      const f32 v0 = hash_to_float(i0);
      const f32 v1 = hash_to_float(i1);

      // Smoothstep interpolation
      const f32 s = frac * frac * (3.0f - 2.0f * frac);
      return v0 + s * (v1 - v0);
    }

    /// Hash a u32 to a float in [-1, 1].
    static constexpr f32 hash_to_float(u32 x) noexcept {
      // Robert Jenkins' 32-bit integer hash
      x = ((x >> 16u) ^ x) * 0x45d9f3bu;
      x = ((x >> 16u) ^ x) * 0x45d9f3bu;
      x = (x >> 16u) ^ x;
      // Map to [-1, 1]
      return static_cast<f32>(x) / static_cast<f32>(0x7FFFFFFFu) * 2.0f - 1.0f;
    }
  };

}  // namespace zs::threeC
