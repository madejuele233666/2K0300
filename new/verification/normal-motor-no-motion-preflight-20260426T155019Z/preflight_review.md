# Normal motor no-motion preflight - 2026-04-26T15:50:19Z

Board: `10.100.170.226`

Host route source: `10.100.170.115`

## Scope

- Runtime used the normal direct-match profile: camera, IMU, encoder, motor, timer, and persistence.
- `LS2K_ALLOW_DEGRADED_STARTUP` was not set for the normal profile runs.
- No assistant control listener was started and no `set_target_speed` or start command was sent.
- The car was off track during this check, so perception veto is expected and not treated as a BEV geometry finding.

## Startup Results

- Low-voltage startup check cleared:
  - first run raw `2415`, threshold `400`
  - media run raw `2417`, threshold `400`
- Camera initialized through `/dev/video0`.
- IMU initialized as `imu660ra` at `/sys/bus/iio/devices/iio:device1`.
- IMU gyro zero-bias calibration completed from 32 startup samples.
- Encoder initialized with left `/dev/zf_encoder_1`, right `/dev/zf_encoder_2`.
- Motor initialized through the true LS2K bridge:
  - logical left maps to `/dev/zf_device_pwm_motor_2`
  - logical right maps to `/dev/zf_device_pwm_motor_1`
- Timer started and the control loop reached `control.start`.

## No-Motion Safety Evidence

- Harness context reported `auto_start=false`.
- First run:
  - `47` `control.snapshot` lines
  - no nonzero `effective_speed_target`
  - no nonzero left/right wheel target
  - no nonzero left/right PWM
  - no phase other than `DISARMED`
- Media run:
  - `62` `control.snapshot` lines
  - no nonzero `effective_speed_target`
  - no nonzero left/right wheel target
  - no nonzero left/right PWM
  - no phase other than `DISARMED`
- No unexpected startup `FAIL_SAFE` or `ERROR` entries were found after excluding the expected perception veto and emergency-stop application while off track.
- No `new` runtime process was left running after the checks.

## Steering Media Evidence

- Steering-media connected from `10.100.170.226:50092`.
- Config snapshot was received.
- `36` raw `320x240` frames were captured.
- `summary.json` reports `receiver_error=null`.
- Off-track perception state stayed fail-safe:
  - `motion_phase=DISARMED`
  - `track_valid=false`
  - `effective_speed_target=0`
  - `raw_turn_output=0`
  - `applied_turn_output=0`

## Conclusion

Normal-profile hardware startup passed, including motor adapter initialization. The no-motion safety surface behaved correctly: no start command produced no drive phase, no wheel target, and no PWM. The perception veto is expected because the vehicle was off track during this check.

Before low-speed motion, place the car on the straight centerline and repeat a normal-profile no-motion media check. The next gate should show a sane BEV observation on track and no persistent perception emergency veto before any motion command is issued.
