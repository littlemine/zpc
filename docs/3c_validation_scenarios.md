# 3C Validation Scenarios & Artifact Layout

**Date:** 2026-03-13
**Branch:** `zpc_3c`

---

## 1. Validation Scenario Registry

Each scenario is registered as a `CanaryScenarioDescriptor` in the ZPC canary system.
Scenarios are deterministic: given the same parameters, they produce the same
`ValidationSuiteReport`.

### 1.1 Input Validation Scenarios

| Scenario ID | Description | Key Metrics | Parameters |
|------------|-------------|-------------|------------|
| `3c.input.dead_zone` | Verify dead-zone suppresses values below threshold | axis output = 0 when raw < threshold | `threshold: [0.05, 0.3]` |
| `3c.input.response_curve` | Verify response curves transform axis values correctly | processed vs. expected at 10 sample points | `curve: {linear, quadratic, cubic}` |
| `3c.input.action_phases` | Verify pressed/held/released transitions over N frames | correct phase sequence | `holdFrames: [1, 60]` |
| `3c.input.buffer_replay` | Verify input buffer stores and replays N frames | replayed snapshot matches original | `bufferSize: [8, 64]` |
| `3c.input.latency` | Measure input phase processing time | phase_ms ≤ 0.2 | — |

### 1.2 Camera Validation Scenarios

| Scenario ID | Description | Key Metrics | Parameters |
|------------|-------------|-------------|------------|
| `3c.camera.orbit_360` | Orbit camera 360° around target at fixed distance | final position matches start (ε < 1e-4), jitter ≤ 0.5 px RMS | `distance: [5, 50]`, `speed: deg/s` |
| `3c.camera.follow_moving` | Follow a target moving in a straight line | offset error ≤ threshold after settling, jitter ≤ 0.5 px | `targetSpeed: [1, 20] m/s`, `smoothing: [0.05, 0.5]` |
| `3c.camera.fps_pitch_clamp` | FPS camera pitch clamped to [-89°, 89°] | pitch never exceeds bounds | `inputRate: deg/frame` |
| `3c.camera.mode_transition` | Blend between two camera modes | no discontinuity in position/orientation (max delta ≤ threshold) | `from: mode`, `to: mode`, `duration: [0.1, 2.0] s` |
| `3c.camera.shake_decay` | Apply trauma and verify shake decays to zero | amplitude ≤ ε after decay time | `trauma: [0.1, 1.0]`, `decayRate: [0.5, 5.0]` |
| `3c.camera.projection_reference` | Verify perspective matrix against reference values | element-wise error ≤ 1e-6 | `fov, aspect, near, far` |
| `3c.camera.lookAt_reference` | Verify lookAt matrix against reference values | element-wise error ≤ 1e-6 | `eye, target, up` |
| `3c.camera.phase_budget` | Measure camera phase processing time | phase_ms ≤ 0.5 | — |

### 1.3 Character Validation Scenarios

| Scenario ID | Description | Key Metrics | Parameters |
|------------|-------------|-------------|------------|
| `3c.char.state_transitions` | Walk through all defined state transitions | 100% correct transitions, no invalid states | — |
| `3c.char.coyote_time` | Jump after leaving ledge within coyote window | jump succeeds within ± 1 frame | `coyoteFrames: [3, 12]` |
| `3c.char.jump_buffer` | Buffer jump input before landing | jump executes within ± 1 frame of landing | `bufferFrames: [3, 12]` |
| `3c.char.variable_jump` | Release jump early for shorter jump | apex height proportional to hold duration | `holdFrames: [1, 30]` |
| `3c.char.accel_curve` | Verify acceleration curve matches configured profile | velocity at t matches expected ± ε | `profile: {linear, ease_in, ease_out, ease_in_out}` |
| `3c.char.ground_detection` | Verify ground state updates with mock query | grounded/airborne transitions correct | — |
| `3c.char.sprint_velocity` | Verify sprint produces correct max velocity | velocity within ± 1% of configured max | `maxSpeed: [5, 15] m/s` |
| `3c.char.phase_budget` | Measure character phase processing time | phase_ms ≤ 0.5 | — |

