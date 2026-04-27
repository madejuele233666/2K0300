# Straight Entry Controlled Launch - Longer Distance Review

- Date: 2026-04-27 Asia/Shanghai
- Board: 10.100.170.226
- Host listener: 10.100.170.115
- Runtime params: `/home/root/default_params.launch_100.json`
- Local params copy: `default_params.launch_100.json`
- Command profile: normal, `LS2K_AUTO_START=1`, `LS2K_AUTO_START_DELAY_MS=200`, `LS2K_AUTO_STOP_AFTER_MS=2500`
- Speed target: `Speed_base=100`
- Projector: `bev_projector_straight_entry_fixed_camera_v4`
- Projector hash: `bev-projector-straight-entry-fixed-camera-20260426T151213Z`

## Safety / Lifecycle

PASS.

- Runtime exited by controlled auto-stop and reported `runtime_status=stopped`.
- Motion lifecycle was `DISARMED -> START_REQUESTED -> SPINUP -> RUNNING -> STOPPING -> DISARMED`.
- `motion.stop.complete` was reached and `shutdown.complete` disabled actuators and released resources.
- No forced stop fallback was required.
- Initial perception-stale veto occurred only before fresh perception; after that the control path was eligible.

## Control Summary

- `control.snapshot` rows: 29
- `control.steering_snapshot` rows: 26
- `effective_speed_target` max: 100
- measured speed max: left 108, right 109
- PWM max: left 3019, right 2738
- PWM returned to 0 during STOPPING / DISARMED.
- `raw_turn_output` / `applied_turn_output`: min -2, median near 0, max 0.

## BEV / Scene Summary

- `track_valid=true` for all steering snapshots.
- `visible_range_m=4.5` for all steering snapshots.
- `track_confidence`: min 0.6625, median about 0.83125, max 0.94375.
- `near_lateral_error`: min -0.141627, median about -0.0624097, max -0.0306486.
- `far_heading_error`: min -0.0193432, median about -0.00920928, max -0.00362386.
- `preview_curvature`: min -0.128735, median about -0.0011404, max 0.00320106.
- Special-scene counters from board snapshots:
  - `active_module=straight`: 21
  - `active_module=bend`: 1
  - `active_module=circle`: 4
  - `scene_phase=idle`: 21
  - `scene_phase=bend_veto`: 1
  - `scene_phase=circle_entry`: 1
  - `scene_phase=circle_interior`: 3
  - `reference_mode=centerline`: 22
  - `reference_mode=inner_offset`: 4
- Cross and zebra candidates stayed false.

## Steering Media

PASS for connection and payload integrity, but incomplete for the later scene finding.

- Listener connected from `10.100.170.226:50138`.
- `frame_count=8`.
- `receiver_error=null`.
- Captured frames were `gray8`, `320x240`, 76800 bytes each.
- Captured media frames covered early SPINUP / early RUNNING only; the later STOPPING frames where `active_module=circle` appeared are not present in the media bundle.

## Finding

FINDING: The longer straight-entry powered run exposed a scene/reference authority issue before any further distance increase should be attempted.

At the end of the run, the board snapshots transitioned from ordinary straight control into `active_module=circle`, `scene_phase=circle_entry/circle_interior`, and `reference_mode=inner_offset`. This happened after `motion.stop.requested`, while the vehicle was decelerating, with `circle_left_candidate` first false at entry and then true for later STOPPING frames. If the car was still in the intended straight-entry segment, this is a false-positive scene latch. Even if the physical car reached a downstream feature, the current evidence is insufficient because the media bundle does not include the late frames that triggered the state change.

The next action should be local source-first investigation of the BEV scene FSM / reference-policy boundary using this log and existing baseline images. Do not increase distance again until the scene transition is either explained by aligned image evidence or fixed at the BEV observation/FSM contract level.

## Files

- Board log: `new_launch_controlled_100_longer.log`
- Runtime status: `launch_remote_status_after_wait.txt`
- Media summary: `launch-steering-media/summary.json`
- Media metadata: `launch-steering-media/frame_metadata.jsonl`
- Media config snapshot: `launch-steering-media/config_snapshot.json`
