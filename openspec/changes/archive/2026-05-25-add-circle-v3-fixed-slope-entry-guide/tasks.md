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
- Artifact-completion gate ownership:
  - this task list completes the schema's `applyRequires` set
  - the artifact-creation caller owns docs-first review before implementation entry
  - `openspec-apply-change` does not own that docs-first artifact gate

## 1. Vertical Slice: Fixed-slope parameters reach CircleV2Scene

- [x] 1.1 Add left/right `dx/dy` entry fixed slopes to runtime parameter types, `CircleV2Params`, default JSON/docs, media snapshot, parser, and validator.
- [x] 1.2 Pass the parsed slope values through `BuildCircleV2Params()` into `CircleV2Scene::Step()`.
- [x] 1.3 Add focused parameter tests for explicit parse, default parity, and invalid slope fallback behavior.

## 2. Vertical Slice: Shared expansion observation

- [x] 2.1 Add a private Circle scene expansion observer that extracts widest-row side boundaries, side reach, straight baseline, expanded components, and a `P_est` candidate.
- [x] 2.2 Update event observer to derive Phase1 cue and Approach entry gate from the shared expansion observation while preserving old Phase1 left/right/none parity.
- [x] 2.3 Add scene tests for Phase1 parity and locked-direction entry-gate behavior through the new shared observation.

## 3. Vertical Slice: V3 InnerTrace geometry

- [x] 3.1 Update `CircleV2Scene::Step()` and internal detail interfaces so event and geometry observers consume the same expansion observation.
- [x] 3.2 Update `CircleV2Geometry` / composer so geometry supplies the reference offset instead of composer guessing offset by role.
- [x] 3.3 Replace `InnerTrace` geometry with `P_est + fixed_slope` virtual opposite boundary sampling; keep `ExitTrace` outer-edge behavior unchanged.
- [x] 3.4 Add scene tests for left/right V3 virtual boundary generation, baseline-derived `P.x`, geometry-unavailable behavior, and unchanged ExitTrace behavior.

## 4. Verification and Review

- [x] 4.1 Run focused host tests: CircleV2 scene, visual-reference orchestration, parameter load, and runtime parameter defaults.
- [x] 4.2 Run `openspec validate add-circle-v3-fixed-slope-entry-guide --strict` and record the result.
- [x] 4.3 [Checkpoint] Run verifier-subagent review for implementation correctness using `verify-sequence/default`. Use the verification contract above for field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Require `review_goal=implementation_correctness`. Write authoritative findings JSON and verifier evidence JSON under `openspec/changes/add-circle-v3-fixed-slope-entry-guide/verification/source-first/attempt-N/`; the caller/orchestrator reconciles and writes `agent-table.json`. Follow `cycle_rules` for agent lifecycle. Require fields from `verifier_evidence_required`, enforce valid-pass requirements, and require explicit `scope` for any partial verification.
- [x] 4.4 Plan board smoke-test: deploy runtime, capture low-speed/disarmed steering media around circle entrance, and confirm parameter snapshot plus `circle_v2_inner` path behavior. Board execution remains a later hardware step; source-first evidence records the remote SSH/transfer instability and does not claim a board run.

## 5. Cleanup and Decision Capture

- [x] 5.1 Update `new/docs/visual-element-sparse-circle-v3.zh-CN.md` if implementation names or parameter semantics differ from the current V3 note.
- [x] 5.2 Sync the delta spec into `openspec/specs/sparse-circle-v2-scene/spec.md`, validate the main spec, and archive the completed change.
