# Bend entry v4 normal-profile static check - 2026-04-26T15:54:07Z

Board: `10.100.170.226`

Host route source: `10.100.170.115`

## Scope

- Vehicle was placed at the bend entrance.
- Runtime used the normal direct-match hardware profile.
- No assistant control listener was started.
- No `set_target_speed`, start, or tuning command was sent.
- Runtime was stopped after the capture.

## Safety Surface

- `71` `control.snapshot` lines were captured in the board log.
- All control snapshots stayed `phase=DISARMED`.
- No nonzero `effective_speed_target` was found.
- No nonzero left/right wheel target was found.
- No nonzero left/right PWM was found.
- No unexpected startup `FAIL_SAFE` or `ERROR` entry was found after excluding the expected perception/control veto path.
- No `new` runtime process was left running.

## Adapter Startup

- Low-voltage check cleared with raw values around `2403..2419`, threshold `400`.
- Camera initialized at `/dev/video0`.
- IMU initialized as `imu660ra` at `/sys/bus/iio/devices/iio:device1`.
- IMU gyro zero-bias calibration completed from 32 startup samples.
- Encoder initialized at `/dev/zf_encoder_1` and `/dev/zf_encoder_2`.
- Motor adapter initialized through the true LS2K bridge.
- Timer started and control loop reached `control.start`.

## Steering Media

- Steering-media connected from `10.100.170.226:50106`.
- Config snapshot was received.
- `45` raw `320x240` frames were captured.
- `summary.json` reports `receiver_error=null`.

## BEV / Scene Metrics

All `45` media frames reported the same stable primary observation:

- `track_valid=true`
- `track_confidence=0.831250071526`
- `visible_range_m=4.5`
- `near_lateral_error=0.00286641530693`
- `far_heading_error=0.0492189973593`
- `preview_curvature=0.0115245878696`
- `active_module=straight`
- `scene_phase=idle`
- `reference_mode=centerline`
- `scene_override_source=none`
- `circle_left_candidate=false`
- `circle_right_candidate=false`
- `cross_candidate=false`
- `zebra_candidate=false`
- `raw_turn_output=0`
- `applied_turn_output=0`

Representative frame:

- Raw: `steering-media/frames/frame-000221.raw`
- Overlay: `bend-entry-v4-overlay-frame-000221.png`
- Overlay probe: `projector_id=bev_projector_straight_entry_fixed_camera_v4`

## Conclusion

The bend-entry static geometry check passes. BEV geometry is stable, reaches the configured 4.5 m preview, and does not misclassify the bend entrance as circle, cross, or zebra. No code change is indicated by this run.

For first powered low-speed testing, prefer starting from a straight-centerline pose. Use the bend entry after the straight low-speed gate proves actuator direction, wheel sign, and low-speed stop behavior.
