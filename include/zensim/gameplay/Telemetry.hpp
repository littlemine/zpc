#pragma once
/// @file Telemetry.hpp
/// @brief Per-frame telemetry types for Gameplay system performance and quality tracking.
///
/// FrameTelemetry captures timing, quality metrics, and state transition data
/// that maps directly to ZPC ValidationRecord fields for automated regression
/// detection.

#include "zensim/gameplay/Core.hpp"

namespace zs::gameplay {

  /// Per-frame telemetry collected by the Gameplay system.
  ///
  /// Each field maps to a ValidationRecord with a stable ID:
  ///   "gameplay.total_phase_ms"          → totalPhaseMs        (≤ 2.0)
  ///   "gameplay.input_phase_ms"          → inputPhaseMs        (≤ 0.2)
  ///   "gameplay.character_phase_ms"      → characterPhaseMs    (≤ 0.5)
  ///   "gameplay.camera_phase_ms"         → cameraPhaseMs       (≤ 0.5)
  ///   "gameplay.input_latency_ms"        → inputToScreenLatencyMs (≤ 16.67)
  ///   "gameplay.camera_jitter_px_rms"    → cameraJitterPx      (≤ 0.5)
  struct FrameTelemetry {
    // ── Timing (milliseconds) ──
    f64 inputPhaseMs;         ///< Input phase processing duration
    f64 characterPhaseMs;     ///< Character phase processing duration
    f64 cameraPhaseMs;        ///< Camera phase processing duration
    f64 totalPhaseMs;         ///< Sum of all phase durations

    // ── Quality metrics ──
    f32 inputToScreenLatencyMs;  ///< Input event to final camera output (ms)
    f32 cameraJitterPx;          ///< Frame-to-frame camera jitter (pixels RMS)
    f32 cameraSmoothness;        ///< 1.0 = perfectly smooth, 0.0 = max jitter

    // ── Character state ──
    u32 stateTransitionsThisFrame;  ///< Number of state transitions this frame
    f32 velocityMagnitude;          ///< Character velocity magnitude (m/s)

    // ── Frame info ──
    u64 frameNumber;

    /// Zero-initialize all fields
    static constexpr FrameTelemetry zero(u64 frame) noexcept {
      return FrameTelemetry{
        0.0, 0.0, 0.0, 0.0,
        0.0f, 0.0f, 1.0f,
        0, 0.0f,
        frame
      };
    }
  };

}  // namespace zs::gameplay
