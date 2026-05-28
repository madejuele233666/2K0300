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
- Supported continuation overrides:
  - `verify-only`
  - `dry-run`
  - `manual_pause`
- Artifact-completion gate ownership:
  - when this task list completes the schema's `applyRequires` set under `ai-enforced-workflow`, the active artifact-creation caller runs docs-first review before implementation entry
  - `openspec-apply-change` does not own that docs-first artifact gate

## 1. Vertical Slice: OpenSpec V5 Contract

- [x] 1.1 Create `proposal.md`, `design.md`, `tasks.md`, and delta specs for `bev-reference-connectivity`, `ordinary-bev-reference`, and `steering-tuning-media-observability`.
- [x] 1.2 Run `openspec validate add-bev-reference-connectivity-v5 --strict --no-interactive`.
- [x] 1.3 [Checkpoint] Run docs-first verifier-subagent review using `verify-sequence/default` for the change artifacts. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `review/review-runs/add-bev-reference-connectivity-v5/docs-first-findings.json`, verifier evidence JSON at `review/review-runs/add-bev-reference-connectivity-v5/docs-first-verifier-evidence.json`, and caller/orchestrator-maintained `agent-table.json`.

## 2. Vertical Slice: Reference Connectivity Helper

- [x] 2.1 Add `new/code/legacy/steering_reference_connectivity.hpp/.cpp` with zero-copy grayscale-frame segment/path connectivity checking.
- [x] 2.2 Add helper coverage for white connected segments, black blocked segments, endpoint-black rejection, diagonal-black rejection, leading path traversal, single-point paths, and black-only semantics.
- [x] 2.3 Run `rtk bash new/verification/tests/run_bev_simple_perception_test.sh` and record the command output.

## 3. Vertical Slice: V5 Parameter Surface

- [x] 3.1 Add `BEV_GEOMETRY.REFERENCE_LATERAL_JUMP_GATE_M` to runtime types, parser validation, default JSON, parameter docs, and steering media config snapshots.
- [x] 3.2 Route existing ordinary lateral jump checks through the new parameter while keeping `LATERAL_STEP_M` as sampling resolution only.
- [x] 3.3 Add or update parameter/default/media tests, including a no-false-rejection case proving the default `REFERENCE_LATERAL_JUMP_GATE_M=1000.0` no longer rejects a normal large adjacent lateral change, and run `rtk bash new/verification/tests/run_runtime_parameter_defaults_test.sh`, `rtk bash new/verification/tests/run_param_store_load_runtime_parameters_board_test.sh`, and `rtk bash new/verification/tests/run_steering_media_selftest.sh`.
  - Local default/media tests passed. Board parameter script was attempted with `rtk timeout 30s ...` and timed out in the SSH/SCP transport path, so board-side execution is recorded under 5.3 rather than treated as local evidence.

## 4. Vertical Slice: Runtime Candidate Gate

- [x] 4.1 Apply `ReferencePathHasNoBlackSegments()` at the current-frame visual candidate aggregation point in `new/code/runtime/steering_frame_perception_pipeline.cpp`.
- [x] 4.2 Keep hold-last outside the V5 gate and keep `CircleV2Scene`, cross evidence, candidate adapters, and selector unaware of connectivity details.
- [x] 4.3 Update build/test source lists and add or update coverage showing the central gate can withhold disconnected-white visual paths for line, CircleV2, and cross-style candidates without changing generator ownership.
- [x] 4.4 Run `rtk bash new/verification/tests/run_scene_overlay_probe_authority_baseline_test.sh`, `rtk bash new/verification/tests/run_steering_circle_v2_scene_test.sh`, `rtk bash new/verification/tests/run_visual_element_evidence_test.sh`, and `rtk bash new/verification/tests/run_visual_reference_orchestration_test.sh`.

## 5. Integration Verification

- [x] 5.1 Run `rtk env SKIP_UPLOAD=1 ./new/user/debug.sh build` to compile the board runtime without uploading.
- [x] 5.2 Run `openspec validate add-bev-reference-connectivity-v5 --strict --no-interactive` after implementation.
- [x] 5.3 Record board smoke status. If hardware is not explicitly requested for this turn, record that board smoke was intentionally not executed and local build/probe evidence was used.
  - Board upload/smoke is not used as completion evidence for this turn. The board parameter script probe timed out after 30 seconds in SSH/SCP transport to the default board address.
- [x] 5.4 [Checkpoint] Run source-first verifier-subagent review using `verify-sequence/default` for changed source, tests, config, docs, and OpenSpec artifacts. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON at `review/review-runs/add-bev-reference-connectivity-v5/findings.json`, verifier evidence JSON at `review/review-runs/add-bev-reference-connectivity-v5/verifier-evidence.json`, and caller/orchestrator-maintained `agent-table.json`.

## 6. OpenSpec Closure

- [x] 6.1 Sync delta specs into `openspec/specs/` after source-first verification passes.
- [x] 6.2 Archive `openspec/changes/add-bev-reference-connectivity-v5` after specs are synced and tasks are complete.
