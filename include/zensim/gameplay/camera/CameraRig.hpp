#pragma once
/// @file CameraRig.hpp
/// @brief Camera mode manager with smooth transitions and blending.
///
/// CameraRig owns one of each camera mode (orbit, follow, fps) and provides:
///   - Smooth transitions between modes via slerp/lerp blending
///   - Camera shake as a post-process on the final output
///   - CameraOutput generation with view/projection matrices
///
/// The rig does NOT own the character or input — those are fed in via
/// the `update()` parameters. This keeps the rig composable and testable.

#include "zensim/gameplay/camera/Camera.hpp"
#include "zensim/gameplay/camera/CameraMode.hpp"
#include "zensim/gameplay/camera/CameraShake.hpp"
#include "zensim/gameplay/camera/MatrixBuilders.hpp"
#include "zensim/gameplay/camera/Frustum.hpp"

namespace zs::gameplay {

  /// Configuration for mode transition blending.
  struct TransitionConfig {
    f32 blendDuration   {0.5f};    ///< Duration of blend between modes (seconds)
    f32 blendSmoothing  {4.0f};    ///< Smoothing factor for blend alpha
  };

  /// Camera rig: manages modes, transitions, shake, and output generation.
  struct CameraRig {
    // ── Mode instances ────────────────────────────────────────────────────
    OrbitMode   orbit{};
    FollowMode  follow{};
    FpsMode     fps{};
    CameraShake shake{};

    // ── Transition state ──────────────────────────────────────────────────
    TransitionConfig transConfig{};
    CameraModeId     activeMode    {CameraModeId::orbit};
    CameraModeId     previousMode  {CameraModeId::orbit};
    f32              blendAlpha    {1.0f};   ///< 1.0 = fully in activeMode
    bool             blending      {false};

    // ── Cached output from previous mode (for blending) ───────────────────
    Camera previousCamera{};

    /// Request a transition to a new mode.
    /// If already in the target mode, does nothing.
    /// If a blend is in progress, snaps the old blend and starts a new one.
    constexpr void setMode(CameraModeId newMode) noexcept {
      if (newMode == activeMode && !blending) return;

      // Snapshot the current camera state as the blend-from source
      previousCamera = currentModeCamera();
      previousMode = activeMode;
      activeMode = newMode;
      blendAlpha = 0.0f;
      blending = true;
    }

    /// Update all active modes and blend if transitioning.
    ///
    /// Call this after setting mode inputs (orbit.pivot, follow.target*, fps.character*).
    /// @param ctx  Frame timing
    /// @return     Complete CameraOutput with matrices
    constexpr CameraOutput update(FrameContext const& ctx) noexcept {
      // Update all modes so they stay warm (no pop on transition)
      Camera camOrbit  = orbit.update(ctx);
      Camera camFollow = follow.update(ctx);
      Camera camFps    = fps.update(ctx);

      // Get the active mode's camera
      Camera activeCam = cameraForMode(activeMode, camOrbit, camFollow, camFps);

      // Blend if transitioning
      Camera finalCam = activeCam;
      if (blending) {
        blendAlpha += ctx.dt / transConfig.blendDuration;
        if (blendAlpha >= 1.0f) {
          blendAlpha = 1.0f;
          blending = false;
        }
        // Smoothstep the blend for natural feel
        const f32 t = smoothstep(blendAlpha);
        finalCam = blendCameras(previousCamera, activeCam, t);

        // Update the previous camera snapshot each frame for smooth blending
        // (the "from" state still evolves so the blend doesn't freeze)
        // Actually, we want to blend from a snapshot, not a moving target.
        // The previousCamera is frozen at transition start.
      }

      // Apply shake
      finalCam = shake.apply(finalCam, ctx);

      // Build output
      CameraOutput output;
      output.camera = finalCam;
      output.view = view_from_camera(finalCam.position, finalCam.orientation);
      output.projection = perspective(finalCam.fovY, finalCam.aspectRatio,
                                       finalCam.nearPlane, finalCam.farPlane);
      output.activeMode = activeMode;
      output.blendFrom = previousMode;
      output.blendAlpha = blendAlpha;

      return output;
    }

    /// Clear per-frame inputs on all modes.
    constexpr void clearInputs() noexcept {
      orbit.clear_input();
      fps.clear_input();
    }

    /// Get the camera from the currently active mode (without blending).
    constexpr Camera currentModeCamera() const noexcept {
      switch (activeMode) {
        case CameraModeId::orbit:  return cameraFromOrbit();
        case CameraModeId::follow: return cameraFromFollow();
        case CameraModeId::fps:    return cameraFromFps();
        default: return Camera{};
      }
    }

  private:
    /// Snapshot camera from orbit state (without running update).
    constexpr Camera cameraFromOrbit() const noexcept {
      Camera cam;
      cam.position = orbit.smoothedPos;
      const Vec3f fwd = (orbit.pivot - orbit.smoothedPos).normalized();
      cam.orientation = orientation_from_look(fwd, Vec3f{0.0f, 1.0f, 0.0f});
      return cam;
    }

    /// Snapshot camera from follow state.
    constexpr Camera cameraFromFollow() const noexcept {
      Camera cam;
      cam.position = follow.smoothedPos;
      cam.orientation = follow.smoothedOri;
      return cam;
    }

    /// Snapshot camera from FPS state.
    constexpr Camera cameraFromFps() const noexcept {
      Camera cam;
      cam.position = Vec3f{
        fps.characterPosition(0) + fps.config.eyeOffset(0),
        fps.characterPosition(1) + fps.config.eyeOffset(1),
        fps.characterPosition(2) + fps.config.eyeOffset(2)
      };
      cam.orientation = fps.smoothedOri;
      return cam;
    }

    /// Select camera by mode ID.
    static constexpr Camera cameraForMode(CameraModeId mode,
                                           Camera const& orbitCam,
                                           Camera const& followCam,
                                           Camera const& fpsCam) noexcept {
      switch (mode) {
        case CameraModeId::orbit:  return orbitCam;
        case CameraModeId::follow: return followCam;
        case CameraModeId::fps:    return fpsCam;
        default: return Camera{};
      }
    }

    /// Blend two cameras by factor t (0 = a, 1 = b).
    static constexpr Camera blendCameras(Camera const& a, Camera const& b, f32 t) noexcept {
      Camera c;
      // Lerp position
      c.position = Vec3f{
        a.position(0) + t * (b.position(0) - a.position(0)),
        a.position(1) + t * (b.position(1) - a.position(1)),
        a.position(2) + t * (b.position(2) - a.position(2))
      };
      // Slerp orientation
      c.orientation = slerp(a.orientation, b.orientation, t);
      // Lerp scalar fields
      c.fovY = a.fovY + t * (b.fovY - a.fovY);
      c.nearPlane = a.nearPlane + t * (b.nearPlane - a.nearPlane);
      c.farPlane = a.farPlane + t * (b.farPlane - a.farPlane);
      c.aspectRatio = a.aspectRatio + t * (b.aspectRatio - a.aspectRatio);
      return c;
    }

    /// Hermite smoothstep for blend alpha.
    static constexpr f32 smoothstep(f32 t) noexcept {
      t = zs::math::clamp(t, 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }
  };

}  // namespace zs::gameplay
