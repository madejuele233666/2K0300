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

- [x] 1.1 Validate `proposal.md`, `design.md`, `specs/bev-visual-element-evidence/spec.md`, and `tasks.md` with `rtk openspec validate add-circle-element-evidence-phase1 --strict`.
- [x] 1.2 Audit the alignment mapping from `README.md`, `steering_bev_element_raster.hpp`, `visual_element_evidence_types.hpp`, `steering_visual_element_pipeline.*`, `steering_frame_perception_pipeline.cpp`, `param_store.cpp`, `default_params.json`, and `scene_overlay_probe.cpp` against the design deliverables.
- [x] 1.3 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for the OpenSpec artifact set. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `openspec/changes/add-circle-element-evidence-phase1/verification/docs-first/attempt-<n>/findings.json`, verifier evidence JSON at `openspec/changes/add-circle-element-evidence-phase1/verification/docs-first/attempt-<n>/verifier-evidence.json`, and caller/orchestrator-maintained `openspec/changes/add-circle-element-evidence-phase1/verification/docs-first/agent-table.json`.

## 2. Circle Evidence Contracts And Parameters

- [x] 2.1 Extend `BEVElementParameters` in `new/code/port/visual_element_evidence_types.hpp` with append-only visual element fields: `cross_wide_row_white_ratio_min`, `circle_evidence_enabled`, `circle_min_support_rows`, `circle_min_sampleable_per_row`, `circle_open_expansion_min_m`, `circle_opening_expansion_ratio_min`, `circle_opposite_straight_drift_max_m`, `circle_opposite_shrink_ratio_min`, and `circle_present_confidence_min`.
- [x] 2.2 Update `new/config/default_params.json`, `new/code/platform/param_store.cpp`, and parameter snapshot helpers so the new `BEV_ELEMENT` fields load optionally, preserve missing-field defaults, reject malformed/out-of-range values through the existing parse-failure path, and keep `CROSS_EXIT_TAKEOVER_ENABLED` semantics unchanged.
- [x] 2.3 Update docs or README parameter notes so `BEV_ELEMENT` circle fields are visible as evidence thresholds rather than candidate-takeover controls.
- [x] 2.4 Add or update parameter tests in `new/verification/tests/runtime_parameter_defaults_test.cpp` and `new/verification/tests/param_store_load_runtime_parameters_test.cpp` for defaults, missing fields, explicit disable, and malformed/out-of-range circle fields.

## 3. Raster-Backed Circle Detector

- [x] 3.1 Add `new/code/legacy/steering_circle_element_evidence.hpp` and `new/code/legacy/steering_circle_element_evidence.cpp` with a raster-only detector that emits raw `circle_left_raw` and `circle_right_raw` `VisualElementEvidenceRecord` values.
- [x] 3.2 Implement the minimal visual rule from `BEVElementRasterFrame`: left opening plus right straight support yields left circle; right opening plus left straight support yields right circle; both-open and no-open fail closed as non-circle evidence.
- [x] 3.3 Ensure the detector never reads cross evidence, line candidates, hold memory, safety, IMU, encoder, yaw, actuator, control phase, debug overlays, media output, or archive topology state.
- [x] 3.4 Add focused circle detector tests covering left present, right present, both-open absent, no-open absent, disabled evidence, missing/invalid raster, insufficient sampleability, excessive opposite-side drift, and low confidence.
- [x] 3.5 Add the new detector and tests to the relevant `new/user/CMakeLists.txt` targets without removing existing visual element, raster, or telemetry tests.

## 4. Visual Element Pipeline Integration

- [x] 4.1 Register the circle detector in `new/code/legacy/steering_visual_element_pipeline.*` using `input.element_raster` while preserving existing cross detection and cross candidate behavior.
- [x] 4.2 Always append circle records after `cross_exit` in the order `circle_left_raw`, `circle_right_raw`, `circle_left`, `circle_right`.
- [x] 4.3 Implement cross suppression only in the pipeline: when `cross_exit.present=true`, preserve raw circle records and set effective `circle_left/right.present=false` with `reason=suppressed_by_cross_exit`.
- [x] 4.4 Set all circle candidate summaries to `built=false`, `takeover_enabled=false`, `included_in_arbitration=false`, and `reason=evidence_only`; do not push `kCircleLeft` or `kCircleRight` candidates.
- [x] 4.5 Update visual element pipeline tests to cover record order, raw preservation, effective suppression, cross-absent raw-to-effective mirroring, and no circle candidates in arbitration.

## 5. Offline Probe And Authority-Baseline Evidence

- [x] 5.1 Update `new/user/scene_overlay_probe.cpp` so `RunProbePipeline` builds a `BEVElementRasterFrame` with the current raw frame, Otsu threshold, runtime params, and projector before calling `RunVisualElementPipeline`.
- [x] 5.2 Extend probe output or residual parsing so generic circle records are visible without changing runtime control behavior.
- [x] 5.3 Run authority-baseline raw checks with deterministic expectations: `circle-1/2/3.raw` have `cross_exit.present=false`, `circle_left_raw.present=true`, and effective `circle_left.present=true`; `cross-1/2/3.raw` have cross evidence and absent raw/effective circle records because both sides are open; `bend-1/2/3.raw` have absent raw/effective circle records.
- [x] 5.4 Update `new/verification/tests/run_bev_simple_residual_check.sh` expectations only where the runtime-raster-backed evidence contract requires it.
- [x] 5.5 Add and run `new/verification/tests/run_scene_overlay_probe_authority_baseline_test.sh` so the named authority-baseline raw checks are source-controlled and assertable.
- [x] 5.6 Update `new/user/scene_overlay_probe.cpp` so offline probe parameter loading includes `BEV_ELEMENT` and `BEV_ELEMENT_RASTER`, mirrors malformed/out-of-range and non-object parent fallback for those fields, and verify disabled, invalid-confidence, and non-object parent overrides change probe output as expected.

