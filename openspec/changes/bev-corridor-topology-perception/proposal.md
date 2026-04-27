## Why

The current BEV-first steering chain still builds `BEVTrackEstimate` by projecting BEV forward samples back to image rows, scanning white image runs, and projecting those runs back into BEV. That removes the old pixel authority on paper, but it preserves a row-scan shadow authority that can still compete with BEV geometry during ordinary tracking, cross/circle/zebra classification, reference selection, and debug review.

This change upgrades steering perception to a single BEV corridor-topology authority: raw frame -> metric sparse BEV sampling -> corridor intervals -> corridor graph -> topology hypotheses/evidence -> hysteretic scene FSM -> reference policy -> curvature control.

## What Changes

- Add typed BEV sparse-sampling and topology contracts in `new/code/port/control_types.hpp`, including `BEVSampleClass`, `BEVSample`, `CorridorInterval`, `CorridorGraphEdge`, `PathCandidate`, `RoadHypotheses`, and `TopologyEvidence`.
- Add runtime parameter ownership for `BEV_TOPOLOGY_SAMPLER`, `BEV_CORRIDOR_GRAPH`, `BEV_TOPOLOGY_EVIDENCE`, and `BEV_REFERENCE_POLICY`, with values loaded through `RuntimeParameters`, `default_params.json`, `param_store`, and `config_snapshot`.
- Add `new/code/legacy/steering_bev_sparse_sampler.*` so runtime authority samples a metric sparse BEV lattice directly from raw frame and threshold images through `BEVProjector`, classifying each sample as invalid outside image, unknown low confidence, background, or drivable.
- Add `new/code/legacy/steering_corridor_intervals.*` so each forward layer becomes explicit corridor intervals while preserving the distinction between invalid image projection, unknown low confidence, true background, and true drivable samples.
- Add `new/code/legacy/steering_corridor_graph.*` so ordinary geometry is selected from a local BEV interval DAG/DP, replacing image-row run extraction as the source of `BEVTrackEstimate`.
- Add `new/code/legacy/steering_topology_hypotheses.*` and `steering_topology_evidence.*` so ordinary, left/right arc, forward-exit, branch, zebra, lost, bend-veto, and opening evidence are produced from corridor topology rather than legacy `width_expand_ratio`, `open_score`, `bottom_transition_density`, or bottom tracker fields.
- Replace the formal decision source of `steering_scene_fsm.*` with topology evidence, preserving candidate/confirm/progress/release hysteresis while making bend an ordinary-corridor curvature property rather than a formal special scene.
- Update `steering_reference_policy.*` so reference paths are selected from `RoadHypotheses` and topology FSM phase, including hold-last, entry-heading extension, stable-boundary offset, arc follow, blend-to-exit, and lost prediction.
- Keep `steering_control_error_model.*` as the curvature-command owner and ensure scene names do not directly select motor, PWM, wheel target, or PID branches.
- Demote legacy row-scan, bottom-tracker, `LaneMetrics`, `highest_line`, `farthest_line`, `steering_reference_col`, `track_seed_col`, `track_source`, `width_expand_ratio`, `open_score`, and `bottom_transition_density` style heuristics to removed, debug-only, or offline-comparison surfaces with no runtime authority.
- Update `control_debug_snapshot`, `steering_media_protocol`, `assistant_protocol`, `steering_media_capture.py`, `tune_steering.py`, `scene_overlay_probe.cpp`, and related selftests so every frame can reconstruct raw-image plus BEV-topology evidence and no official output depends on removed compatibility authority fields.

## Capabilities

### New Capabilities

- `bev-corridor-topology-perception`: Defines sparse BEV sample classification, corridor interval extraction, corridor graph selection, path hypotheses, topology evidence, and the rule that `BEVTrackEstimate` must come from corridor topology rather than image-row run authority.
- `bev-topology-scene-reference-control`: Defines topology-evidence scene FSM semantics, topology-driven reference policy modes, bend-as-ordinary behavior, cross/circle/zebra/lost progression, and the strict boundary that control consumes reference path, constraints, and `curvature_command` without scene-specific motor branches.

### Modified Capabilities

- `steering-tuning-media-observability`: Steering snapshots, assistant protocol, media capture, overlay probes, and authority baseline validation must expose topology source, topology scores, evidence accumulators, reference mode/confidence, corridor samples/intervals/graph, final reference path, pure pursuit target, and curvature command while removing old authority fields from formal runtime output.

## Risk Tier

- `STRICT`: This change replaces the runtime authority used to construct `BEVTrackEstimate`, modifies scene/FSM and reference-policy decision sources, changes runtime parameter ownership, changes debug/protocol/tooling outputs, and directly affects the camera-to-curvature control chain that feeds wheel target mixing and PWM. It also depends on the active `bev-steering-perception-refactor` BEV-first foundation and must preserve its no-double-truth boundary.

## Impact

- Affected layers: `port` typed contracts and runtime parameters, `legacy` steering perception/FSM/reference/control modules, `runtime` perception/control state progression and debug snapshots, `platform` parameter loading and media/assistant protocols, `config` default parameter JSON, `new/user` steering tooling and overlays, and `new/verification/tests` selftests/baselines.
- Primary source impact: `new/code/port/control_types.hpp`, `new/code/legacy/camera_logic.*`, `new/code/legacy/steering_bev_projector.*`, new BEV topology modules, `new/code/legacy/steering_scene_fsm.*`, `new/code/legacy/steering_reference_policy.*`, `new/code/legacy/steering_control_error_model.*`, `new/code/runtime/control_debug_snapshot.*`, `new/code/platform/param_store.cpp`, `new/code/platform/steering_media_protocol.*`, `new/code/platform/assistant_protocol.*`, `new/user/CMakeLists.txt`, `new/user/steering_media_capture.py`, `new/user/tune_steering.py`, and `new/user/scene_overlay_probe.cpp`.
- Verification impact: add sparse sampler, corridor interval, corridor graph, topology scene, PID/control-cycle, authority baseline, runtime parameter, and steering-media tests; runtime target must build; board validation must begin motor-disabled and only proceed to low-speed powered runs after topology fields and sign conventions are verified.
- Dependency: this change builds on the current `bev-steering-perception-refactor` BEV-first foundation and narrows it by removing the remaining image-row run authority from the active steering path.
