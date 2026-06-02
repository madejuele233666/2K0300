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
  - the artifact-creation caller owns docs-first review before implementation entry
  - `openspec-apply-change` does not own that docs-first artifact gate

## 1. Vertical Slice: InnerTrace Direct Inner-Edge Plan

- [x] 1.1 Archive the current V3 P-point fixed-slope boundary-override implementation under `new/code/archive/circle_v2_v3_fixed_slope_entry_guide/` with a README that marks it historical and inactive.
- [x] 1.2 Remove active CircleV2 boundary-override plumbing from `CircleV2StepResult`, the composer, adapter, and frame perception pipeline.
- [x] 1.3 Change `CircleV2GeometryObserver` so `InnerTrace` observes the locked-side inner edge and `ComposeCircleV2Reference()` emits an `InnerTrace` `CircleV2ReferencePlan`.
- [x] 1.4 Update `AdaptCircleV2ReferencePlan()` so both `InnerTrace` and `ExitTrace` scene plans are packaged, while the adapter remains confidence-free and does not repair geometry.
- [x] 1.5 Run focused CircleV2 scene tests to prove `InnerTrace` emits a direct plan and no boundary override is needed.

## 2. Vertical Slice: Remove Retired Fixed-Slope Runtime Surface

- [x] 2.1 Remove active fixed-slope fields from `CircleV2Params`, runtime parameter types, parser, default JSON, default parameter docs, and protocol/default tests if no active consumer remains.
- [x] 2.2 Update V2/V3 docs so the retired P-point补线 approach is recorded as historical and the active behavior is inner-edge `InnerTrace` path generation.
- [x] 2.3 Run runtime parameter defaults and steering media serialization tests; compile the runtime parameter load test locally without executing its board scp/ssh runner.

## 3. Verification and Review

- [x] 3.1 Run `openspec validate restore-circle-v2-inner-edge-path --strict` for artifact/schema validity.
- [x] 3.2 Run focused local tests: CircleV2 scene, visual reference orchestration, BEV simple perception, steering media selftest, runtime parameter defaults, runtime parameter load compile-only, and scene overlay authority baseline.
- [x] 3.3 Run `git diff --check`.
- [x] 3.4 [Checkpoint] Record a local source-first implementation review using `verify-sequence/default` scope fields. Evidence is stored in this archived change's `verification/implementation-findings.json` and `verification/implementation-evidence.json`. A verifier subagent was not spawned because the active tool policy requires an explicit user request for subagents or delegation.
- [x] 3.5 Record that board upload/smoke is intentionally not executed for this change because the user explicitly requested not to touch the board; board no-motion smoke remains the first hardware follow-up when allowed.

## 4. Sync and Archive

- [x] 4.1 Sync the delta spec into `openspec/specs/sparse-circle-v2-scene/spec.md`.
- [x] 4.2 Archive the completed change under `openspec/changes/archive/2026-05-25-restore-circle-v2-inner-edge-path/`.
