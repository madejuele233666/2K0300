# IMU normal check - 2026-04-26T15:47:34Z

Board: `10.100.170.226`

Host route source: `10.100.170.115`

## Scope

- Motor was disabled through `hardware_profile.imu_check_motor_disabled.json`.
- IMU remained `direct-match:imu-core-device-detect`.
- Runtime was started only as a short diagnostics check and then stopped.

## Findings

- `/sys/bus/iio/devices/iio:device1/name` reports `IMU660RA`.
- Raw IMU sysfs channels were readable:
  - `in_anglvel_x_raw=3`
  - `in_anglvel_y_raw=3`
  - `in_anglvel_z_raw=-1`
  - `in_accel_x_raw=117`
  - `in_accel_y_raw=-35`
  - `in_accel_z_raw=-4081`
- Runtime IMU initialization passed:
  - `imu initialized through true_ls2k0300 bridge: resolved IMU resource at /sys/bus/iio/devices/iio:device1`
  - `imu detection path selected: imu660ra`
  - `imu gyro zero-bias calibrated from 32 startup sample(s)`
  - `imu sample stream stayed valid for 32 consecutive reads after bridge normalization`
- Motor remained disabled:
  - `motor profile is disabled; control loop stays diagnostics-only with actuators disarmed`
  - `drive command suppressed because the active motor profile is diagnostics-only`

## Conclusion

IMU enumeration and runtime IMU initialization are normal in this check. This clears the earlier IMU-enumeration blocker for perception/diagnostic runs. A low-speed run still needs a separate normal-runtime preflight with the intended motor profile and explicit user approval before actuators are enabled.
