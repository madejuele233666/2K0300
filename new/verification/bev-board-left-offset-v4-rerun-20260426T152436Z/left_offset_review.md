# BEV Left Offset V4 Review

- Pose: straight track, vehicle/camera placed left of the lane centerline.
- Runtime profile: diagnostic capture profile with `imu` and `motor` disabled under `LS2K_ALLOW_DEGRADED_STARTUP=1`.
- Projector: `bev_projector_straight_entry_fixed_camera_v4`.
- Safety evidence: board snapshots stayed `phase=DISARMED`; `raw_turn_output=0`; `applied_turn_output=0`; motor profile was disabled.
- Board snapshots: `204`.
- Steering-media frames: `4`.
- Representative frame: `steering-media/frames/frame-001957.raw`.
- Overlay: `left-offset-overlay-frame-001957.png`.

## Interpretation

The BEV geometry reports the expected sign for a left-offset placement:
the lane centerline is to the vehicle's right, so `near_lateral_error` is positive.

## Evidence Summary

- `near_lateral_error`: mean/median `0.0574786 m`.
- `far_heading_error`: mean/median `0.00010284 rad`.
- `preview_curvature`: median `0.00132943`.
- `visible_range_m`: `4.5 m`.
- `track_confidence`: `0.83125`.
- `active_module`: `straight` only.
- `scene_phase`: `idle` only.
- `cross_candidate`: never true.
- `circle_left_candidate` / `circle_right_candidate`: never true.
- `scene_evidence.cross_bilateral_open_score_m`: median `0.00172703 m`.
- `scene_evidence.width_expand_ratio`: median `1.15648`.

## Boundary

This run validates perception geometry and scene evidence only. It does not validate motor control,
because the motor profile was intentionally disabled for the no-motor test.
