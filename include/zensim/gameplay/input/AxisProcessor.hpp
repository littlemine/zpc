#pragma once
/// @file AxisProcessor.hpp
/// @brief Axis processing pipeline: dead-zone, response curve, sensitivity.
///
/// Transforms raw device axis values [-1, 1] through a configurable pipeline:
///   1. Dead-zone: suppress values below a threshold
///   2. Response curve: shape the output (linear, quadratic, cubic, custom power)
///   3. Sensitivity: scale the final output
///
/// All functions are constexpr and operate on single f32 values.

#include "zensim/gameplay/Core.hpp"

namespace zs::gameplay {

  // ── Response curve types ───────────────────────────────────────────────

  /// Predefined response curve shapes.
  enum class ResponseCurve : u8 {
    linear    = 0,  ///< output = input (default)
    quadratic = 1,  ///< output = input^2 * sign(input)
    cubic     = 2,  ///< output = input^3
    custom    = 3,  ///< output = input^exponent * sign(input)
  };

  // ── AxisConfig ─────────────────────────────────────────────────────────

  /// Configuration for a single axis processing pipeline.
  struct AxisConfig {
    f32 deadZone;       ///< Dead-zone threshold [0, 1). Values below are zeroed.
    f32 sensitivity;    ///< Output multiplier (default 1.0)
    ResponseCurve curve; ///< Response curve shape
    f32 exponent;       ///< Custom exponent (only used when curve == custom)
    bool invert;        ///< If true, negate the output

    /// Default config: no dead-zone, linear, sensitivity 1.0
    static constexpr AxisConfig defaults() noexcept {
      return AxisConfig{0.0f, 1.0f, ResponseCurve::linear, 2.0f, false};
    }

    /// Gamepad-typical config: 15% dead-zone, quadratic curve
    static constexpr AxisConfig gamepad() noexcept {
      return AxisConfig{0.15f, 1.0f, ResponseCurve::quadratic, 2.0f, false};
    }

    /// Mouse config: no dead-zone, linear, adjustable sensitivity
    static constexpr AxisConfig mouse(f32 sens = 1.0f) noexcept {
      return AxisConfig{0.0f, sens, ResponseCurve::linear, 2.0f, false};
    }
  };

  // ── Processing functions ───────────────────────────────────────────────

  /// Apply dead-zone to a raw axis value.
  /// Input in [-1, 1]. Output in [-1, 1] or 0 if within dead-zone.
  /// The output is rescaled so that the edge of the dead-zone maps to 0,
  /// and ±1 still maps to ±1.
  constexpr f32 apply_dead_zone(f32 raw, f32 threshold) noexcept {
    if (threshold <= 0.0f) return raw;
    if (threshold >= 1.0f) return 0.0f;

    const f32 absVal = raw < 0.0f ? -raw : raw;
    if (absVal < threshold) return 0.0f;

    // Rescale: [threshold, 1] → [0, 1]
    const f32 sign = raw < 0.0f ? -1.0f : 1.0f;
    const f32 rescaled = (absVal - threshold) / (1.0f - threshold);
    return sign * rescaled;
  }

  /// Apply a response curve to a dead-zone-processed value.
  /// Input assumed to be in [-1, 1].
  constexpr f32 apply_response_curve(f32 value, ResponseCurve curve, f32 exponent) noexcept {
    const f32 absVal = value < 0.0f ? -value : value;
    const f32 sign = value < 0.0f ? -1.0f : 1.0f;

    f32 shaped = absVal;
    switch (curve) {
      case ResponseCurve::linear:
        shaped = absVal;
        break;
      case ResponseCurve::quadratic:
        shaped = absVal * absVal;
        break;
      case ResponseCurve::cubic:
        shaped = absVal * absVal * absVal;
        break;
      case ResponseCurve::custom:
        // pow(absVal, exponent) — use repeated multiplication for small integer exponents
        shaped = zs::pow(absVal, exponent);
        break;
    }
    return sign * shaped;
  }

  /// Full axis processing pipeline: dead-zone → response curve → sensitivity → invert.
  constexpr f32 process_axis(f32 raw, AxisConfig const& config) noexcept {
    f32 value = apply_dead_zone(raw, config.deadZone);
    value = apply_response_curve(value, config.curve, config.exponent);
    value *= config.sensitivity;
    if (config.invert) value = -value;

    // Clamp to [-1, 1] * sensitivity (allow sensitivity > 1 to produce > 1 output)
    return value;
  }

  // ── Dual-axis processing ───────────────────────────────────────────────

  /// Process a 2D axis (e.g., gamepad stick) with optional radial dead-zone.
  /// Radial dead-zone considers the magnitude of the 2D vector rather than
  /// each axis independently, which produces a circular dead-zone.
  struct DualAxisConfig {
    AxisConfig xConfig;
    AxisConfig yConfig;
    bool radialDeadZone;  ///< If true, use vector magnitude for dead-zone

    static constexpr DualAxisConfig defaults() noexcept {
      return DualAxisConfig{AxisConfig::defaults(), AxisConfig::defaults(), false};
    }

    static constexpr DualAxisConfig gamepadStick() noexcept {
      return DualAxisConfig{AxisConfig::gamepad(), AxisConfig::gamepad(), true};
    }

    static constexpr DualAxisConfig mouseLook(f32 sens = 1.0f) noexcept {
      return DualAxisConfig{AxisConfig::mouse(sens), AxisConfig::mouse(sens), false};
    }
  };

  /// Process a 2D axis pair through the dual-axis pipeline.
  constexpr Vec2f process_dual_axis(f32 rawX, f32 rawY, DualAxisConfig const& config) noexcept {
    if (config.radialDeadZone) {
      // Radial dead-zone: compute magnitude, apply dead-zone to magnitude
      const f32 mag = zs::sqrt(rawX * rawX + rawY * rawY);
      const f32 threshold = config.xConfig.deadZone;  // use X config's dead-zone for both

      if (mag < threshold) {
        return Vec2f{0.0f, 0.0f};
      }

      // Rescale magnitude
      const f32 rescaled = (mag - threshold) / (1.0f - threshold);
      const f32 scale = rescaled / mag;
      f32 x = rawX * scale;
      f32 y = rawY * scale;

      // Apply response curves and sensitivity individually
      x = apply_response_curve(x, config.xConfig.curve, config.xConfig.exponent);
      x *= config.xConfig.sensitivity;
      if (config.xConfig.invert) x = -x;

      y = apply_response_curve(y, config.yConfig.curve, config.yConfig.exponent);
      y *= config.yConfig.sensitivity;
      if (config.yConfig.invert) y = -y;

      return Vec2f{x, y};
    }

    // Independent axis processing
    return Vec2f{
      process_axis(rawX, config.xConfig),
      process_axis(rawY, config.yConfig)
    };
  }

}  // namespace zs::gameplay
