# Legacy Pixel Scene Archive

This directory preserves the removed pre-BEV steering scene implementation for reference only.

Archived material:
- `code/legacy/steering_bottom_tracker.*`: old bottom-connected pixel tracker.
- `code/legacy/steering_scene_common.*`: old `LaneMetrics` extraction and pixel-scene evidence helpers.
- `code/legacy/steering_scene_orchestrator.*` and `steering_scene_*.{cpp,hpp}`: old pixel-owned straight/bend/circle/cross/zebra/special-wide scene modules.
- `tests/steering_track_fusion_selftest.cpp` and `tests/run_steering_track_fusion_selftest.sh`: old bottom-tracker regression harness.

Deleted material:
- `steering_scene_roadblock_stub.*`: inert placeholder with no reference value.
- Active `new/verification/tests/steering_track_fusion_selftest` binary: stale generated artifact with no verification value.

Status:
- These files are not part of the runtime target.
- These files are not part of current verification tests.
- They must not be included from `new/code/` or used as fallback authority for steering decisions.
- The active implementation is the BEV pipeline in `new/code/legacy/steering_bev_*`, `steering_observation_assembly.*`, `steering_scene_fsm.*`, `steering_reference_policy.*`, and `steering_control_error_model.*`.
