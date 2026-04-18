# Phase A IMU Closure

- Evidence logs:
  - `openspec/changes/archive/2026-04-17-close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-imu-closure.log`
  - `openspec/changes/archive/2026-04-17-close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-imu-static-raw-20260417.log`
  - `openspec/changes/archive/2026-04-17-close-phase-a-hardware-loop-and-runtime-unveto/verification/imu_direction_live_20260417.log`
  - `openspec/changes/archive/2026-04-17-close-phase-a-hardware-loop-and-runtime-unveto/verification/imu_ccw_live_20260417.log`
- Board run date: `2026-04-17`
- Result: `closed`

## What This Run Proves

1. Default `direct-match` startup still reaches `imu.init`, `imu.detect`, `startup.complete`, and `control.start`.
2. The adapter now performs project-owned IMU normalization at the owning boundary instead of leaking raw vendor counts into runtime:
   - gyro zero-bias priming is reported by `imu.calibration.ready`
   - continuous valid runtime sampling is reported by `imu.continuity.ready`
   - normalized samples are observable through `imu.sample.summary`
3. Runtime IMU validity now stays continuously true long enough for control to leave the initial startup veto window.
4. A named static bench capture now closes the "is the board stationary and continuously valid" part of the contract:
   - `phase-a-imu-static-raw-20260417.log` captured 11 consecutive raw samples from `/sys/bus/iio/devices/iio:device1`
   - `accel_z_raw` stayed in `[-4105, -3904]`
   - `gyro_z_raw` stayed in `[-19, 10]`
   - this is sufficient Phase A evidence that the current bench pose is stable and not drifting in yaw while stationary
5. A named live direction capture now closes the yaw-sign part of the contract:
   - `imu_ccw_live_20260417.log` reached `gyro_z=-1.180320 radps` during the counter-clockwise hand-rotation segment
   - the paired live direction capture also shows the opposite positive excursion up to `gyro_z=+0.262766 radps`
   - this makes the current project-owned `gyro_z` sign interpretable for the differential-drive control path

## Scope Note

1. The current Phase A control path consumes `gyro_z`; it does not close control on `acc_x/acc_y`.
2. The observed `acc_z` magnitude remains a bench sanity signal rather than a Phase A control gate.
3. Any future accelerometer scale refinement can be handled in later tuning without blocking Phase B entry.

## Stage Impact

- IMU startup proof remains accepted.
- IMU runtime continuity, static bench pose, and yaw-direction interpretation are now closed at Phase A scope.
- IMU is no longer a Phase A exit blocker.
