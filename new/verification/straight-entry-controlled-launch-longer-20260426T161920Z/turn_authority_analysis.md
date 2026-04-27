# Turn Authority Analysis

Context: user observed that the vehicle did not auto-center and showed almost no differential drive even with a large lateral offset.

## Evidence

The observation matches the board log.

In the later RUNNING frames:

- frame 65: `near_lateral_error=-0.089831`, `w_target=-1.94632`, `gyro_p_term=-1.0314`, `raw_turn_output=-1`, `applied_turn_output=-1`
- frame 69: `near_lateral_error=-0.0974481`, `w_target=-2.08005`, `gyro_p_term=-1.06262`, `raw_turn_output=-1`, `applied_turn_output=-1`
- frame 71: `near_lateral_error=-0.102018`, `w_target=-2.16117`, `gyro_p_term=-1.2314`, `raw_turn_output=-1`, `applied_turn_output=-1`
- frame 75: `near_lateral_error=-0.111159`, `preview_curvature=-0.128735`, `w_target=-3.21295`, `gyro_p_term=-1.66951`, `raw_turn_output=-2`, `applied_turn_output=-2`
- frame 79: `near_lateral_error=-0.115729`, `w_target=-2.49522`, `gyro_p_term=-1.22977`, `raw_turn_output=-1`, `applied_turn_output=-1`

The control path was not being suppressed during these frames:

- `veto=false`
- `turn_suppressed=false`
- `effective_speed_target=100`
- `raw_turn_output == applied_turn_output`

The wheel target mixer then converts turn output as:

```text
turn_delta = applied_turn_output / pwm_limit * wheel_turn_target_scale
left_target = effective_speed_target - turn_delta
right_target = effective_speed_target + turn_delta
```

For this run, `pwm_limit=5000` and `wheel_turn_target_scale=100`.

That means:

- `applied_turn=-1` -> target split `100.02 / 99.98`
- `applied_turn=-2` -> target split `100.04 / 99.96`

This is only `0.04` to `0.08` speed-unit gap between left and right targets. It is too small to visibly re-center the car and is smaller than the measured wheel-speed variation in the same run.

## Cause

The narrowest failing boundary is `BEV control error model -> legacy PID / wheel-target mixer`.

The BEV refactor correctly exposes metric control errors:

- lateral error in meters
- heading error in radians
- curvature in approximately inverse meters

But `LegacyPidControl::ComputeTurnTarget()` still feeds those metric values into the old two-stage camera/gyro turn chain as if the upstream error magnitude were large enough to produce a `turn_pwm`-like output. With the current parameters:

- `PID_TURN_CAMERA.P=20`
- `PID_TURN_GYRO_CAMERA.P=0.5`

the effective proportional turn authority is about `10 raw_turn units per meter` before heading/curvature terms and smoothing. A 0.10 m lateral error therefore produces roughly `1` raw turn unit, which then becomes almost zero wheel-speed split after `WheelTargetMixer`.

The wheel PID is not the primary cause here because it is only asked to track nearly identical left/right speed targets.

## Related But Separate

The late `active_module=circle` / `reference_mode=inner_offset` transition is a separate scene/reference finding. It can change the selected reference path, but it does not explain the lack of differential during ordinary RUNNING frames, where `active_module=straight`, `scene_phase=idle`, and `reference_mode=centerline`.

## Required Direction

Do not patch this by arbitrarily increasing one PID number on the board.

The BEV-first design needs an explicit control-authority contract between metric BEV error and actuator turn command. That contract should live in the control model / controller boundary, not inside scene logic or compatibility fields.

The next repair should add or formalize a BEV metric-to-turn scaling policy with local tests that prove a representative lateral offset produces a meaningful `applied_turn_output` and left/right target split while preserving suppression, speed-limit, and fail-safe behavior.
