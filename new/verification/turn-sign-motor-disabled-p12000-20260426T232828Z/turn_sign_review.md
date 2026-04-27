# Turn Sign Motor-Disabled Evidence

Run time: 2026-04-26T23:28:28Z host-created evidence directory.

Board: `10.100.170.226`
Evidence directory: `/home/madejuele/projects/2K0300/new/verification/turn-sign-motor-disabled-p12000-20260426T232828Z`
Direct board log: `board_runtime_direct.log`

## Configuration Under Test

- Source of truth: local `new/config/default_params.json`
- `Speed_base=100.0`
- `PID_TURN_CAMERA.P=12000.0`
- `PID_TURN_GYRO_CAMERA.P=0.5`
- Motor profile: `motor.mode=disabled`

## Result

PASS. With positive BEV control error and positive turn output, the rebuilt mixer now produces a right-turn wheel target split:

- `near_lateral_error=0.0120069`
- `far_heading_error=0.0561499`
- `resolved_fuzzy_p=12000`
- `raw_turn=282`
- `applied_turn=282`
- `left_target=105.64`
- `right_target=94.36`

Before the mixer sign correction, positive turn output produced `left_target < right_target`, which the powered run showed as a left turn. The current contract is now:

`positive_right BEV error -> positive turn_output -> left wheel target higher than right wheel target -> right turn`

## Safety

PASS. The run used a diagnostics-only motor profile:

- `motor.disabled`
- `control.arm.motor_disabled`
- `control.apply.suppressed drive command suppressed because the active motor profile is diagnostics-only`
- `motion.stop.complete`
- `shutdown.complete`

No physical motor drive was enabled in this verification run.
