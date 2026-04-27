# PID Retune Motor-Disabled Board Evidence

Run time: 2026-04-26T23:18:10Z host capture, 2026-04-27 local time.

Board: `10.100.170.226`
Host source: `10.100.170.115`
Evidence directory: `/home/madejuele/projects/2K0300/new/verification/pid-retune-motor-disabled-20260426T231725Z`
Direct board log: `board_runtime_direct.log`

## Configuration Under Test

- `Speed_base=100.0`
- `PID_TURN_CAMERA.P=12000.0`
- `PID_TURN_CAMERA.USE_FUZZY=0`
- `PID_TURN_CAMERA.D=0.0`
- `PID_TURN_GYRO_CAMERA.P=0.5`
- `wheel_turn_target_scale=100.0`
- `raw_turn_output_limit=8000`
- Motor profile: `motor.mode=disabled`

## Safety Result

PASS. The runtime used the diagnostics-only motor profile and suppressed drive application:

- `profile.motor disabled:imu-check-motor-disabled`
- `motor.disabled`
- `control.start.degraded.motor_disabled`
- `control.arm.motor_disabled`
- `control.apply.suppressed drive command suppressed because the active motor profile is diagnostics-only`
- Final lifecycle returned to `DISARMED` and emitted `shutdown.complete`.

The run exercised control computation only. Physical motor output was not enabled.

## Steering Authority Result

PASS for the specific question "does the edited camera P restore visible differential command generation at Speed_base=100".

Representative RUNNING snapshots:

- `near_lateral_error=0.0331041`
- `far_heading_error=0.0392683..0.0410505`
- `preview_curvature=0.0189029`
- `visible_range_m=4.5`
- `track_confidence=0.71875`
- `active_module=straight`
- `scene_phase=idle`
- `reference_mode=centerline`
- `resolved_fuzzy_p=12000`
- `raw_turn_output=368..374`
- `applied_turn_output=368..374`
- Wheel target split at `Speed_base=100`: about `14.72..14.96` target units

This confirms the authority path is now numerically active through:

`BEV errors -> PID_TURN_CAMERA.P -> w_target -> PID_TURN_GYRO_CAMERA.P -> raw_turn_output -> wheel_target_mixer`

## Scene Result

PASS for no obvious false scene activation during this short straight-entry static run:

- `active_module=straight`
- `scene_phase=idle`
- `reference_mode=centerline`
- `cross_candidate=false`
- `circle_left_candidate=false`
- `circle_right_candidate=false`

## Evidence Gap

The host assistant and steering-media listeners did not connect in this run:

- `assistant TCP connect failed: Connection refused`
- `steering media TCP connect failed: Connection refused`
- `assistant_connected=false`
- `steering_media_connected=false`

Therefore this run is valid as a board-log control-authority check, but not as media-aligned image evidence. A later media capture should use a longer listener window or preflight the host listener sockets before starting the board runtime.

## Next Gate

Before a powered low-speed run, collect one clean no-motor or no-auto-start media-aligned capture if image/snapshot alignment is needed. For motion validation, keep `Speed_base=100` and use a short straight-entry run after explicitly switching back to the normal motor profile.
