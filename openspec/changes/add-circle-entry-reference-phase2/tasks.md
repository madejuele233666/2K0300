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
  - `openspec-align` for existing-system reference mapping
  - `openspec-architect` for stack equivalents and tradeoffs
  - `openspec-artifact-verify` for docs-first artifact review
  - `openspec-verify-change` for implementation review
- Supported continuation overrides:
  - `verify-only`
  - `dry-run`
  - `manual_pause`
- Artifact-completion gate ownership:
  - when this task list completes the schema's `applyRequires` set under `ai-enforced-workflow`, the active artifact-creation caller (`openspec-propose` or `openspec-continue-change`) owns docs-first review before implementation entry
  - `openspec-apply-change` does not own that docs-first artifact gate

## 1. OpenSpec Artifact Checks

- [ ] 1.1 Validate `proposal.md`, `design.md`, `specs/circle-entry-reference-candidate/spec.md`, `specs/bev-visual-element-evidence/spec.md`, and `tasks.md` with `rtk openspec validate add-circle-entry-reference-phase2 --strict`.
- [ ] 1.2 Audit the alignment mapping from `README.md`, `steering_circle_element_evidence.*`, `steering_visual_element_pipeline.*`, `visual_element_evidence_types.hpp`, `visual_reference_orchestration_types.hpp`, `steering_frame_perception_pipeline.cpp`, `scene_overlay_probe.cpp`, and authority-baseline tests against the design deliverables.
- [ ] 1.3 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for the OpenSpec artifact set. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `openspec/changes/add-circle-entry-reference-phase2/verification/docs-first/attempt-<n>/findings.json`, verifier evidence JSON at `openspec/changes/add-circle-entry-reference-phase2/verification/docs-first/attempt-<n>/verifier-evidence.json`, and caller/orchestrator-maintained `openspec/changes/add-circle-entry-reference-phase2/verification/docs-first/agent-table.json`.

## 2. Circle Entry Contracts And Parameters

- [ ] 2.1 Extend circle typed facts in `new/code/legacy/steering_circle_element_evidence.hpp` so `CircleElementEvidenceResult` carries left/right entry facts without changing `VisualElementEvidenceRecord` wire fields.
- [ ] 2.2 Extend `BEVElementParameters` in `new/code/port/visual_element_evidence_types.hpp` with append-only fields: `circle_entry_takeover_enabled`, `circle_entry_min_frontier_points`, `circle_entry_direction_min_lateral_m`, `circle_entry_max_interpolation_gap_m`, and `circle_entry_max_join_jump_m`.
- [ ] 2.3 Update `new/config/default_params.json`, `new/config/default_params.md`, `new/code/platform/param_store.cpp`, and parameter snapshot/serialization helpers so new `BEV_ELEMENT` fields load optionally, preserve defaults, reject malformed/out-of-range values through the existing parse-failure path, and keep existing cross/Phase1 circle semantics unchanged.
- [ ] 2.4 Add or update parameter tests in `new/verification/tests/runtime_parameter_defaults_test.cpp` and `new/verification/tests/param_store_load_runtime_parameters_test.cpp` for defaults, missing fields, explicit takeover enable, and malformed/out-of-range entry fields.

## 3. Raster Entry Facts

- [ ] 3.1 Implement near-connected white component extraction in `new/code/legacy/steering_circle_element_evidence.cpp` from current-frame sampleable white raster cells, starting from the nearest valid white support.
- [ ] 3.2 Implement rear-side sampleable black-white frontier extraction that rejects unknown, invalid, unavailable, outside-frame, raster/FOV-boundary, and detached-island support.
- [ ] 3.3 Implement side-specific frontier chain direction gating using net forward motion and `CIRCLE_ENTRY_DIRECTION_MIN_LATERAL_M`, without requiring per-point monotonic lateral movement.
- [ ] 3.4 Implement median near-row half-width inference and frontier-derived centerline points with left formula `frontier + road_half_width` and right formula `frontier - road_half_width`.
- [ ] 3.5 Add focused circle evidence tests covering left/right entry facts, FOV-boundary rejection, unknown/unavailable rejection, non-sampleable black rejection, detached component rejection, direction below `0.08m`, insufficient frontier points, and insufficient near-row width support.

## 4. Circle Entry Candidate Builder And Pipeline

