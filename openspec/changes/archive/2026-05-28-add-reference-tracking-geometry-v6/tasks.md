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

## 1. Vertical Slice: Tracking Geometry Fact

- [x] 1.1 Add `new/code/port/reference_tracking_geometry_types.hpp` and wire it into `PerceptionResult`.
- [x] 1.2 Add `new/code/legacy/steering_reference_tracking_geometry.hpp/.cpp` to compute lateral offset, heading error, curvature, sample count, and reason from `BEVReferencePath`, `ReferenceUsability`, and BEV control-model parameters only.
- [x] 1.3 Add `reference_tracking_geometry_test` covering straight, offset straight, curved, insufficient sample, and degenerate fit cases.

## 2. Vertical Slice: Runtime Control Chain

- [x] 2.1 Compute `ReferenceTrackingGeometry` after selected/held reference usability in `SteeringFramePerceptionPipeline`.
- [x] 2.2 Recompute `ReferenceTrackingGeometry` after control-time reference alignment in `control_loop.cpp`.
- [x] 2.3 Update `ReferenceControlReadiness` to consume tracking geometry instead of old lateral-error authority and add focused readiness tests.
- [x] 2.4 Update `SteeringYawController` to consume tracking geometry and expose lateral, heading, curvature, and final turn terms; add focused yaw-controller tests.

## 3. Vertical Slice: Parameter And Evidence Surface

- [x] 3.1 Add BEV control-model params for lateral offset gain, heading error gain, curvature gain, and tracking fit minimum samples in port/config/platform parsing and defaults.
- [x] 3.2 Publish the new parameter surface in steering media config snapshots and update parameter/default tests.
- [x] 3.3 Update `new/config/default_params.md` so the human-readable parameter docs describe the new V6 control-model keys and the legacy alias semantics.
- [x] 3.4 Add `tracking_geometry` and yaw term decomposition to control debug snapshot/reporting, steering media, assistant protocol, and scene overlay probe.
- [x] 3.5 Update steering media selftest and probe baseline tests to require the new public fields.

## 4. Verification And Review

- [x] 4.1 Run focused local feedback loops for tracking geometry, readiness, yaw controller, parameter defaults, media selftest, scene overlay probe, and a local build or no-upload debug build.
- [x] 4.2 [Checkpoint] Run docs-first verifier-subagent review for `proposal.md`, `design.md`, `tasks.md`, and `specs/**/*.md` using `verify-sequence/default`. Use the verification contract above for field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Require `review_goal=implementation_correctness`. Write authoritative findings JSON and verifier evidence JSON under `review/review-runs/add-reference-tracking-geometry-v6/`, and require caller/orchestrator-maintained `agent-table.json`. Follow `cycle_rules` for agent lifecycle. Require fields from `verifier_evidence_required`, enforce valid-pass requirements, and require explicit `scope` for any partial verification.
- [x] 4.3 [Checkpoint] Run source-first verifier-subagent review for changed implementation, tests, config, and directly impacted code using `verify-sequence/default`. Use the same shared field groups, require authoritative findings JSON, verifier evidence JSON, current-state-only `agent-table.json`, subject binding through `subject_required_any_of`, and findings routing semantics from `findings_required / finding_object_required / finding_semantics / repair_routing_rules`.
- [x] 4.4 After the first source-first pass, run the requested extra verifier pass on the same implementation surface; automatically repair any blocking finding and rerun until it passes.

## 5. Cleanup And Lifecycle

- [x] 5.1 Update task checkboxes and remove or absorb any temporary prototype artifacts.
- [x] 5.2 Sync delta specs into `openspec/specs/` after implementation verification passes.
- [x] 5.3 Archive `add-reference-tracking-geometry-v6` after sync and record final evidence paths in the completion summary.
