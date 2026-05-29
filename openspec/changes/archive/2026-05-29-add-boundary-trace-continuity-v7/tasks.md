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
  - this task list completes the schema's `applyRequires` set under `ai-enforced-workflow`, so the active artifact-creation caller owns docs-first review before implementation entry
  - `openspec-apply-change` does not own that docs-first artifact gate

## 1. Vertical Slice: Explicit Boundary Distance Parameter

- [x] 1.1 Add `BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M` to `port::BEVGeometryParameters`, defaults, JSON loading validation, config docs, and steering media config snapshot serialization; keep it as the sole boundary-trace distance source.
- [x] 1.2 Add or update parameter/default/media tests proving default parity, JSON load, invalid-value rejection, and config snapshot publication.
- [x] 1.3 Run the focused parameter feedback loops and record commands/results: `run_runtime_parameter_defaults_test.sh`, `run_param_store_load_runtime_parameters_test.sh`, and `run_steering_media_selftest.sh`.
  - `rtk bash new/verification/tests/run_runtime_parameter_defaults_test.sh` currently fails before the new field check on existing `RUNNING_SPEED_TARGET` default mismatch (`RuntimeParameters=300`, `default_params.json=200`). This change does not alter that existing parameter value per user instruction.
  - The repository has `run_param_store_load_runtime_parameters_board_test.sh`, not a no-board `run_param_store_load_runtime_parameters_test.sh`; the board script was not used for verification. Equivalent compile coverage was run with the LoongArch toolchain for `param_store_load_runtime_parameters_test.cpp` and `param_store.cpp`, with no board upload.
  - `rtk bash new/verification/tests/run_steering_media_selftest.sh` passed.

## 2. Vertical Slice: Neutral Boundary Trace Clip Helper

- [x] 2.1 Add `new/code/legacy/steering_bev_boundary_trace_clip.hpp` with neutral `BEVBoundaryTracePoint`, `BEVBoundaryTraceClipOptions`, and `ClipBoundaryTraceOutliers()` using the direct row-gap-scaled distance comparison with no added quantization tolerance.
- [x] 2.2 Add focused helper tests covering the V7 clipping rule: continuous trace, single outlier deletion with later retention at row-gap-scaled distance, consecutive outliers, and ordering.
- [x] 2.3 Run the focused helper feedback loop through `run_bev_simple_perception_test.sh`.
  - `rtk bash new/verification/tests/run_bev_simple_perception_test.sh` passed.

## 3. Vertical Slice: Ordinary Candidate Generation Uses Clipped Edge Facts

- [x] 3.1 Update `steering_bev_simple_perception.cpp` so ordinary low/high edge facts are clipped before midpoint and single-boundary candidate interpretation.
- [x] 3.2 Ensure one clipped side naturally degrades to existing single-edge semantics, two clipped sides remove the row candidate, and `BuildSingleBoundaryOffsetReference()` is called only with associated kept boundary points.
- [x] 3.3 Preserve existing screen-edge visibility, path connectivity, strict-leading extraction, hold-last ownership, and outside-sampleable single-boundary offset behavior.
- [x] 3.4 Add ordinary-reference behavior tests for discontinuous single-edge rejection, farther associated-row retention after an outlier, double-edge degradation, both-sides removal, and outside-sampleable path preservation.
- [x] 3.5 Run focused ordinary/reference feedback loops and record commands/results: `run_bev_simple_perception_test.sh`, `run_visual_reference_orchestration_test.sh`, `run_visual_element_evidence_test.sh`, and `run_steering_circle_v2_scene_test.sh`.
  - `rtk bash new/verification/tests/run_bev_simple_perception_test.sh` passed.
  - `rtk bash new/verification/tests/run_visual_reference_orchestration_test.sh` passed.
  - `rtk bash new/verification/tests/run_visual_element_evidence_test.sh` passed.
  - `rtk bash new/verification/tests/run_steering_circle_v2_scene_test.sh` passed.

## 4. Build, Smoke, And Board Consideration

