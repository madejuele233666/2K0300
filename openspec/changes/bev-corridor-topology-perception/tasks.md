## 0. Verification Contract

- Shared sequence:
  - `openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`
- Shared JSON verification contract:
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`
- Shared field groups:
  - `invocation_common_required`
  - `output_paths_required`
  - `verifier_evidence_required`
  - `valid_pass_requirements`
  - `partial_scope_rule`
  - `subject_required_any_of`
  - `findings_required`
  - `finding_object_required`
  - `finding_semantics`
  - `repair_routing_rules`
- Routing target for blocking findings:
  - `openspec-repair-change`
- Expected skills:
  - `openspec-align`
  - `openspec-architect`
  - `openspec-artifact-verify`
  - `openspec-repair-change`
  - `openspec-verify-change`
- Supported continuation overrides:
  - `verify-only`
  - `dry-run`
  - `manual_pause`
- Artifact-completion gate ownership:
  - when this task list completes the schema's `applyRequires` set under `ai-enforced-workflow`, the active artifact-creation caller (`openspec-propose` or `openspec-continue-change`) runs docs-first review before implementation entry
  - `openspec-apply-change` does not own that docs-first artifact gate
- Agent lifecycle:
  - follow `cycle_rules` in `verification-cycle-core-v1.json`
  - continue a usable `active` verifier first
  - prefer `send_input` while the same active agent is still open
  - `send_input` returning `agent not found` routes to `resume`, not immediate spawn
  - use `continuation_probe` to distinguish resume from recovery spawn
  - spawn a new verifier only when no usable active verifier exists
  - `no_usable_active_agent` means `agent-table.json` literally contains no usable `active` agent
  - recovery spawn reason codes must distinguish `active_agent_missing` and `active_agent_not_resumable`
  - only `block -> pass` marks an agent `non_active`
  - final termination requires a valid active pass with complete/exhaustive coverage

## 1. Artifact Gate And Scope Lock

- [x] 1.1 Run `openspec-artifact-verify` for `bev-corridor-topology-perception` docs-first scope, covering `proposal.md`, `design.md`, `specs/**/*.md`, and `tasks.md`, and route blocking findings to `openspec-repair-change`.
- [x] 1.2 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for artifact-completion docs-first readiness. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `openspec/changes/bev-corridor-topology-perception/verification/artifact-completion/attempt-<n>/findings.json`, verifier evidence JSON at `openspec/changes/bev-corridor-topology-perception/verification/artifact-completion/attempt-<n>/verifier-evidence.json`, and caller/orchestrator-maintained `agent-table.json` at `openspec/changes/bev-corridor-topology-perception/verification/artifact-completion/agent-table.json`. Require fields from `verifier_evidence_required`, bind subject through `subject_required_any_of`, enforce `valid_pass_requirements`, require explicit `scope` for partial verification, and route blocking findings through `findings_required / finding_object_required / finding_semantics / repair_routing_rules` to `openspec-repair-change`.

## 2. Typed Contracts, Parameters, And Build Wiring

- [x] 2.1 Add `BEVSampleClass`, `BEVSample`, `CorridorInterval`, `CorridorGraphEdge`, `PathCandidate`, `RoadHypotheses`, and `TopologyEvidence` to `new/code/port/control_types.hpp` without adding vendor/OpenCV types to public port contracts.
- [x] 2.2 Add runtime state fields for topology memories, evidence accumulators, reference-policy memory, and lost prediction in `new/code/port/control_types.hpp` or the existing runtime-owned steering state boundary.
- [x] 2.3 Add `BEV_TOPOLOGY_SAMPLER`, `BEV_CORRIDOR_GRAPH`, `BEV_TOPOLOGY_EVIDENCE`, and `BEV_REFERENCE_POLICY` defaults to `new/config/default_params.json` and update `new/config/default_params.md`.
- [x] 2.4 Update `new/code/platform/param_store.cpp` so the new parameter groups load into `RuntimeParameters`, preserve fallback defaults, and appear in `config_snapshot`.
- [x] 2.5 Add CMake wiring in `new/user/CMakeLists.txt` for the new runtime modules and verification helper targets without removing existing BEV-first helper targets.
- [x] 2.6 Add or update `run_runtime_params_load_test.sh` coverage for the new topology parameter groups; document the acceptable residual risk when hosts lack `qemu-loongarch64`.
- [ ] 2.7 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for typed contracts, parameter ownership, config snapshot exposure, and CMake wiring. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-1/attempt-<n>/findings.json`, verifier evidence JSON at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-1/attempt-<n>/verifier-evidence.json`, and caller/orchestrator-maintained `agent-table.json` at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-1/agent-table.json`. Caller-local guardrails: port contracts remain hardware-free, topology thresholds are runtime-owned, no undocumented control-affecting magic constants are introduced, and blocking findings route to `openspec-repair-change`.

