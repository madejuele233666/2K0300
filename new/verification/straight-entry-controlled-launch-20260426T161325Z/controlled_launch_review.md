# Straight entry controlled launch - 2026-04-26T16:13:25Z

Board: `10.100.170.226`

Host route source: `10.100.170.115`

## Parameters

- Runtime params: `/home/root/default_params.launch_100.json`
- Source evidence params: `default_params.launch_100.json`
- `Speed_base=100`
- `motion_spinup_ms=600`
- `motion_stop_ms=300`
- `motion_unveto_confirm_cycles=3`
- `LS2K_AUTO_START=1`
- `LS2K_AUTO_START_DELAY_MS=200`
- `LS2K_AUTO_STOP_AFTER_MS=1200`
- Normal direct-match hardware profile was used.

## No-Motion Gate

- Runtime started with the same `Speed_base=100` params and `LS2K_AUTO_START` unset.
- Startup passed: camera, IMU, encoder, motor, timer.
- `69` `control.snapshot` lines were captured.
- No nonzero `effective_speed_target` was found.
- No nonzero left/right wheel target was found.
- No nonzero left/right PWM was found.
- `66` `control.steering_snapshot` lines were captured.
- BEV observation was stable:
  - `track_valid=true` for all steering snapshots
  - `track_confidence=0.8875`
  - `visible_range_m=4.5`
  - `near_lateral_error=-0.0212776`
  - `far_heading_error=-0.00278271`
  - `active_module=straight`
  - `scene_phase=idle`
  - no circle/cross/zebra candidate

## Controlled Launch

- Runtime started and exited by itself; no force-stop fallback was used.
- Lifecycle evidence:
  - `motion start requested by LS2K_AUTO_START`
  - `DISARMED -> START_REQUESTED`
  - `START_REQUESTED -> SPINUP`
  - `SPINUP -> RUNNING`
  - `controlled stop requested by LS2K_AUTO_STOP_AFTER_MS`
  - `STOPPING -> DISARMED`
  - `controlled stop reached DISARMED; process may now exit`
  - `shutdown.complete`
- `18` `control.snapshot` lines were captured.
- Motion phase counts:
  - `DISARMED`: 5
  - `SPINUP`: 6
  - `RUNNING`: 3
  - `STOPPING`: 4
- Target and measured speed:
  - `effective_speed_target`: min `0`, max `100`
  - `left_measured`: min `0`, max `112`
  - `right_measured`: min `0`, max `105`
- PWM:
  - `left_pwm`: min `0`, max `2864`
  - `right_pwm`: min `0`, max `2606`
  - both returned to `0` before DISARMED
- Steering:
  - `raw_turn=0`
  - `applied_turn=0`
  - `raw_turn_output=0`
  - `applied_turn_output=0`
- BEV during launch:
  - `track_valid=true` for all `15` steering snapshots
  - `visible_range_m=4.5`
  - `track_confidence`: min `0.8875`, max `0.94375`
  - `near_lateral_error`: min `-0.0426054`, median `-0.0212776`, max `-0.0197542`
  - `far_heading_error`: min `-0.0107695`, median `-0.00663074`, max `-0.000997578`
  - no circle/cross/zebra candidate

## Media

- No-motion steering media:
  - connected
  - `receiver_error=null`
  - `1` raw `320x240` frame
- Launch steering media:
  - connected
  - `receiver_error=null`
  - `5` raw `320x240` frames
  - captured frames were in `SPINUP`

## Conclusion

The straight-entry `Speed_base=100` controlled launch passed. The runtime entered motion, reached RUNNING, requested controlled stop, returned to DISARMED, disabled actuators, and exited without force-stop fallback. BEV stayed valid throughout the run and no special-scene false positive appeared.

Next powered step should keep the same explicit evidence chain and increase only one risk variable at a time: either a slightly longer straight run, or the first bend-entry powered run, not both together.