- [x] 4.1 Run a local no-upload or equivalent build feedback loop and record the command/result.
  - `rtk env SKIP_UPLOAD=1 ./debug.sh build` from `new/user` passed and logged `SKIP_UPLOAD=1, build completed without upload`.
- [x] 4.2 Decide whether this turn performs board upload/smoke. If skipped, record the reason and the intended on-board hook: loaded config snapshot plus steering-media candidate path inspection for the V7 jump pattern.
  - Board upload/smoke skipped because the user requested no board-side action for this review/fix cycle. On-board hook remains loaded config snapshot plus steering-media candidate path inspection for the V7 jump pattern.

## 5. Verification And Review

- [x] 5.1 Confirm domain language and alignment evidence from `new/docs/path-evaluation-boundary-continuity-v7.zh-CN.md`, `openspec/specs/ordinary-bev-reference/spec.md`, `openspec/specs/steering-tuning-media-observability/spec.md`, `steering_bev_simple_perception.cpp`, `steering_bev_interval_edges.hpp`, `steering_single_boundary_offset.cpp`, and `steering_reference_connectivity.cpp`.
- [x] 5.2 [Checkpoint] Run docs-first verifier-subagent review for `proposal.md`, `design.md`, `tasks.md`, and `specs/**/*.md` using `verify-sequence/default`. Use field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Require `review_goal=implementation_correctness`, authoritative findings JSON at `review/review-runs/add-boundary-trace-continuity-v7/docs-first-findings.json`, verifier evidence JSON at `review/review-runs/add-boundary-trace-continuity-v7/docs-first-verifier-evidence.json`, and caller/orchestrator-maintained `agent-table.json`. Follow `cycle_rules`, require fields from `verifier_evidence_required`, enforce valid-pass requirements, require subject binding through `subject_required_any_of`, and route blocking findings through `openspec-repair-change`. Docs-first final active pass is recorded in `docs-first-final-findings.json` and `docs-first-final-verifier-evidence.json`.
- [x] 5.3 [Checkpoint] Run source-first verifier-subagent review for changed implementation, tests, config, docs, and directly impacted code using `verify-sequence/default`. Use the same shared field groups, authoritative findings JSON at `review/review-runs/add-boundary-trace-continuity-v7/findings.json`, verifier evidence JSON at `review/review-runs/add-boundary-trace-continuity-v7/verifier-evidence.json`, and current-state-only `agent-table.json`.
  - Initial source-first verifier blocked on F001: single-boundary support could use a previous kept point. Fixed by restricting support to future kept same-side points and adding `TestBoundaryContinuityRequiresFutureSupportForSingleEdge()`. Same verifier rerun passed.
- [x] 5.4 After the first valid source-first pass, run extra source-first verifier rounds until two consecutive valid source-first passes are recorded. Use `extra-pass-1-*` and `extra-pass-2-*` report paths from `design.md`; repair any blocking finding and restart the consecutive-pass count.
  - `extra-pass-1-findings.json` / `extra-pass-1-verifier-evidence.json`: pass.
  - `extra-pass-2-findings.json` / `extra-pass-2-verifier-evidence.json`: pass.

## 6. Cleanup And Lifecycle

- [x] 6.1 Remove or absorb temporary prototypes/instrumentation and update task checkboxes with final evidence.
  - No temporary prototype or instrumentation remains; review-run JSON files are retained as verification evidence.
- [x] 6.2 Sync delta specs into `openspec/specs/ordinary-bev-reference/spec.md` and `openspec/specs/steering-tuning-media-observability/spec.md` after two consecutive source-first verifier passes.
  - Synced after source-first pass plus extra pass 1 and extra pass 2. `rtk openspec validate --all --strict --json` passed.
- [x] 6.3 Archive `add-boundary-trace-continuity-v7` after sync and record final evidence paths in the completion summary.
  - Archive target: `openspec/changes/archive/2026-05-29-add-boundary-trace-continuity-v7/`.
