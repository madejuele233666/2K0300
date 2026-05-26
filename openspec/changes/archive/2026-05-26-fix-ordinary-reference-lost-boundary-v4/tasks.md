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
- Agent lifecycle:
  - follow `cycle_rules` in `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- Routing target for blocking findings:
  - `openspec-repair-change`
- Supported continuation overrides:
  - `verify-only`
  - `dry-run`
  - `manual_pause`
- Artifact-completion gate ownership:
  - when this task list completes the schema's `applyRequires` set under
    `ai-enforced-workflow`, the active artifact-creation caller
    (`openspec-propose` or `openspec-continue-change`)
    runs docs-first review before implementation entry
  - `openspec-apply-change` does not own that docs-first artifact gate

## 1. Vertical Slice: Shared Single-Boundary Helper

- [x] 1.1 Add `new/code/legacy/steering_single_boundary_offset.hpp/.cpp` with a pure BEV helper that consumes boundary trace points, target forward samples, and signed normal offset.
- [x] 1.2 Add helper tests covering straight zero slope, nonzero slope, zero offset, positive/negative offsets, leading stop, and direction-unavailable output.
- [x] 1.3 Run the helper-focused test target or the enclosing BEV simple perception test script and record the command: `rtk bash new/verification/tests/run_bev_simple_perception_test.sh`.

## 2. Vertical Slice: Ordinary Lost-Boundary Reference

- [x] 2.1 Update `new/code/legacy/steering_bev_simple_perception.cpp` so ordinary reference extraction interprets low/high endpoint visibility before selecting center candidates.
- [x] 2.2 Preserve strict leading behavior, existing `ReferenceMode::kIntervalCenter` / `simple_interval_center` output compatibility, and reference-continuity ownership of hold-last.
- [x] 2.3 Add ordinary reference tests for both-edge midpoint, low-edge normal offset, high-edge normal offset, double-lost unavailability, multi-interval candidate selection, and hold bridge compatibility.
- [x] 2.4 Run `new/verification/tests/run_bev_simple_perception_test.sh` and record the command: `rtk bash new/verification/tests/run_bev_simple_perception_test.sh`.

## 3. Vertical Slice: CircleV2 Helper Reuse

- [x] 3.1 Update `new/code/runtime/detail/steering_circle_v2_composer.cpp` so InnerTrace and ExitTrace path composition call the shared helper with caller-owned edge trace and signed offset.
- [x] 3.2 Preserve CircleV2 reducer/event/telemetry ownership: helper absence can suppress a reference plan but must not mutate `CircleV2Memory`.
- [x] 3.3 Add or update CircleV2 scene tests for zero-offset InnerTrace, nonzero sloped InnerTrace offset, ExitTrace helper reuse, and geometry-unavailable plan absence.
- [x] 3.4 Run `new/verification/tests/run_steering_circle_v2_scene_test.sh` and record the command: `rtk bash new/verification/tests/run_steering_circle_v2_scene_test.sh`.
- [x] 3.5 Preserve cross-over-circle composition suppression from public cross evidence when V4 leaves the line candidate unavailable; verified with `rtk bash new/verification/tests/run_scene_overlay_probe_authority_baseline_test.sh`.

## 4. Integration Verification

- [x] 4.1 Run `openspec validate fix-ordinary-reference-lost-boundary-v4 --strict` or the repository-supported equivalent schema validation command.
- [x] 4.2 Run a local build or focused compile path that includes modified legacy/runtime files: BEV simple, CircleV2 scene, visual element, and scene overlay probe test scripts all compiled modified sources; `steering_frame_perception_pipeline.cpp` was directly compiled with `rtk c++ -std=c++17 -Wall -Wextra -Werror -pthread -Inew/code -Inew/code/port -Inew/code/legacy -Inew/code/runtime -Inew/code/platform -Inew/code/platform/true_ls2k0300 -c new/code/runtime/steering_frame_perception_pipeline.cpp -o /tmp/steering_frame_perception_pipeline.o`.
- [x] 4.3 Run a board-facing no-motion or supervised steering-media smoke only if hardware is reachable and safe; otherwise record that board smoke was intentionally not executed in this turn. Board smoke was intentionally not executed; local scene overlay authority baseline was used instead.
- [x] 4.4 Run source-first verification through `openspec-verify-change` using `verify-sequence/default`, authoritative findings JSON at `review/review-runs/fix-ordinary-reference-lost-boundary-v4/findings.json`, verifier evidence JSON at `review/review-runs/fix-ordinary-reference-lost-boundary-v4/verifier-evidence.json`, and caller/orchestrator-maintained `agent-table.json`.
- [x] 4.5 Require source-first verifier outputs to satisfy `verifier_evidence_required`, `subject_required_any_of`, `findings_required`, `finding_object_required`, `finding_semantics`, and `repair_routing_rules`; blocking findings route to `openspec-repair-change`.

## 5. OpenSpec Closure

- [x] 5.1 Confirm the docs-first artifact gate has already passed before implementation entry and is not re-run as a closure task.
- [x] 5.2 Sync delta specs into `openspec/specs/` after implementation verification passes.
- [x] 5.3 Archive `openspec/changes/fix-ordinary-reference-lost-boundary-v4` after specs are synced and tasks are complete.