- [ ] 4.1 Add `BuildCircleEntryVisualReferenceCandidate` in circle-specific legacy files, consuming only the effective circle record, side-specific typed entry facts, and runtime parameters.
- [ ] 4.2 Build `BEVReferencePath` with `ReferenceMode::kIntervalCenter`, leading near-component centerline support, frontier-derived centerline support, circle-local interpolation, `CIRCLE_ENTRY_MAX_INTERPOLATION_GAP_M`, and `CIRCLE_ENTRY_MAX_JOIN_JUMP_M`.
- [ ] 4.3 Integrate the builder in `new/code/legacy/steering_visual_element_pipeline.cpp` after effective circle records are determined, preserving raw record evidence-only summaries.
- [ ] 4.4 Ensure default `CIRCLE_ENTRY_TAKEOVER_ENABLED=false` reports `candidate.built=true`, `included_in_arbitration=false`, `reason=takeover_disabled` when build succeeds, and pushes no candidate.
- [ ] 4.5 Ensure takeover enabled pushes valid `kCircleLeft`/`kCircleRight` candidates with source `circle_left`/`circle_right`, while cross-suppressed effective records never build or push candidates.
- [ ] 4.6 Update visual element pipeline and visual-reference orchestration tests for record order, raw/effective candidate summaries, cross suppression, path continuity from index 0, source/kind/confidence, enabled arbitration, and invalid-path rejection.

## 5. Offline Probe And Authority-Baseline Evidence

- [ ] 5.1 Update `new/user/scene_overlay_probe.cpp` to print or serialize circle entry diagnostics needed for review: entry reason, direction delta, road half-width, frontier support count, centerline support count, and candidate summary.
- [ ] 5.2 Update `new/verification/tests/run_scene_overlay_probe_authority_baseline_test.sh` so `circle-1/2/3.raw` assert cross false, effective `circle_left` true, candidate built, and default takeover excluded.
- [ ] 5.3 Add takeover-enabled authority-baseline coverage so `circle-1/2/3.raw` select `visual_reference.source=circle_left` when `BEV_ELEMENT.CIRCLE_ENTRY_TAKEOVER_ENABLED=true`.
- [ ] 5.4 Keep `cross-1/2/3.raw` and `bend-1/2/3.raw` expectations negative for circle entry candidate build, including cross suppression and bend/non-circle absence.
- [ ] 5.5 Capture probe artifacts under the change verification directory for implementation review, including default and takeover-enabled authority-baseline runs.

## 6. Regression Verification

- [ ] 6.1 Run `rtk bash new/verification/tests/run_visual_element_evidence_test.sh`.
- [ ] 6.2 Run `rtk bash new/verification/tests/run_scene_overlay_probe_authority_baseline_test.sh`.
- [ ] 6.3 Run `rtk bash new/verification/tests/run_visual_reference_orchestration_test.sh`.
- [ ] 6.4 Run `rtk bash new/verification/tests/run_bev_simple_perception_test.sh`.
- [ ] 6.5 Run `rtk bash new/verification/tests/run_runtime_parameter_defaults_test.sh`.
- [ ] 6.6 Run the current parameter-store load test entrypoint covering `param_store_load_runtime_parameters_test.cpp`.
- [ ] 6.7 Run `rtk bash new/verification/tests/run_steering_media_selftest.sh`.
- [ ] 6.8 Run `rtk env SKIP_UPLOAD=1 new/user/debug.sh build`.
- [ ] 6.9 Run `rtk git diff --check`.
- [ ] 6.10 Run `rtk code-index refresh` after code edits settle.

## 7. Board Static Smoke

- [ ] 7.1 After local verification passes, build/upload through the accepted project workflow with `CIRCLE_ENTRY_TAKEOVER_ENABLED=false`, collect static/no-motion assistant or media evidence at a circle entrance, and verify circle entry candidate summaries are built but excluded from arbitration.
- [ ] 7.2 Collect static/no-motion evidence at cross and bend scenes and verify cross suppression or circle absence prevents circle entry candidate build.
- [ ] 7.3 Only after static evidence is reviewed, perform an explicitly parameter-enabled circle entry check; verify selected source becomes `circle_left`/`circle_right` while downstream readiness/safety/yaw/actuator gates remain observable.

## 8. Implementation Review Gate

- [ ] 8.1 Run source-first verifier review using `verify-sequence/default` for changed code, tests, config, docs, probe tooling, and directly impacted serializers. Require `review_goal=implementation_correctness`, fields from `verifier_evidence_required`, subject binding through `subject_required_any_of`, explicit `review_scope`, and valid-pass requirements from `valid_pass_requirements`.
- [ ] 8.2 Write authoritative source-first findings JSON at `openspec/changes/add-circle-entry-reference-phase2/verification/source-first/attempt-<n>/findings.json`, verifier evidence JSON at `openspec/changes/add-circle-entry-reference-phase2/verification/source-first/attempt-<n>/verifier-evidence.json`, and caller/orchestrator-maintained `openspec/changes/add-circle-entry-reference-phase2/verification/source-first/agent-table.json`.
- [ ] 8.3 Route any blocking findings through `openspec-repair-change`, preserving findings semantics from `findings_required`, `finding_object_required`, `finding_semantics`, and `repair_routing_rules`.
- [ ] 8.4 Require the reusable working verifier to reach zero findings with complete/exhaustive coverage, then require a fresh challenger pass before implementation closure.
