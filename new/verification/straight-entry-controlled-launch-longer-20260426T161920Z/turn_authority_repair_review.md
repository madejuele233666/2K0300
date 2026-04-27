# Turn Authority Repair Review

- Date: 2026-04-27 Asia/Shanghai
- Scope: BEV turn authority restored through existing PID tuning surface

## Change

The intermediate BEV control-error scale parameters were removed. They added tuning cost without adding real geometric meaning.

The repair now keeps the control path simple:

- BEV geometry/control model exports the existing raw error fields.
- `LegacyPidControl` consumes those raw BEV error fields directly.
- Turn authority is restored by retuning the existing `PID_TURN_CAMERA.P` parameter.
- No new `BEV_CONTROL_MODEL.*_CONTROL_ERROR_SCALE` knobs are present.

## Current Baseline

- `PID_TURN_CAMERA.P=3000`
- `PID_TURN_GYRO_CAMERA.P=0.5`
- `pwm_limit=5000`
- `wheel_turn_target_scale=100`

A representative `near_lateral_error=-0.10`, `far_heading_error=-0.012` produces on the first smoothed control cycle:

- `camera_p_term`: about `-300`
- `raw_turn_output`: about `-144`
- wheel target split: about `5.76`

This replaces the prior powered-run behavior where the same order of offset produced `raw_turn_output=-1/-2` and only `0.04..0.08` wheel target split.

## Verification

PASS:

- `bash new/verification/tests/run_legacy_pid_control_cycle_test.sh`
- `bash new/verification/tests/run_scene_classifier_selftest.sh`
- `bash new/verification/tests/run_steering_media_selftest.sh`
- `bash new/verification/tests/run_authority_baseline_validate.sh`

BUILD PASS:

- `./new/user/build.sh` compiled `/home/madejuele/projects/2K0300/new/out/new`
- Output is a LoongArch ELF runtime binary.

LIMITATION:

- `bash new/verification/tests/run_runtime_params_load_test.sh` built `runtime_params_load_test`, but did not run because `qemu-loongarch64` is unavailable on this host.
- `./new/user/build.sh` failed only at its final upload step because SSH to `10.100.170.226:22` timed out. The runtime binary build itself succeeded. No board runtime was started by this repair review.

## Remaining Findings

The earlier longer-launch scene finding remains open:

- late `active_module=circle`
- `scene_phase=circle_entry/circle_interior`
- `reference_mode=inner_offset`

That is separate from the PID authority adjustment. The next powered test should still start with no-motion or a very short controlled straight run after board access is restored.