## 3. Sparse Sampler And Corridor Intervals

- [x] 3.1 Implement `new/code/legacy/steering_bev_sparse_sampler.hpp` and `new/code/legacy/steering_bev_sparse_sampler.cpp` so raw frame, threshold, `BEVProjector`, and runtime parameters produce forward-layered sparse BEV samples.
- [x] 3.2 Ensure sampler classification distinguishes `kInvalidOutsideImage`, `kUnknownLowConfidence`, `kBackground`, and `kDrivable`, including patch-radius confidence and valid-image-projection reporting.
- [x] 3.3 Implement `new/code/legacy/steering_corridor_intervals.hpp` and `new/code/legacy/steering_corridor_intervals.cpp` so samples become `CorridorInterval` lists with edge validity, opening scores, valid-sample ratio, and confidence.
- [x] 3.4 Add `new/verification/tests/run_bev_sparse_sampler_test.sh` and its C++ helper covering projection valid domain, 4.5 m coverage, lateral sign consistency, invalid handling, and low-confidence-to-unknown behavior.
- [x] 3.5 Add `new/verification/tests/run_corridor_interval_test.sh` and its C++ helper covering single corridor, multi-interval, single-side opening, bilateral opening, invalid-edge no-opening, and unknown confidence degradation.
- [x] 3.6 Wire sampler and intervals into debug/selftest surfaces only; do not change control authority before graph ordinary cutover.
- [ ] 3.7 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for sampler and interval extraction. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-2/attempt-<n>/findings.json`, verifier evidence JSON at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-2/attempt-<n>/verifier-evidence.json`, and caller/orchestrator-maintained `agent-table.json` at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-2/agent-table.json`. Caller-local guardrails: invalid outside image is never opening, unknown low confidence is not background, projector stays scene/control-free, and blocking findings route to `openspec-repair-change`.

## 4. Corridor Graph And Ordinary Authority Cutover

- [x] 4.1 Implement `new/code/legacy/steering_corridor_graph.hpp` and `new/code/legacy/steering_corridor_graph.cpp` using local DAG/DP scoring for overlap, center jump, width change, curvature, interval confidence, and prior carry.
- [x] 4.2 Build the ordinary `PathCandidate` from the selected graph chain, including sampled path, mean width, width stability, curvature, curvature consistency, start/end forward range, and confidence.
- [x] 4.3 Replace active `ComputeBevTrackEstimate` row-run authority with corridor-topology-derived construction and set `BEVTrackEstimate.source` to `bev_corridor_topology` or an equivalent explicit topology source.
- [x] 4.4 Preserve existing BEV projector calibration and coordinate semantics while removing active `ExtractRuns`, `PickPrimaryRun`, image-row center-reference, and row-scan fallback authority from runtime control decisions.
- [x] 4.5 Add `new/verification/tests/run_corridor_graph_test.sh` and its helper covering straight ordinary chain, curved ordinary chain, short single-side loss, bottom loss, image-edge stability, branch ambiguity, and width-jump downweighting.
- [x] 4.6 Update `new/verification/tests/run_authority_baseline_validate.sh` and baseline helper coverage for straight and bend cases so formal outputs show topology source and no old compatibility authority fields.
- [x] 4.7 Run `rg -n "highest_line|farthest_line|steering_reference_col|LaneMetrics|steering_bottom_tracker|OrchestrateSteeringScenes" new/code new/user new/verification/tests` and classify any remaining matches as removed, debug-only, offline-only, or failing.
- [ ] 4.8 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for corridor graph ordinary cutover and no-row-run authority. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-3/attempt-<n>/findings.json`, verifier evidence JSON at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-3/attempt-<n>/verifier-evidence.json`, and caller/orchestrator-maintained `agent-table.json` at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-3/agent-table.json`. Caller-local guardrails: `BEVTrackEstimate` comes from corridor topology, active runtime does not call image-row run authority, bends remain ordinary curvature, and blocking findings route to `openspec-repair-change`.

