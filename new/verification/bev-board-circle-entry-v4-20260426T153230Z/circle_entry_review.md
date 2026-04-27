# BEV Circle Entry V4 Review

- Pose: circle entry.
- Runtime profile: diagnostic capture profile with `imu` and `motor` disabled under `LS2K_ALLOW_DEGRADED_STARTUP=1`.
- Projector: `bev_projector_straight_entry_fixed_camera_v4`.
- Safety evidence: board snapshots stayed `phase=DISARMED`; `raw_turn_output=0`; `applied_turn_output=0`; motor profile was disabled.
- Board snapshots: `129`.
- Steering-media frames: `7`.
- Representative frame: `steering-media/frames/frame-000012.raw`.
- Overlay: `circle-entry-overlay-frame-000012.png`.

## Interpretation

The BEV scene evidence correctly separates this from cross:
`circle_left_candidate=true`, `cross_candidate=false`, and `cross_bilateral_open=false`.
The board FSM owned the scene as `active_module=circle` with `circle_direction=left` and
`reference_mode=inner_offset`.

## Evidence Summary

- `near_lateral_error`: median `0.0361509 m`.
- `far_heading_error`: median `0.0130148 rad`.
- `preview_curvature`: median `0.0060614`.
- `visible_range_m`: `4.5 m`.
- `track_confidence`: `0.71875`.
- `active_module`: `circle` only.
- `scene_phase`: `circle_interior` only.
- `reference_mode`: `inner_offset` only.
- `circle_direction`: `left` only.
- `circle_left_candidate`: true.
- `circle_right_candidate`: false.
- `cross_candidate`: never true.
- `scene_evidence.left_open_score`: median `0.350349`.
- `scene_evidence.right_open_score`: median `0.0634756`.
- `scene_evidence.cross_bilateral_open_score_m`: median `0.0634756 m`.

## Boundary

The static no-motor run confirms circle ownership and BEV evidence. It also shows that the
current FSM progresses from entry to interior by cycle count even when the vehicle is stationary
and IMU is disabled. Treat that as a scene-FSM progression semantics item, not a BEV geometry issue.
