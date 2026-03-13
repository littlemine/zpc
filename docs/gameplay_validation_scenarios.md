# Gameplay Validation Scenarios & Artifact Layout

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
| `gameplay.input.dead_zone` | Verify dead-zone suppresses values below threshold | axis output = 0 when raw < threshold | `threshold: [0.05, 0.3]` |
| `gameplay.input.response_curve` | Verify response curves transform axis values correctly | processed vs. expected at 10 sample points | `curve: {linear, quadratic, cubic}` |
| `gameplay.input.action_phases` | Verify pressed/held/released transitions over N frames | correct phase sequence | `holdFrames: [1, 60]` |
| `gameplay.input.buffer_replay` | Verify input buffer stores and replays N frames | replayed snapshot matches original | `bufferSize: [8, 64]` |
| `gameplay.input.latency` | Measure input phase processing time | phase_ms ≤ 0.2 | — |

### 1.2 Camera Validation Scenarios

| Scenario ID | Description | Key Metrics | Parameters |
|------------|-------------|-------------|------------|
| `gameplay.camera.orbit_360` | Orbit camera 360° around target at fixed distance | final position matches start (ε < 1e-4), jitter ≤ 0.5 px RMS | `distance: [5, 50]`, `speed: deg/s` |
| `gameplay.camera.follow_moving` | Follow a target moving in a straight line | offset error ≤ threshold after settling, jitter ≤ 0.5 px | `targetSpeed: [1, 20] m/s`, `smoothing: [0.05, 0.5]` |
| `gameplay.camera.fps_pitch_clamp` | FPS camera pitch clamped to [-89°, 89°] | pitch never exceeds bounds | `inputRate: deg/frame` |
| `gameplay.camera.mode_transition` | Blend between two camera modes | no discontinuity in position/orientation (max delta ≤ threshold) | `from: mode`, `to: mode`, `duration: [0.1, 2.0] s` |
| `gameplay.camera.shake_decay` | Apply trauma and verify shake decays to zero | amplitude ≤ ε after decay time | `trauma: [0.1, 1.0]`, `decayRate: [0.5, 5.0]` |
| `gameplay.camera.projection_reference` | Verify perspective matrix against reference values | element-wise error ≤ 1e-6 | `fov, aspect, near, far` |
| `gameplay.camera.lookAt_reference` | Verify lookAt matrix against reference values | element-wise error ≤ 1e-6 | `eye, target, up` |
| `gameplay.camera.phase_budget` | Measure camera phase processing time | phase_ms ≤ 0.5 | — |

### 1.3 Character Validation Scenarios

| Scenario ID | Description | Key Metrics | Parameters |
|------------|-------------|-------------|------------|
| `gameplay.char.state_transitions` | Walk through all defined state transitions | 100% correct transitions, no invalid states | — |
| `gameplay.char.coyote_time` | Jump after leaving ledge within coyote window | jump succeeds within ± 1 frame | `coyoteFrames: [3, 12]` |
| `gameplay.char.jump_buffer` | Buffer jump input before landing | jump executes within ± 1 frame of landing | `bufferFrames: [3, 12]` |
| `gameplay.char.variable_jump` | Release jump early for shorter jump | apex height proportional to hold duration | `holdFrames: [1, 30]` |
| `gameplay.char.accel_curve` | Verify acceleration curve matches configured profile | velocity at t matches expected ± ε | `profile: {linear, ease_in, ease_out, ease_in_out}` |
| `gameplay.char.ground_detection` | Verify ground state updates with mock query | grounded/airborne transitions correct | — |
| `gameplay.char.sprint_velocity` | Verify sprint produces correct max velocity | velocity within ± 1% of configured max | `maxSpeed: [5, 15] m/s` |
| `gameplay.char.phase_budget` | Measure character phase processing time | phase_ms ≤ 0.5 | — |

### 1.4 Integration Validation Scenarios

| Scenario ID | Description | Key Metrics | Parameters |
|------------|-------------|-------------|------------|
| `gameplay.integration.full_loop` | Run input → character → camera for 600 frames | total_phase_ms ≤ 2.0, no crashes | — |
| `gameplay.integration.input_to_screen` | Measure end-to-end input-to-camera latency | latency ≤ 16.67 ms | — |
| `gameplay.integration.camera_follow_character` | Character moves, camera follows with configured mode | offset converges, jitter ≤ 0.5 px | `mode: {orbit, follow, fps}` |

---

## 2. Artifact Directory Layout

```
zpc_assets/
  gameplay/
    baselines/
      input/
        dead_zone.json            # baseline report for gameplay.input.dead_zone
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
  gameplay/
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
# --- Gameplay Tests ---

# Input
add_executable(zs_gameplay_dead_zone gameplay/input/dead_zone.cpp)
target_link_libraries(zs_gameplay_dead_zone PRIVATE zpc_gameplay_input)
add_test(ZsGameplayInputDeadZone zs_gameplay_dead_zone)
add_dependencies(zensim zs_gameplay_dead_zone)

# Camera
add_executable(zs_gameplay_projection_ref gameplay/camera/projection_reference.cpp)
target_link_libraries(zs_gameplay_projection_ref PRIVATE zpc_gameplay_camera)
add_test(ZsGameplayCameraProjectionRef zs_gameplay_projection_ref)
add_dependencies(zensim zs_gameplay_projection_ref)

# Benchmark (no add_test)
add_executable(zs_gameplay_camera_bench gameplay/camera/camera_phase_benchmark.cpp)
target_link_libraries(zs_gameplay_camera_bench PRIVATE zpc_gameplay_camera)
add_dependencies(zensim zs_gameplay_camera_bench)
```

---

## 3. Baseline Workflow

1. **Create baseline:** Run scenario, save `ValidationSuiteReport` as JSON to
   `zpc_assets/gameplay/baselines/<category>/<scenario>.json`
2. **Validate:** Run scenario again, compare with `compare_validation_reports()`,
   assert no regressions
3. **Promote:** When tuning changes improve metrics, overwrite baseline with new report
4. **CI integration:** CTest runs all `add_test()` targets; benchmarks run separately
   in a nightly/weekly job

This workflow directly uses `ValidationPersistence::save_report()` and
`ValidationPersistence::load_report()` from ZPC's existing validation infrastructure.
