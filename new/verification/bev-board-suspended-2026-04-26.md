# BEV Board Suspended Verification - 2026-04-26

## Context

- Host direct-Ethernet source: `169.254.6.147`
- Board direct-Ethernet address: `169.254.148.181`
- Board runtime transport: `assistant=8888`, `steering_media=8890`
- Safety condition: board suspended off-track, motor-disabled smoke used for runtime harness

## Build And Deploy

- Host build/upload completed with:
  - `BOARD_IP=169.254.148.181 ./debug.sh build`
- Normal runtime restarted successfully with:
  - `BOARD_IP=169.254.148.181 ./debug.sh remote restart normal`

## Host-Side Selftests

- `bash new/verification/tests/run_steering_media_selftest.sh`
  - `steering_media_selftest passed`
- `bash new/verification/tests/run_scene_classifier_selftest.sh`
  - `scene_classifier_selftest passed`

## Steering-Media Evidence

### Capture 1

- Output directory:
  - `new/verification/bev-board-suspended-20260426T093920Z`
- Result summary:
  - assistant connected
  - steering-media connected
  - `board_steering_snapshots=96`
  - `steering_media_frames=74`
- Key confirmation:
  - `steering-media/frame_metadata.jsonl` contains BEV primary fields
    (`near_lateral_error`, `far_heading_error`, `preview_curvature`,
    `visible_range_m`, `reference_mode`)
  - nested `compatibility` object is present with
    `farthest_line` and `steering_reference_col`
  - `steering-media/config_snapshot.json` contains
    `BEV_PROJECTOR`, `BEV_GEOMETRY`, `BEV_SCENE_FSM`, `BEV_CONTROL_MODEL`

### Capture 2

- Output directory:
  - `new/verification/bev-board-suspended-20260426T094034Z`
- Result summary:
  - assistant connected
  - steering-media connected
  - `board_steering_snapshots=45`
  - `steering_media_frames=32`
  - `steering_media_alignment.alignment_count=32`
- Key confirmation:
  - `board_steering_snapshot.jsonl` now preserves nested
    `compatibility.valid`, `compatibility.farthest_line`,
    `compatibility.steering_reference_col`
  - `steering_media_alignment.jsonl` now carries aligned
    `farthest_line` and `steering_reference_col` values instead of `null`

### Capture 3

- Output directory:
  - `new/verification/bev-board-suspended-20260426T095330Z`
- Result summary:
  - assistant connected
  - steering-media connected
  - `board_steering_snapshots=28`
  - `steering_media_frames=24`
  - `steering_media_alignment.alignment_count=23`
- Key confirmation:
  - runtime was rebuilt after BEV reset-boundary cleanup and default
    `track_source=bev_projection` updates
  - first steering-media snapshot stayed BEV-first, with
    `active_module=bend`, `scene_phase=bend_veto`,
    `reference_mode=centerline`
  - compatibility payload remained nested and reported
    `compatibility.track_source=bev_projection`

### Capture 4

- Output directory:
  - `new/verification/bev-board-suspended-20260426T095841Z`
- Result summary:
  - assistant connected
  - steering-media connected
  - `board_steering_snapshots=17`
  - `steering_media_frames=10`
- Key confirmation:
  - runtime was rebuilt again after renaming residual
    `special_wide_*` runtime-owned debug traces to generic scene-debug fields
  - first steering-media snapshot stayed coherent, with
    `active_module=cross`, `scene_phase=cross_hold`,
    `reference_mode=hold_last`
  - compatibility payload remained nested and reported
    `compatibility.track_source=bev_projection`

### Capture 5

- Output directory:
  - `new/verification/bev-board-suspended-20260426T100541Z`
- Result summary:
  - assistant connected
  - steering-media connected
  - `board_steering_snapshots=17`
  - `steering_media_frames=13`
- Key confirmation:
  - runtime was rebuilt after removing top-level compatibility duplicates
    from `PerceptionResult` and steering snapshot carriers
  - the steering-media snapshot top level now contains only BEV-first
    primary fields plus scene/controller diagnostics
  - compatibility-only fields remain nested under `compatibility`, with
    `highest_line`, `farthest_line`, `steering_reference_col`,
    `track_seed_col`, `track_seed_score`, `track_sign`, and
    `track_source=bev_projection`

## Overlay Tooling

- `new/user/scene_overlay_probe.cpp` now renders a raw-image + sparse BEV
  dual-view overlay instead of old anchor-only drawings
- Host-built sample binary:
  - `new/verification/tests/scene_overlay_probe_host`
- Sample overlay artifact:
  - `new/verification/scene-overlay-probe-dualview.bmp`

## Runtime Smoke

- Command path:
  - `BOARD_IP=169.254.148.181 ./debug.sh remote stop`
  - `BOARD_IP=169.254.148.181 ./debug.sh smoke run`
- Smoke log:
  - `new/verification/runtime-smoke.log`
- Observed result:
  - camera, IMU, encoder, timer, and control loop initialized successfully
  - motor profile stayed disabled under degraded smoke mode
  - `control.steering_snapshot` emitted BEV primary fields plus nested compatibility fields
  - smoke run exited cleanly after harness termination

## Runtime Restoration

- After smoke, normal runtime restored with:
  - `BOARD_IP=169.254.148.181 ./debug.sh remote restart normal`
- Final status:
  - `runtime_status=running`

## Remaining Gaps

- No representative off-suspended track evidence for the required
  `straight / bend / circle / cross` run set yet
- OpenSpec checkpoint verifier bundles (`findings.json`,
  `verifier-evidence.json`, `agent-table.json`) were not produced in this pass
- Some legacy compatibility and quarantined old-scene source files still exist
  in-tree even though the formal runtime path is now BEV-first
