# Controlled 3s Launch After Turn Sign Fix

Run time: 2026-04-26T23:34:30Z host capture.

Board: `10.100.170.226`
Evidence directory: `/home/madejuele/projects/2K0300/new/verification/controlled-3s-launch-signfix-20260426T233350Z`
Direct board log: `board_runtime_direct.log`

## Configuration Under Test

- Normal hardware profile: `motor.mode=direct-match`
- `Speed_base=100.0`
- `PID_TURN_CAMERA.P=12000.0`
- `PID_TURN_GYRO_CAMERA.P=0.5`
- `LS2K_AUTO_STOP_AFTER_MS=3000`

Host capture connected successfully:

- `board_steering_snapshots=32`
- `steering_media_frames=23`
- `alignment_count=23`

## Direction Result

PASS. The corrected mixer sign remained consistent through both steering directions.

Early RUNNING, positive turn:

- `raw_turn=286`, `left_target=105.72`, `right_target=94.28`, `left_measured=111`, `right_measured=93`
- `raw_turn=433`, `left_target=108.66`, `right_target=91.34`, `left_measured=110`, `right_measured=99`

Late RUNNING, negative turn:

- `raw_turn=-812`, `left_target=83.76`, `right_target=116.24`, `left_measured=87`, `right_measured=114`
- `raw_turn=-1077`, `left_target=78.46`, `right_target=121.54`, `left_measured=74`, `right_measured=126`

## Control Behavior

The 3s run exposed overcorrection/oscillation risk:

- RUNNING started near `near_lateral_error=0.00720613`, `raw_turn=286`.
- RUNNING crossed sign between frames 67 and 72.
- RUNNING ended near `near_lateral_error=-0.117252`, `raw_turn=-1077`.
- RUNNING raw turn ranged from `-1077` to `740`.

This means the direction fix is correct, but the current steering loop should not be extended further without reviewing the 3s evidence. The likely next work item is controller damping/gain behavior, not another sign fix.

## Scene / Lifecycle

During active drive, the runtime stayed in ordinary bend handling:

- `active_module=bend`
- `scene_phase=bend_veto`
- `reference_mode=centerline`

Circle entry appeared only during STOPPING/DISARMED after drive output had been zeroed.

PASS for bounded lifecycle:

- `motion.stop.requested` fired from `LS2K_AUTO_STOP_AFTER_MS`.
- `motion.stop.complete` returned to `DISARMED`.
- `shutdown.complete` emitted and no runtime process remained after the run.
