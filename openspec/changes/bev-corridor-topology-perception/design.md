## Context

The active `new/` runtime already has a BEV-first steering foundation from `bev-steering-perception-refactor`, but the current ordinary geometry path still preserves an image-row scan authority:

- `new/code/legacy/steering_bev_geometry.cpp` projects each BEV forward sample to an image row, extracts white `RowRun`s on that row, picks a primary run, and projects that image run back into BEV.
- `new/code/legacy/steering_observation_assembly.cpp` derives scene evidence from width expansion/open-score style BEV summaries plus `bottom_transition_density` from a bottom image row.
- `new/code/legacy/steering_scene_fsm.cpp` consumes old observation booleans/scores and still allows `kBend` to become a formal active scene.
- `new/code/legacy/steering_reference_policy.cpp` already separates reference path generation from control, but it reads `BEVTrackEstimate` and old scene state instead of topology hypotheses.
- `new/code/legacy/steering_control_error_model.cpp` already owns pure-pursuit/curvature-command generation from a reference path and constraint set.

This change keeps the current coordinate contract for `BEVPoint.forward_m` and `BEVPoint.lateral_m`; it does not redefine left/right signs. The projector remains a coordinate mapper only. Geometry and topology produce facts only. The FSM owns candidate/confirm/progress/release state. Reference policy owns path choice. Control owns curvature-command generation and never branches directly on cross/circle/zebra/bend names.

### Reference Alignment Inventory

Target action: `adapt`.

Primary source references:

- `new/code/legacy/steering_bev_geometry.cpp`
- `new/code/legacy/steering_bev_geometry.hpp`
- `new/code/legacy/steering_bev_projector.cpp`
- `new/code/legacy/steering_bev_projector.hpp`
- `new/code/legacy/steering_observation_assembly.cpp`
- `new/code/legacy/steering_scene_fsm.cpp`
- `new/code/legacy/steering_reference_policy.cpp`
- `new/code/legacy/steering_control_error_model.cpp`
- `new/code/legacy/camera_logic.cpp`
- `new/code/port/control_types.hpp`
- `new/code/platform/param_store.cpp`
- `new/code/platform/steering_media_protocol.*`
- `new/code/platform/assistant_protocol.*`
- `new/code/runtime/control_debug_snapshot.*`
- `new/code/runtime/control_loop.cpp`
- `new/user/scene_overlay_probe.cpp`
- `new/user/steering_media_capture.py`
- `new/user/tune_steering.py`
- `new/verification/tests/scene_classifier_selftest.cpp`
- `new/verification/tests/authority_baseline_validate.cpp`

### Alignment Mapping

| Reference Module | Target Module | Action | Notes |
|---|---|---|---|
| `ComputeBevTrackEstimate` row-run extraction | `steering_bev_sparse_sampler` + `steering_corridor_intervals` + `steering_corridor_graph` | Adapt | Replace image-row run authority with metric BEV sparse samples, intervals, and graph-selected ordinary chain |
| `BEVSceneObservation` width/open/bottom-transition booleans | `RoadHypotheses` + `TopologyEvidence` | Adapt | Preserve useful derived debug ideas only after topology authority exists; do not let legacy scores drive FSM/reference/control |
| `UpdateSceneFsm(BEVSceneObservation, ...)` | `UpdateTopologySceneFsm(TopologyEvidence, ...)` | Adapt | Keep hysteretic state ownership but change formal decision input to topology evidence |
| `ResolveReferencePolicy(track, observation, scene, ...)` | `ResolveReferencePolicy(hypotheses, scene, prior, params)` | Adapt | Reference path source becomes hypotheses and topology FSM phase |
| `ComputeControlErrorModel` | Existing `ComputeControlErrorModel` | Reuse/adapt | Keep pure-pursuit/curvature command as the control owner; update inputs when reference/constraints become topology-derived |
| `control_debug_snapshot` and steering media protocol | Topology observability surface | Adapt | Replace old authority fields with topology source/scores/reference/curvature and overlay-reconstructable samples |