## 5. Topology Hypotheses, Evidence, FSM, And Reference Policy

- [x] 5.1 Implement `new/code/legacy/steering_topology_hypotheses.hpp` and `new/code/legacy/steering_topology_hypotheses.cpp` to generate ordinary, left/right arc, forward exit, left/right branch, and zebra-hold hypotheses from graph/interval facts and prior memory.
- [x] 5.2 Add `new/code/legacy/steering_topology_evidence.hpp` and the ordinary/bend/lost score scaffolding in `steering_topology_evidence.cpp`, including score accumulator state and invalid-edge penalty inputs.
- [x] 5.3 Implement cross evidence scoring in `steering_topology_evidence.cpp`, covering bilateral opening sync, short-range interval expansion, forward ordinary reacquire, and arc veto.
- [x] 5.4 Implement left/right circle evidence scoring in `steering_topology_evidence.cpp`, covering single-side opening, opposite boundary continuity, same-sign curvature, arc/inner-offset hypothesis, and exit corridor appearance.
- [x] 5.5 Implement zebra evidence scoring in `steering_topology_evidence.cpp`, using only BEV-local projected or aggregated transition evidence and no bottom-row authority.
- [x] 5.6 Replace formal scene decision input in `new/code/legacy/steering_scene_fsm.*` or new `steering_topology_scene_fsm.*` so FSM reads `TopologyEvidence` instead of old observation booleans and scores.
- [x] 5.7 Update cross FSM phase semantics and tests for `cross_approach`, `cross_hold`, `cross_reacquire`, and release.
- [x] 5.8 Update circle FSM phase semantics and tests for `circle_entry`, `circle_interior`, `circle_exit`, release, and direction latch.
- [x] 5.9 Update zebra and lost FSM semantics and tests so zebra uses hold/reference/constraints and lost uses prediction until configured hold expiration.
- [x] 5.10 Update `new/code/legacy/steering_reference_policy.*` hold-last and entry-heading extension modes for cross and zebra phases.
- [x] 5.11 Update `new/code/legacy/steering_reference_policy.*` stable-boundary offset, arc-follow, blend-to-exit, and lost-prediction modes for circle and lost phases.
- [x] 5.12 Update `new/code/legacy/steering_observation_assembly.*` so it remains typed assembly for vehicle/context/constraints/debug derivatives and no longer manufactures formal scene candidate authority from old heuristics.
- [x] 5.13 Keep `new/code/legacy/steering_control_error_model.*` as the pure-pursuit/curvature-command owner; update lookahead/confidence/constraint inputs only through accepted reference path and `ControlConstraintSet`.
- [x] 5.14 Update `new/verification/tests/scene_classifier_selftest.cpp` for bend-not-special, large-bend circle veto, cross bilateral confirmation, cross hold/reacquire, circle phases, circle direction latch, zebra through FSM/reference/constraints, and lost prediction.
- [x] 5.15 Update `new/verification/tests/run_scene_classifier_selftest.sh` build inputs so the topology evidence, FSM, reference policy, and helper modules are exercised.
- [x] 5.16 Update `new/verification/tests/legacy_pid_control_cycle_test.cpp` and `run_legacy_pid_control_cycle_test.sh` so scene-type changes do not directly alter wheel/PWM output and only reference path, curvature command, constraints, or safety gates affect control.
- [ ] 5.17 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for topology hypotheses/evidence, FSM progression, reference policy, and control separation. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-4/attempt-<n>/findings.json`, verifier evidence JSON at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-4/attempt-<n>/verifier-evidence.json`, and caller/orchestrator-maintained `agent-table.json` at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-4/agent-table.json`. Caller-local guardrails: topology facts do not output control commands, bends are ordinary, direction latch cannot flip on one frame, zebra does not bypass reference/constraints, and blocking findings route to `openspec-repair-change`.

## 6. Runtime Chain, Observability, Tooling, And Board Evidence

- [x] 6.1 Update the early `AnalyzeFrame(...)` stages in `new/code/legacy/camera_logic.cpp` to call threshold, projector configuration, sparse sampler, corridor intervals, and corridor graph in order.
- [x] 6.2 Update the later `AnalyzeFrame(...)` stages in `new/code/legacy/camera_logic.cpp` to call hypotheses, topology evidence, topology FSM, reference policy, topology-derived constraints, and control error model in order.
- [ ] 6.3 Update `new/code/runtime/control_debug_snapshot.*` so snapshot structs and reporters include topology source, topology scores, evidence accumulators, reference confidence, ordinary curvature, lookahead, curvature command, visible range, and track confidence.
- [ ] 6.4 Update `new/code/runtime/control_loop.cpp` exporters to populate the new topology debug snapshot fields from perception output.
- [ ] 6.5 Update `new/code/platform/steering_media_protocol.*` so `config_snapshot` and `image_frame` expose topology parameter groups and topology authority fields.
- [ ] 6.6 Update `new/code/platform/assistant_protocol.*` so assistant telemetry exposes topology authority fields and removes or quarantines old authority fields.
- [ ] 6.7 Update `new/code/runtime/steering_media_service.*` so runtime-to-media translation carries topology source, topology scores, reference mode/confidence, and curvature fields.
- [ ] 6.8 Update `new/user/steering_media_capture.py` to parse and persist topology snapshot fields and sample/interval/hypothesis overlay metadata.
- [ ] 6.9 Update `new/user/tune_steering.py` to ingest/export topology scores, reference mode, reference confidence, curvature command, visible range, and track confidence instead of old authority fields.
- [x] 6.10 Update `new/user/scene_overlay_probe.cpp` to render raw image, sparse sample classes, corridor intervals, ordinary chain, arc/exit hypotheses, final reference path, and pure-pursuit target.
- [x] 6.11 Update `new/user/steering_media_selftest.cpp` to validate topology media contract fields and old-authority-field quarantine.
- [x] 6.12 Update `new/verification/tests/run_steering_media_selftest.sh`, `new/verification/tests/run_authority_baseline_validate.sh`, and related helpers so straight/bend/cross/circle/zebra baselines output topology scores and reference modes without old compatibility authority fields.
- [x] 6.13 Run scene and media host verification: `bash new/verification/tests/run_scene_classifier_selftest.sh` and `bash new/verification/tests/run_steering_media_selftest.sh`.
- [x] 6.14 Run control and authority host verification: `bash new/verification/tests/run_legacy_pid_control_cycle_test.sh`, `bash new/verification/tests/run_authority_baseline_validate.sh`, and `bash new/verification/tests/run_runtime_params_load_test.sh`.
- [x] 6.15 Run build and formatting verification: `cmake --build new/out --target new -j4` and `git diff --check -- new/code new/user new/config new/verification/tests`.
- [x] 6.16 Preserve verification notes for any host limitation, especially `runtime_params_load_test` compiling but not executing on hosts without `qemu-loongarch64`.
- [ ] 6.17 Capture motor-disabled board evidence after build/deploy showing topology fields, projector/sign consistency, sparse sample and interval rendering, scene phase behavior, reference mode behavior, and curvature command sign.
- [ ] 6.18 Capture low-speed powered board evidence only after motor-disabled topology/sign evidence passes; include representative straight, bend, cross, circle, and zebra observations where available.
- [ ] 6.19 Assemble the final source-first verification scope list covering changed `port/legacy/runtime/platform/config/new/user/new/verification/tests` surfaces, preserved host evidence, and board evidence.
- [ ] 6.20 Run `openspec-verify-change` for the completed source-first bundle using the scope list and evidence from task 6.19.
- [ ] 6.21 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for final source-first implementation closure. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-5/attempt-<n>/findings.json`, verifier evidence JSON at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-5/attempt-<n>/verifier-evidence.json`, and caller/orchestrator-maintained `agent-table.json` at `openspec/changes/bev-corridor-topology-perception/verification/checkpoint-5/agent-table.json`. Caller-local guardrails: active runtime has no row-scan authority, `BEVTrackEstimate.source` is topology-derived, cross/circle/zebra come from topology evidence, bends are not formal special control branches, FSM/reference/control remain decoupled, media can reconstruct raw plus BEV topology dual-view, and blocking findings route to `openspec-repair-change`.