### 1.4 Integration Validation Scenarios

| Scenario ID | Description | Key Metrics | Parameters |
|------------|-------------|-------------|------------|
| `3c.integration.full_loop` | Run input → character → camera for 600 frames | total_phase_ms ≤ 2.0, no crashes | — |
| `3c.integration.input_to_screen` | Measure end-to-end input-to-camera latency | latency ≤ 16.67 ms | — |
| `3c.integration.camera_follow_character` | Character moves, camera follows with configured mode | offset converges, jitter ≤ 0.5 px | `mode: {orbit, follow, fps}` |

---

## 2. Artifact Directory Layout

```
zpc_assets/
  3c/
    baselines/
      input/
        dead_zone.json            # baseline report for 3c.input.dead_zone
        response_curve.json
        action_phases.json
        buffer_replay.json
        latency.json
      camera/
        orbit_360.json
        follow_moving.json
        fps_pitch_clamp.json
        mode_transition.json
        shake_decay.json
        projection_reference.json
        lookAt_reference.json
        phase_budget.json
      character/
        state_transitions.json
        coyote_time.json
        jump_buffer.json
        variable_jump.json
        accel_curve.json
        ground_detection.json
        sprint_velocity.json
        phase_budget.json
      integration/
        full_loop.json
        input_to_screen.json
        camera_follow_character.json

    reference_data/
      projection_matrices/       # known-good matrices for comparison
        perspective_60fov_16x9.json
        perspective_90fov_16x9.json
        orthographic_10x10.json
      response_curves/           # expected output curves
        linear.json
        quadratic.json
        cubic.json
      state_transition_tables/   # expected transition sequences
        basic_locomotion.json
```

### 2.1 Test Source Layout

```
test/
  3c/
    input/
      dead_zone.cpp
      response_curve.cpp
      action_phases.cpp
      input_buffer.cpp
      input_latency_benchmark.cpp    # benchmark (no add_test)
    camera/
      orbit_360.cpp
      follow_moving.cpp
      projection_reference.cpp
      lookAt_reference.cpp
      camera_phase_benchmark.cpp     # benchmark
    character/
      state_transitions.cpp
      coyote_time.cpp
      jump_buffer.cpp
      accel_curve.cpp
      character_phase_benchmark.cpp  # benchmark
    integration/
      full_loop.cpp
      input_to_screen.cpp
    math/
      slerp.cpp                      # slerp correctness test
```

### 2.2 CTest Registration Pattern

```cmake
# --- 3C Tests ---

# Input
add_executable(zs_3c_dead_zone 3c/input/dead_zone.cpp)
target_link_libraries(zs_3c_dead_zone PRIVATE zpc_3c_input)
add_test(Zs3cInputDeadZone zs_3c_dead_zone)
add_dependencies(zensim zs_3c_dead_zone)

# Camera
add_executable(zs_3c_projection_ref 3c/camera/projection_reference.cpp)
target_link_libraries(zs_3c_projection_ref PRIVATE zpc_3c_camera)
add_test(Zs3cCameraProjectionRef zs_3c_projection_ref)
add_dependencies(zensim zs_3c_projection_ref)

# Benchmark (no add_test)
add_executable(zs_3c_camera_bench 3c/camera/camera_phase_benchmark.cpp)
target_link_libraries(zs_3c_camera_bench PRIVATE zpc_3c_camera)
add_dependencies(zensim zs_3c_camera_bench)
```

---

## 3. Baseline Workflow

1. **Create baseline:** Run scenario, save `ValidationSuiteReport` as JSON to
   `zpc_assets/3c/baselines/<category>/<scenario>.json`
2. **Validate:** Run scenario again, compare with `compare_validation_reports()`,
   assert no regressions
3. **Promote:** When tuning changes improve metrics, overwrite baseline with new report
4. **CI integration:** CTest runs all `add_test()` targets; benchmarks run separately
   in a nightly/weekly job

This workflow directly uses `ValidationPersistence::save_report()` and
`ValidationPersistence::load_report()` from ZPC's existing validation infrastructure.
