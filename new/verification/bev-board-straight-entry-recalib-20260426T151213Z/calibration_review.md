# BEV Projector Recalibration Review

- Capture: straight entry, camera mechanically fixed, no motor command.
- Runtime profile: diagnostic capture profile with `imu` and `motor` disabled under `LS2K_ALLOW_DEGRADED_STARTUP=1`.
- Safety evidence: board snapshots stayed `phase=DISARMED`; `raw_turn_output=0`; `applied_turn_output=0`; motor profile was disabled.
- Captured media frame: `steering-media/frames/frame-001671.raw`.
- Overlay: `straight-entry-overlay-frame-001671.png`.

## Decision

`BEV_PROJECTOR` was updated for this fixed-camera pose.

- New `PROJECTOR_ID`: `bev_projector_straight_entry_fixed_camera_v4`.
- New `PROJECTOR_HASH`: `bev-projector-straight-entry-fixed-camera-20260426T151213Z`.
- Source points: `(220, 19.0)`, `(220, 305.0)`, `(68, 108.0)`, `(68, 220.0)`.
- Target points remain the nominal 0.42 m lane contract at 0.45 m and 4.5 m.

## Evidence Summary

- Before recalibration, `near_lateral_error`: mean/median `0.00479937 m`.
- After recalibration on `frame-001671.raw`, `near_lateral_error`: `-0.000180404 m`.
- After recalibration on `frame-001671.raw`, `far_heading_error`: `-0.00542208 rad`.
- `visible_range_m`: `4.5 m`.
- `track_confidence`: `1.0`.
- `active_module`: `straight` only.
- `scene_phase`: `idle` only.
- `cross_candidate`: never true.
- `circle_left_candidate` / `circle_right_candidate`: never true.
- Before recalibration, `scene_evidence.cross_bilateral_open_score_m`: median `0.025374 m`.
- After recalibration on `frame-001671.raw`, `scene_evidence.cross_bilateral_open_score_m`: `0.0044136 m`.
- Before recalibration, `scene_evidence.width_expand_ratio`: median `1.11906`.
- After recalibration on `frame-001671.raw`, `scene_evidence.width_expand_ratio`: `1.02927`.

## Boundary

The diagnostic profile is an evidence-capture workaround for the current board IMU enumeration failure:
normal runtime startup failed because the IMU adapter could not find a supported IIO name file.
This review does not change production startup, scene logic, PID, or BEV geometry code.