## 6. Regression Verification

- [x] 6.1 Run `rtk bash new/verification/tests/run_visual_element_evidence_test.sh`.
- [x] 6.2 Run `rtk bash new/verification/tests/run_visual_reference_orchestration_test.sh`.
- [x] 6.3 Run `rtk bash new/verification/tests/run_bev_simple_perception_test.sh`.
- [x] 6.4 Run `rtk bash new/verification/tests/run_assistant_telemetry_selftest.sh`.
- [x] 6.5 Run `rtk bash new/verification/tests/run_steering_media_selftest.sh`.
- [x] 6.6 Run `rtk bash new/verification/tests/run_runtime_parameter_defaults_test.sh`.
- [x] 6.7 Run `rtk bash new/verification/tests/run_param_store_load_runtime_parameters_test.sh` or the current parameter-store load test entrypoint.
- [x] 6.8 Run `rtk bash new/verification/tests/run_bev_simple_residual_check.sh`.
- [x] 6.9 Run `rtk env SKIP_UPLOAD=1 new/user/build.sh`.
- [x] 6.10 Run `rtk git diff --check`.
- [x] 6.11 Run `code-index refresh` after code edits settle.
- [x] 6.12 Run board no-motion smoke only after local verification passes: build/upload through the accepted project workflow, start normal no-motion evidence capture, verify `element_evidence.records` includes circle records and new `BEV_ELEMENT` values, and verify selected-reference/actuator behavior remains unchanged.
- [x] 6.13 Run `rtk env ARTIFACT_DIR=/home/madejuele/projects/2K0300/openspec/changes/add-circle-element-evidence-phase1/verification/source-first/attempt-1/authority-baseline-probe bash new/verification/tests/run_scene_overlay_probe_authority_baseline_test.sh`.
- [x] 6.14 Run `rtk env ARTIFACT_DIR=/home/madejuele/projects/2K0300/openspec/changes/add-circle-element-evidence-phase1/verification/source-first/attempt-3/authority-baseline-probe bash new/verification/tests/run_scene_overlay_probe_authority_baseline_test.sh`.
- [x] 6.15 Run `rtk env ARTIFACT_DIR=/home/madejuele/projects/2K0300/openspec/changes/add-circle-element-evidence-phase1/verification/source-first/attempt-4/authority-baseline-probe bash new/verification/tests/run_scene_overlay_probe_authority_baseline_test.sh`.
- [x] 6.16 Run `rtk env ARTIFACT_DIR=/home/madejuele/projects/2K0300/openspec/changes/add-circle-element-evidence-phase1/verification/source-first/attempt-5/authority-baseline-probe bash new/verification/tests/run_scene_overlay_probe_authority_baseline_test.sh`.
- [x] 6.17 Run board right-circle static evidence after final upload: `/home/madejuele/projects/2K0300/new/verification/circle-right-board-after-upload-20260506-115628-long/summary.json` showed `cross_exit.present=false` and `circle_right_raw/effective.present=true` for 62/62 snapshots.
- [x] 6.18 Run board bend static evidence after bend-reason repair: `/home/madejuele/projects/2K0300/new/verification/bend-board-after-reason-fix-20260506-1135/summary.json` showed `cross_exit.present=false` and all circle records absent with `reason=bend` for 24/24 snapshots.
- [x] 6.19 Run board cross static evidence: `/home/madejuele/projects/2K0300/new/verification/cross-board-static-20260506-120238/summary.json` showed `cross_exit.present=true` and all circle records absent with `reason=saturated_wide_white_rows` for 26/26 snapshots.

## 7. Implementation Review Gate

- [x] 7.1 Run source-first verifier review using `verify-sequence/default` for changed code, tests, config, docs, probe tooling, and directly impacted serializers. Require `review_goal=implementation_correctness`, fields from `verifier_evidence_required`, subject binding through `subject_required_any_of`, explicit `review_scope`, and valid-pass requirements from `valid_pass_requirements`.
- [x] 7.2 Write authoritative source-first findings JSON at `openspec/changes/add-circle-element-evidence-phase1/verification/source-first/attempt-<n>/findings.json`, verifier evidence JSON at `openspec/changes/add-circle-element-evidence-phase1/verification/source-first/attempt-<n>/verifier-evidence.json`, and caller/orchestrator-maintained `openspec/changes/add-circle-element-evidence-phase1/verification/source-first/agent-table.json`.
- [x] 7.3 Route any blocking findings through `openspec-repair-change`, preserving findings semantics from `findings_required`, `finding_object_required`, `finding_semantics`, and `repair_routing_rules`.
- [x] 7.4 Require the reusable working verifier to reach zero findings with complete/exhaustive coverage, then require a fresh challenger pass before implementation closure.

## 8. Phase 2 Entry Closeout

- [x] 8.1 Align `proposal.md`, `design.md`, and delta spec with the final as-built strict cross/circle/bend semantics and runtime defaults.
- [x] 8.2 Sync `bev-visual-element-evidence` delta requirements into `openspec/specs/bev-visual-element-evidence/spec.md`.
- [x] 8.3 Rerun final closeout regression commands after the closeout edits.
- [x] 8.4 Write final closeout verification evidence summary.
- [x] 8.5 Archive `add-circle-element-evidence-phase1` after validation, sync, and regression pass.