Coverage report:

- Covered: current row-run authority source, old scene evidence source, FSM/reference/control separation, debug/protocol/tooling surfaces.
- Partially covered: final on-board threshold values for cross/circle/zebra/lost evidence; these remain parameter-owned tuning values validated by motor-disabled and low-speed evidence.
- Intentionally not copied: old bottom tracker, old `LaneMetrics`, image-row `highest_line` semantics, `steering_reference_col`, `track_seed_col`, and `OrchestrateSteeringScenes` style scene ownership.

Unresolved alignment risks:

- If `BEVTrackEstimate` keeps calling an image-row run selector anywhere in the active runtime path, the change fails its no-double-truth boundary.
- If debug compatibility fields are kept and later read by FSM/reference/control, the topology contract regresses to a shadow-pixel authority.
- If invalid outside-image sparse samples are treated as openings, image FOV edges will create false cross/circle evidence.

## Goals / Non-Goals

**Goals:**

- Make sparse BEV samples, corridor intervals, corridor graph, hypotheses, and topology evidence the only formal steering perception authority.
- Generate `BEVTrackEstimate` from corridor topology, with `source="bev_corridor_topology"` or an equivalent explicit source.
- Distinguish invalid outside image, unknown low confidence, true background, and true drivable samples throughout the topology pipeline.
- Include ordinary, cross, left/right circle, zebra, and lost hypotheses/evidence in the formal chain.
- Treat bends as ordinary-corridor curvature, not as a formal special scene or motor branch.
- Preserve FSM/reference/control separation: FSM consumes evidence, reference consumes hypotheses plus FSM phase, control consumes reference path plus constraints.
- Update runtime parameters, config snapshot, protocols, tools, overlays, and verification helpers so topology evidence is reviewable frame by frame.

**Non-Goals:**

- Redefining `BEVPoint.forward_m/lateral_m` signs or changing projector responsibility.
- Reintroducing dense BEV images as runtime authority; dense BEV is debug-only.
- Preserving bottom tracker, `LaneMetrics`, `highest_line`, `farthest_line`, `steering_reference_col`, `track_seed_col`, or image-row run scans as formal authority.
- Creating scene-specific wheel/PWM/PID branches for cross, circle, zebra, bend, or lost.
- Completing high-speed powered board tuning in this change before motor-disabled topology/sign evidence is accepted.

## Decisions

### Decision: Sparse Metric BEV Sampling Becomes The First Runtime Authority

Problem:
The current ordinary path claims BEV output but obtains it by scanning image rows. That makes the image row a hidden authority and makes invalid FOV edges indistinguishable from real road openings.

Alternatives considered:

- Option A: Keep projecting BEV forward samples to image rows and harden run picking.
  - Strength: Small code change.
  - Weakness: Preserves row-scan authority and keeps invalid/opening ambiguity.
  - Verification impact: Weak, because failures still require inspecting image-row heuristics.
- Option B: Generate a dense BEV bitmap and derive all geometry from it.
  - Strength: Easy to visualize.
  - Weakness: Higher runtime cost and risks making the debug bitmap a second truth.
  - Verification impact: Medium.
- Option C: Sample a metric sparse BEV lattice directly through `BEVProjector` and classify every sample.
  - Strength: Runtime work matches control needs, invalid handling is explicit, and debug overlays can reconstruct the exact authority.
  - Weakness: Requires new typed contracts and more fixture tests.
  - Verification impact: Strong.

Chosen approach: Option C.

Stack Equivalent:

- Current `RowRun` -> `BEVSample` grouped by forward layer.
- Image threshold comparison -> per-sample `raw_intensity`, `confidence`, `BEVSampleClass`.
- Current `ProjectVehicleToImage` use -> sampler-only projection; projector still has no scene/FSM/PID dependency.

Named Deliverables:

- `new/code/port/control_types.hpp`
- `new/code/legacy/steering_bev_sparse_sampler.hpp`
- `new/code/legacy/steering_bev_sparse_sampler.cpp`
- `new/verification/tests/run_bev_sparse_sampler_test.sh`
- `new/config/default_params.json`
- `new/code/platform/param_store.cpp`

Failure Semantics:

- A sample projected outside the image is `kInvalidOutsideImage`, never background and never opening.
- A sample with insufficient confidence is `kUnknownLowConfidence`; it lowers confidence and may shorten visible range but cannot create a fake opening.
- Projector invalidity sets fail-safe/geometry veto through constraints; it does not fall back to image-row authority.

Boundary Examples:

- `forward_m` samples come from `BEV_TOPOLOGY_SAMPLER.FORWARD_SAMPLES_M`.
- `lateral_m` search range comes from `LATERAL_MIN_M`, `LATERAL_MAX_M`, and `LATERAL_STEP_M`.
- `SAMPLE_PATCH_RADIUS_PX` affects sample confidence only; it must not infer scene semantics.

Contrast Structure:

- Adopt: metric sparse BEV lattice as runtime authority.
- Reject: image-row run scans or dense BEV bitmap authority.

Verification Hook:

- Host tests validate projection valid domain, left/right sign consistency, 4.5 m coverage, invalid handling, and low-confidence classification.
- Board motor-disabled media evidence must show sparse sample colors on raw frame and BEV view before low-speed powered runs.

### Decision: Corridor Intervals Own Layer Geometry And Preserve Invalid/Unknown Semantics

Problem:
Cross and circle evidence depend on openings, but image borders and low-confidence gaps can look like openings unless the extraction step keeps invalid, unknown, background, and drivable classes separate.

Alternatives considered:

- Option A: Convert sparse samples to binary drivable/background and infer edges from gaps.
  - Strength: Simple.
  - Weakness: FOV invalid and unknown gaps become false openings.
  - Verification impact: Weak.
- Option B: Extract `CorridorInterval`s from classified samples and carry edge validity, opening scores, valid-sample ratio, and confidence.
  - Strength: Explicit semantics and direct topology input.
  - Weakness: More typed data and tests.
  - Verification impact: Strong.

Chosen approach: Option B.

Stack Equivalent:

- Current `run.left/run.right/run.center/run.width` -> `CorridorInterval` in meters.
- Border truncation checks -> `left_edge_valid` / `right_edge_valid` and `invalid_edge_penalty`.
- Width expansion debug -> derived from intervals only after invalid/unknown filtering.

Named Deliverables:

- `new/code/legacy/steering_corridor_intervals.hpp`
- `new/code/legacy/steering_corridor_intervals.cpp`
- `new/verification/tests/run_corridor_interval_test.sh`

Failure Semantics:

- Invalid outside-image samples adjacent to a drivable interval do not increase opening score.
- Unknown low-confidence samples reduce interval confidence and valid-sample ratio.
- Width outside accepted min/max remains available for debug but is downweighted or rejected for ordinary graph selection.

Boundary Examples:

- Single corridor, multi-interval, single-side opening, bilateral opening, invalid border, and unknown patches each have explicit fixture cases.

Contrast Structure:

- Adopt: intervals with edge validity and confidence.
- Reject: binary run extraction where all non-drivable gaps mean opening.

Verification Hook:

- `run_corridor_interval_test.sh` must prove invalid edges do not produce false openings and unknown regions lower confidence.

### Decision: Corridor Graph Builds Ordinary Geometry And Replaces Row-Run `BEVTrackEstimate`

Problem:
The control path still needs a stable ordinary path. A per-layer best interval is not enough because temporary missing edges, branch intervals, and width changes can make the path jump.

Alternatives considered:

- Option A: Pick the widest or nearest-center interval independently per forward layer.
  - Strength: Small implementation.
  - Weakness: Jumps between branches and unstable through partial loss.
  - Verification impact: Medium.
