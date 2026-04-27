# Controlled Short Launch After Turn Sign Fix

Run time: 2026-04-26T23:31:53Z host capture.

Board: `10.100.170.226`
Evidence directory: `/home/madejuele/projects/2K0300/new/verification/controlled-short-launch-signfix-20260426T233121Z`
Direct board log: `board_runtime_direct.log`

## Configuration Under Test

- Normal hardware profile: `motor.mode=direct-match`
- `Speed_base=100.0`
- `PID_TURN_CAMERA.P=12000.0`
- `PID_TURN_GYRO_CAMERA.P=0.5`
- `LS2K_AUTO_STOP_AFTER_MS=1000`

The steering-media config snapshot confirms the runtime-loaded steering params.

## Control Direction Result

PASS for the corrected turn-sign contract.

Representative powered SPINUP samples:

- `raw_turn=291`, `applied_turn=291`, `left_target=18.4867`, `right_target=6.84667`, `left_pwm=1841`, `right_pwm=721`
- `raw_turn=291`, `applied_turn=291`, `left_target=35.1533`, `right_target=23.5133`, `left_pwm=2942`, `right_pwm=1712`
- `raw_turn=232`, `applied_turn=232`, `left_target=68.4733`, `right_target=59.1933`, `left_pwm=2770`, `right_pwm=2377`

The measured encoder deltas also followed the commanded split during SPINUP:

- `left_measured=41`, `right_measured=30`
- `left_measured=66`, `right_measured=52`
- `left_measured=88`, `right_measured=70`
- `left_measured=109`, `right_measured=92`

This is the opposite wheel target direction from the failed powered run before the `WheelTargetMixer` sign correction.

## Perception / Scene

The run stayed ordinary through active drive:

- `active_module=straight`
- `scene_phase=idle`
- `reference_mode=centerline`
- `cross_candidate=false`
- `circle_left_candidate=false`
- `circle_right_candidate=false`

Near error reduced across SPINUP from `0.0135303` to `-0.00779745`, while `raw_turn_output` tapered from `291` to `157`.

## Lifecycle / Safety

PASS. The run was powered but bounded:

- Startup used the normal motor profile.
- Host captured `12` board steering snapshots and `10` steering-media frames.
- `motion.stop.requested` fired after the configured auto-stop window.
- `motion.stop.complete` returned the runtime to `DISARMED`.
- `shutdown.complete` emitted and no runtime process remained after the run.
