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
- Routing target for blocking findings:
  - `openspec-repair-change`
- Supported continuation overrides:
  - `verify-only`
  - `dry-run`
  - `manual_pause`
- Artifact-completion gate ownership:
  - artifact creation owns docs-first review before implementation entry
  - implementation owns source-first review before archive

## 1. OpenSpec Artifacts

- [x] 1.1 Create proposal, design, spec deltas, and tasks for `establish-bev-element-raster-pipeline`.
- [x] 1.2 Validate the change with `openspec validate establish-bev-element-raster-pipeline --strict`.
- [x] 1.3 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for proposal/specs/design/tasks. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON and verifier evidence JSON, and require caller/orchestrator-maintained `agent-table.json`.

## 2. Runtime Raster And Parameters

- [x] 2.1 Add `BEV_ELEMENT_RASTER` runtime parameter defaults, loading, validation, docs, and config/media snapshot support.
- [x] 2.2 Add `BEVElementRaster` contracts and a LUT-backed runtime raster builder with reusable buffers.
- [x] 2.3 Add focused tests for raster metric/cell conversion, LUT classification consistency, unavailable cells, and `SegmentTouchesBlack()`.
- [x] 2.4 Add performance-stage evidence for raster build time.

## 3. Pipeline Refactor

- [x] 3.1 Split cross detector/candidate builder into `steering_cross_exit_element_evidence.*` with behavior unchanged.
- [x] 3.2 Add `steering_visual_element_pipeline.*` as the element aggregation entry point, currently registering only cross.
- [x] 3.3 Extract single-frame steering perception work from `PerceptionFrontend` into a dedicated frame pipeline while keeping frontend lifecycle responsibilities.
- [x] 3.4 Verify default selected-reference, hold, lateral-error, readiness, and cross-takeover behavior remains unchanged.

## 4. Evidence Compatibility And Serialization

- [x] 4.1 Extend `VisualElementEvidenceFrame` with generic extension records while preserving typed `cross_exit`.
- [x] 4.2 Add shared element-evidence copy/serialization helpers for assistant telemetry, steering media, debug reporter, and overlay/probe surfaces.
- [x] 4.3 Add serializer tests proving `cross_exit` remains stable and synthetic extension records are appended compatibly.

## 5. Regression Verification

- [x] 5.1 Run `rtk bash new/verification/tests/run_visual_element_evidence_test.sh`.
- [x] 5.2 Run `rtk bash new/verification/tests/run_visual_reference_orchestration_test.sh`.
- [x] 5.3 Run `rtk bash new/verification/tests/run_bev_simple_perception_test.sh`.
- [x] 5.4 Run `rtk bash new/verification/tests/run_assistant_telemetry_selftest.sh`.
- [x] 5.5 Run `rtk bash new/verification/tests/run_steering_media_selftest.sh`.
- [x] 5.6 Run `rtk bash new/verification/tests/run_bev_simple_residual_check.sh`.
- [x] 5.7 Run `rtk bash new/verification/tests/run_runtime_parameter_defaults_test.sh`.
- [x] 5.8 Run the parameter-store load test updated for `BEV_ELEMENT_RASTER`.
- [x] 5.9 Run `rtk env SKIP_UPLOAD=1 new/user/build.sh`.
- [x] 5.10 Run `rtk git diff --check`.
- [x] 5.11 Run board no-motion smoke: build/upload, normal no-motion capture, verify old `cross_exit` telemetry/media fields remain, verify extension-compatible JSON parseability, verify raster-enabled cadence, and verify actuator behavior unchanged.
- [x] 5.12 [Checkpoint] Run source-first verifier-subagent review using `verify-sequence/default` for changed code/tests/config/docs. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON and verifier evidence JSON, require caller/orchestrator-maintained `agent-table.json`, and require a fresh challenger pass for implementation closure.