- Option B: Build a local interval DAG and use DP costs for overlap, center jump, width change, curvature, and prior carry.
  - Strength: Stable ordinary chain without a complex global graph search.
  - Weakness: Requires tuning graph costs and tests.
  - Verification impact: Strong.

Chosen approach: Option B.

Stack Equivalent:

- `previous_run_ptr` and prior reference column -> graph prior carry with `PRIOR_CARRY_CONFIDENCE_SCALE`.
- Row-run continuity check -> edge `center_jump_cost`, `width_change_cost`, and `curvature_cost`.
- `sampled_centerline` -> ordinary `PathCandidate.sampled_path`.

Named Deliverables:

- `new/code/legacy/steering_corridor_graph.hpp`
- `new/code/legacy/steering_corridor_graph.cpp`
- `new/verification/tests/run_corridor_graph_test.sh`
- active `ComputeBevTrackEstimate` cutover or replacement builder that sets `source="bev_corridor_topology"`.

Failure Semantics:

- If no ordinary chain is valid, the track becomes low-confidence/lost and degrades through constraints or short lost prediction.
- A prior carry can downweight ambiguity but cannot invent high-confidence geometry.
- Image-row run extraction is removed from the active authority path; any surviving code is debug/offline only.

Boundary Examples:

- Straight ordinary corridor, curved ordinary corridor, single-side short loss, bottom loss, image edge truncation, branch interval, and width jump fixtures.

Contrast Structure:

- Adopt: local DAG/DP ordinary chain.
- Reject: row-run authority, per-layer greedy center selection, or bottom-tracker fallback truth.

Verification Hook:

- `run_corridor_graph_test.sh` plus `authority_baseline_validate` must prove straight/bend continuity, no screen-edge sticking, no old authority fields, and the new track source.

### Decision: Topology Hypotheses And Evidence Are Facts, Not Control Commands

Problem:
Cross, circle, zebra, and lost behavior need scene-level evidence, but if hypotheses directly select control behavior, scene semantics will leak into motor/PID branches.

Alternatives considered:

- Option A: Keep `BEVSceneObservation` booleans and add more scores.
  - Strength: Smaller type change.
  - Weakness: Keeps legacy `width_expand_ratio/open_score/bottom_transition_density` as formal decision language.
  - Verification impact: Weak.
- Option B: Generate `RoadHypotheses` and `TopologyEvidence` from intervals/graph, then let FSM/reference/control consume only their owned inputs.
  - Strength: Typed responsibilities and testable ownership.
  - Weakness: More module boundaries.
  - Verification impact: Strong.

Chosen approach: Option B.

Stack Equivalent:

- Current `cross_candidate/circle_*_candidate/zebra_candidate` -> accumulated topology scores.
- Current bend scene -> `ordinary.curvature` plus `bend_veto_score`, not formal active scene.
- Current circle reference offset -> arc/branch/exit hypotheses consumed by reference policy.

Named Deliverables:

- `new/code/legacy/steering_topology_hypotheses.hpp`
- `new/code/legacy/steering_topology_hypotheses.cpp`
- `new/code/legacy/steering_topology_evidence.hpp`
- `new/code/legacy/steering_topology_evidence.cpp`

Failure Semantics:

- Legacy heuristics may be emitted as debug derivatives only if labelled non-authoritative.
- Cross requires bilateral opening sync plus forward reacquire and arc veto.
- Circle direction is scored separately and latched only by FSM after confirmation.
- Zebra evidence must be BEV-local and cannot directly modify motor output.

Boundary Examples:

- `ordinary_score`, `cross_score`, `left_circle_score`, `right_circle_score`, `zebra_score`, `lost_score`, `bilateral_opening_sync`, `forward_reacquire_score`, `invalid_edge_penalty`.

Contrast Structure:

- Adopt: topology evidence facts with scores.
- Reject: scene booleans produced directly from old observation fields.

Verification Hook:

- `scene_classifier_selftest` must cover bend-not-special, big-bend circle veto, cross bilateral confirmation, circle direction latch, zebra hold through FSM/reference/constraints, and scene-not-motor ownership.

### Decision: FSM, Reference Policy, And Control Remain Strictly Layered

Problem:
Scene changes are allowed to alter reference paths and constraints, but not to bypass the curvature-control model or wheel/PWM terminal chain.

Alternatives considered:

- Option A: Let scene modules output direct control modifiers.
  - Strength: Fast to tune one scene.
  - Weakness: Recreates special-case motor branches.
  - Verification impact: Weak.
- Option B: FSM consumes evidence, reference policy consumes hypotheses plus FSM phase, control consumes reference path and constraints.
  - Strength: Maintains the BEV-first control contract and future extensibility.
  - Weakness: Requires richer reference modes and memory.
  - Verification impact: Strong.

Chosen approach: Option B.

Stack Equivalent:

- `SpecialSceneFsmState` -> topology FSM state and phase with candidate/confirm/progress/release.
- `ReferencePolicyState` -> hold-last, blend, boundary offset, arc follow, lost prediction memory.
- `ControlErrorModelInput` -> existing track/reference/vehicle/constraints entry.

Named Deliverables:

- `new/code/legacy/steering_topology_scene_fsm.hpp`
- `new/code/legacy/steering_topology_scene_fsm.cpp`
- updated `new/code/legacy/steering_reference_policy.*`
- updated `new/code/legacy/steering_control_error_model.*` only where needed for confidence/lookahead/constraint inputs.
- updated `new/code/runtime/runtime_state.hpp` or equivalent runtime-owned state surfaces.

Failure Semantics:

- Bend must not become a formal special active scene; ordinary curvature can affect lookahead/speed/constraints and veto cross/circle.
- Cross phase progression is `cross_approach -> cross_hold -> cross_reacquire -> release`.
- Circle phase progression is `circle_entry -> circle_interior -> circle_exit -> release`, with direction latched after confirmation.
- Lost allows short `LOST_PREDICTION`; after hold cycles it suppresses steering or fail-safe gates.
- Wheel target/PWM behavior changes only through `curvature_command`, reference path, constraints, or existing actuator safety gates.

Boundary Examples:

- Cross hold uses `HOLD_LAST` or entrance heading extension.
- Circle interior uses `STABLE_BOUNDARY_OFFSET` or `ARC_FOLLOW`.
- Circle exit uses `BLEND_TO_EXIT`.
- Zebra hold uses reference/constraint behavior only.

Contrast Structure:

- Adopt: layered FSM -> reference -> curvature control.
- Reject: scene-specific motor/PID branches.

Verification Hook:

- `run_scene_classifier_selftest.sh` and `run_legacy_pid_control_cycle_test.sh` must prove scene names do not directly change wheel/PWM output.

### Decision: Parameters, Debug, Protocols, And Tooling Are Part Of The Authority Cutover

Problem:
If topology thresholds or debug fields remain hardcoded or old-authority named, board tuning and evidence review will drift away from the runtime contract.

Alternatives considered:

- Option A: Implement perception first and retrofit parameters/tooling later.
  - Strength: Less initial work.
  - Weakness: Hidden constants and stale evidence make board validation unreliable.
  - Verification impact: Weak.
- Option B: Add parameter groups, config snapshot fields, protocol fields, overlay, and baseline validation with the topology modules.
  - Strength: Source, runtime config, and evidence all describe the same authority.
  - Weakness: Larger change surface.
  - Verification impact: Strong.

Chosen approach: Option B.

Stack Equivalent:

- `BEVGeometryParameters` -> existing ordinary geometry tuning plus new topology sampler/graph/evidence/reference groups.
- Current snapshot `scene_width_expand_ratio` style fields -> topology source/scores/phase/reference/curvature fields.
- Current overlay of sampled path -> raw + sparse samples + intervals + hypotheses + final reference + pure pursuit target.

Named Deliverables:

- `new/config/default_params.json`
- `new/code/platform/param_store.cpp`
- `new/code/runtime/control_debug_snapshot.*`
- `new/code/platform/steering_media_protocol.*`
- `new/code/platform/assistant_protocol.*`
- `new/user/steering_media_capture.py`
- `new/user/tune_steering.py`
- `new/user/scene_overlay_probe.cpp`
- `new/verification/tests/run_authority_baseline_validate.sh`
- `new/verification/tests/run_steering_media_selftest.sh`

Failure Semantics:

- Runtime tuning values must load from parameters, accepted calibration artifacts, or documented compile-time invariants.
- Removed old authority names must not appear in formal runtime outputs or active runtime decision reads.
- Dense BEV artifacts may be rendered for debug but must identify their sparse topology source.

Boundary Examples:

- Required per-frame debug: `topology_source`, `ordinary_score`, `cross_score`, `left_circle_score`, `right_circle_score`, `zebra_score`, `lost_score`, `evidence_accumulators`, `scene_phase`, `reference_mode`, `reference_confidence`, `ordinary_curvature`, `lookahead_distance_m`, `curvature_command`, `visible_range_m`, `track_confidence`.

Contrast Structure:

- Adopt: parameter-owned topology evidence and source-owned overlays.
- Reject: stale compatibility authority fields or unowned scene thresholds.

Verification Hook:

- `run_runtime_params_load_test.sh`, `run_steering_media_selftest.sh`, `run_authority_baseline_validate.sh`, `git diff --check -- new/code new/user new/config new/verification/tests`, and motor-disabled board evidence.

## Independent Verification Plan (STANDARD/STRICT)

Verification uses shared sequence `verify-sequence/default` from `openspec/schemas/ai-enforced-workflow/verification-sequence.md` and shared verification-cycle contracts:

- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`

Runtime profile policy:

- Use `.codex/agents/verify-reviewer.toml`.
- Invocation template id: `verify-reviewer-inline-v3`.
- Repository-level `.index/` material is optional background only and has no closure, verdict, repair-routing, or spawn authority.

Loop rule:

- An `active` verifier that reports `block` remains authoritative until the same verifier returns `pass`.
- `agent-table.json` is current-state-only.
- A valid pass requires complete scope coverage and exhaustive review coverage per the shared contracts.
- Only the main orchestrator may authorize resume, spawn, repair, or termination.

### Review Checkpoints

- Artifact-completion docs-first review:
  - Primary surfaces: `proposal.md`, `design.md`, `specs/**/*.md`, `tasks.md`.
  - Findings JSON: `openspec/changes/bev-corridor-topology-perception/verification/artifact-completion/attempt-<n>/findings.json`.
  - Verifier evidence JSON: `openspec/changes/bev-corridor-topology-perception/verification/artifact-completion/attempt-<n>/verifier-evidence.json`.
  - Agent table: `openspec/changes/bev-corridor-topology-perception/verification/artifact-completion/agent-table.json`.
  - Continuation target on pass: implementation entry.

- Checkpoint 1 source-first review:
  - Primary surfaces: typed contracts, topology runtime state, parameter loading, config snapshot exposure, CMake module/helper-target wiring, and runtime-parameter tests.
  - Findings JSON: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-1/attempt-<n>/findings.json`.
  - Verifier evidence JSON: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-1/attempt-<n>/verifier-evidence.json`.
  - Agent table: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-1/agent-table.json`.

- Checkpoint 2 source-first review:
  - Primary surfaces: sparse sampler, sample classification, corridor interval extraction, sampler/interval debug-only wiring, and sampler/interval tests.
  - Findings JSON: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-2/attempt-<n>/findings.json`.
  - Verifier evidence JSON: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-2/attempt-<n>/verifier-evidence.json`.
  - Agent table: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-2/agent-table.json`.

- Checkpoint 3 source-first review:
  - Primary surfaces: corridor graph ordinary cutover, `BEVTrackEstimate` source, removal of image-row run authority from active runtime, graph tests, authority search evidence, and straight/bend baseline cases.
  - Findings JSON: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-3/attempt-<n>/findings.json`.
  - Verifier evidence JSON: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-3/attempt-<n>/verifier-evidence.json`.
  - Agent table: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-3/agent-table.json`.

- Checkpoint 4 source-first review:
  - Primary surfaces: hypotheses, topology evidence, topology FSM, reference policy, typed observation assembly, control separation, lost/zebra/cross/circle behavior, scene classifier tests, and PID/control-cycle tests.
  - Findings JSON: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-4/attempt-<n>/findings.json`.
  - Verifier evidence JSON: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-4/attempt-<n>/verifier-evidence.json`.
  - Agent table: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-4/agent-table.json`.

- Checkpoint 5 source-first review:
  - Primary surfaces: runtime `AnalyzeFrame` chain, control debug snapshot, media and assistant protocols, steering media service, host capture/tuning/overlay tooling, authority baselines, required host tests, runtime build, motor-disabled board evidence, low-speed board evidence when available, and final `openspec-verify-change` closure.
  - Findings JSON: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-5/attempt-<n>/findings.json`.
  - Verifier evidence JSON: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-5/attempt-<n>/verifier-evidence.json`.
  - Agent table: `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-5/agent-table.json`.

## Migration Plan

1. Wave 0 creates and verifies the OpenSpec artifacts for `bev-corridor-topology-perception`, explicitly depending on the active `bev-steering-perception-refactor` foundation.
2. Wave 1 adds sparse sampler and interval extraction, wires them into debug/selftests only, and validates coordinate signs, invalid handling, low-confidence handling, and 4.5 m coverage.
3. Wave 2 cuts ordinary geometry over to the corridor graph, builds `BEVTrackEstimate` from the ordinary chain, and stabilizes straight, bend, edge-loss, and image-edge behavior before topology scenes drive runtime decisions.
4. Wave 3 adds topology hypotheses/evidence for ordinary/cross/circle/zebra/lost and removes old observation booleans/scores from formal FSM/reference decisions.
5. Wave 4 replaces formal FSM input with topology evidence and updates reference policy for hold, boundary offset, arc follow, blend to exit, lost prediction, and bend-as-ordinary behavior.
6. Wave 5 updates protocols, tooling, overlays, baselines, and board evidence. Board validation starts motor-disabled, verifies topology fields and signs, then proceeds to low-speed powered runs.

Rollback strategy:

- Before Wave 2, sampler/interval code is debug-only and can be disabled without changing control.
- After Wave 2, rollback means reverting to the previous BEV-first row-run implementation as a code rollback, not a runtime dual-truth fallback.
- No runtime fallback to bottom tracker or image-row run authority is allowed inside the accepted implementation.

## Open Questions

- No blocking architectural questions remain. The exact on-board thresholds for cross/circle/zebra/lost scores are expected to be tuned through the new runtime parameter surface and accepted evidence, not by changing source constants.
- The `runtime_params_load_test` may compile but not execute on hosts without `qemu-loongarch64`; this is an acceptable documented residual risk when evidence states that limitation.

## Risks / Trade-offs

- Sparse sampling can miss very thin or highly textured markings if patch/confidence parameters are poor. Mitigation: patch-radius, confidence thresholds, and overlay evidence are parameter-owned and fixture-tested.
- Graph DP costs can over-smooth branch transitions. Mitigation: branch hypotheses are generated from retained intervals before ordinary-chain pruning, and cross/circle evidence reads the interval set, not only the ordinary chain.
- Zebra evidence can regress to image-row authority if implemented from bottom transitions. Mitigation: zebra transitions must be projected into BEV-local evidence and can only affect behavior through FSM/reference/constraints.
- Removing old authority fields from formal outputs can break scripts. Mitigation: update `tune_steering.py`, media capture, and authority baselines in the same change; any retained legacy field must be labelled debug/offline-only.
