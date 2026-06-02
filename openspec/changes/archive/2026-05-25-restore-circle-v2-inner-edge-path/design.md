## Context

The current Circle V2 runtime has a clean scene split, but `InnerTrace` path generation was changed to a V3 entrance guide:

```text
CircleV2Scene -> boundary override intent
SteeringFramePerceptionPipeline -> ordinary path builder with patched rows
VisualReferenceAdapter -> circle_v2_inner candidate
```

That kept concerns separated, but it moved the active `InnerTrace` path away from direct circle geometry. The requested behavior is to return to a simpler scene-owned reference: observe the locked-side inner circle edge and compose a path that may run close to that edge. The decoupled scene structure remains useful and should be kept.

## Goals / Non-Goals

**Goals:**

- Make `InnerTrace` produce a direct `CircleV2ReferencePlan` from the locked-side inner edge.
- Keep FSM, events, geometry observation, reference composition, and candidate adaptation mutually unaware beyond their existing contracts.
- Archive the current P-point fixed-slope boundary override implementation under `new/code/archive/` and remove it from active CMake/runtime surfaces.
- Remove active CircleV2 fixed-slope entry parameters if no active runtime code consumes them.
- Preserve `ExitTrace`, yaw exit, yaw-stall fallback, and existing CircleV2 state semantics.

**Non-Goals:**

- No board upload, board restart, or board smoke run in this change unless explicitly requested later.
- No redesign of Phase1 circle cue, Approach entry gate, yaw integration, or control safety gates.
- No attempt to reintroduce rear-black entry detection.
- No ordinary path-builder rewrite.

## Decisions

### Decision 1: InnerTrace emits a direct scene-owned reference plan

Problem: The active V3 boundary override means `InnerTrace` depends on `SteeringFramePerceptionPipeline` to turn a scene boundary override into a candidate. That makes the final inner path depend on ordinary row-patching behavior instead of the circle scene's own role-specific geometry.

Chosen approach: `CircleV2GeometryObserver` observes the locked-side inner edge for `InnerTrace`, and `CircleV2ReferenceComposer` emits a `CircleV2ReferencePlan` directly. The path may stay on or very near the observed inner edge; no half-width offset is required for `InnerTrace`.

Alternatives considered:

- Keep V3 boundary override active: preserves ordinary-builder validation, but keeps the path tied to P estimation and virtual opposite-boundary patching.
- Generate an offset from the inner edge by road half width: closer to earlier "center path" thinking, but the current request explicitly allows a path贴着内圆边线, and offsetting can push samples into black or away from the intended guide.
- Use ordinary path builder after patching rows: rejected for active behavior because it reintroduces the P-point补线 strategy being retired.

Stack Equivalent:

- Scene-owned reference intent = `CircleV2ReferencePlan`.
- Inner-edge observation = `CircleV2Geometry.edge_path` with role `kInnerTrace`.
- Candidate packaging = `AdaptCircleV2ReferencePlan()`.

Named Deliverables:

- `new/code/runtime/detail/steering_circle_v2_geometry_observer.cpp`
- `new/code/runtime/detail/steering_circle_v2_composer.cpp`
- `new/code/runtime/steering_circle_v2_reference_adapter.cpp`
- `new/code/runtime/steering_frame_perception_pipeline.cpp`
- `new/verification/tests/steering_circle_v2_scene_test.cpp`

Failure Semantics:

- If the inner edge cannot produce a finite leading-contiguous segment, geometry is unavailable and `reference_plan` is empty.
- Geometry absence does not mutate the FSM; only reducer events change state.
- The adapter does not repair missing paths.

Boundary Examples:

- `InnerTrace + left`: find the left-side inner edge and emit those samples as `circle_v2_inner`.
- `InnerTrace + right`: find the right-side inner edge and emit those samples as `circle_v2_inner`.
- `Idle` / `Approach`: no candidate.

Verification Hook:

- Unit tests assert `InnerTrace` produces a `CircleV2ReferencePlan`, not a boundary override.
- Pipeline tests or existing scene tests assert `circle_v2_inner` can be adapted directly from a scene plan.
- Board hook, when allowed: no-motion steering log should show `visual_reference.source=circle_v2_inner` without requiring boundary-override reason strings.

Feedback Loop:

- Local scene tests prove the role contract.
- `git grep` / tests prove active code no longer calls the boundary-override builder for CircleV2.

### Decision 2: Keep the scene split, remove only the retired active path

Problem: Rolling back path generation must not collapse observer, reducer, composer, adapter, and pipeline into a single all-knowing block.

Chosen approach: keep the existing split:

```text
ObserveCircleV2Events -> ReduceCircleV2 -> ObserveCircleV2Geometry
-> ComposeCircleV2Reference -> AdaptCircleV2ReferencePlan
```

`InnerTrace` changes only the role-specific geometry/composition. FSM remains unaware of how paths are built.

Alternatives considered:

- Let the FSM choose inner-edge vs fixed-slope mode: rejected because it violates mutual unawareness; phase flow should not own path strategy.
- Let the adapter build the inner-edge path: rejected because the adapter is a packaging boundary, not geometry logic.
- Keep both active strategies behind a runtime switch: rejected as unnecessary complexity and parameter surface growth.

Stack Equivalent:

- Event boundary = `CircleV2Events`.
- State boundary = `CircleV2Decision`.
- Geometry boundary = `CircleV2Geometry`.
- Packaging boundary = `VisualReferenceCandidate`.

Named Deliverables:

- Updated internal CircleV2 detail types only where needed.
- Removed active `CircleV2BoundaryOverridePlan` call path from the frame pipeline.
- Tests that fail if `InnerTrace` depends on boundary override.

Failure Semantics:

- Missing geometry returns `std::nullopt`.
- No fallback to P-point补线.
- No phase reset from geometry failures.

Boundary Examples:

- `CircleV2EventObserver` may know yaw and expansion events but not candidate sources.
- `CircleV2ReferenceAdapter` may know source strings but not how inner edges are found.

Verification Hook:

- Build/tests plus targeted text search for active `BuildReferencePathWithBoundaryOverride` usage.

Feedback Loop:

- Local tests catch accidental coupling.
- Source-first review checks role isolation.

### Decision 3: Archive P-point boundary override as historical code only

Problem: The current P-point补线 implementation should not remain active, but deleting it entirely loses the rationale and a useful comparison point.

Chosen approach: copy the retired implementation into `new/code/archive/circle_v2_v3_fixed_slope_entry_guide/` with a README that states it is historical and excluded from active build/runtime. Remove active includes, CMake entries, and runtime call sites.

Alternatives considered:

- Leave active files unused in `runtime/`: rejected because unused active files invite accidental reuse and confuse ownership.
- Delete everything: simpler, but does not satisfy the explicit request to archive the current P-point補線 code.

Stack Equivalent:

- Archive boundary = `new/code/archive/...`.
- Active build boundary = `new/user/CMakeLists.txt` and runtime includes.

Named Deliverables:

- `new/code/archive/circle_v2_v3_fixed_slope_entry_guide/README.md`
- Archived copies of boundary-override implementation files.
- Active runtime and CMake cleanup.

Failure Semantics:

- Archive files are not compiled, not included, and not referenced by active tests.
- If someone needs the old V3 behavior later, it must return through a new change/spec, not by including archive files.

Boundary Examples:

- Allowed: comments in archive README explaining historical context.
- Not allowed: `#include "runtime/steering_boundary_override_reference.hpp"` in active pipeline after this change.

Verification Hook:

- `rg` confirms active `new/code/runtime` no longer references `BoundaryOverride`.
- Build confirms no active compilation dependency remains.

Feedback Loop:

- `git diff` and CMake build expose stale references immediately.

### Decision 4: Retire active fixed-slope parameters

Problem: `CIRCLE_V2_ENTRY_FIXED_SLOPE_LEFT_DX_DY` and `CIRCLE_V2_ENTRY_FIXED_SLOPE_RIGHT_DX_DY` are only meaningful for the V3 P-point補線 behavior.

Chosen approach: remove them from active `CircleV2Params`, runtime parameter types, defaults, docs, parser tests, and protocol snapshots unless another active user exists.

Alternatives considered:

- Keep parameters as no-ops: rejected because no-op parameters create misleading tuning surface.
- Keep parser only for backward compatibility: rejected because the user explicitly does not want compatibility to preserve deprecated behavior.

Stack Equivalent:

- Runtime parameter surface = `BEVElementParameters`, `param_store`, `default_params.json`, `default_params.md`, and runtime parameter tests.

Named Deliverables:

- Config and parser cleanup.
- Tests updated to remove fixed-slope expectations.

Failure Semantics:

- Old JSON keys are ignored only if the existing parser naturally ignores unknown keys; active docs no longer advertise them.

Verification Hook:

- Runtime parameter default/load tests compile and pass without fixed-slope fields.

Feedback Loop:

- Tests and compiler errors identify any stale parameter consumer.

## Engineering Discipline

- Principles reference:
  `openspec/schemas/ai-enforced-workflow/engineering-principles.md`
- Domain language / ADRs consulted:
  `openspec/specs/sparse-circle-v2-scene/spec.md`, `new/docs/visual-element-sparse-circle-v2.zh-CN.md`, and `new/docs/visual-element-sparse-circle-v3.zh-CN.md`.
- Primary feedback loop:
  local CircleV2 scene tests, runtime parameter tests, steering media serialization tests, and source search for retired boundary-override usage.
- Prototype question, if any:
  no new prototype is required; the path strategy is explicit: direct inner-edge samples are accepted for `InnerTrace`.
- Hard dependencies:
  `verify-sequence/default`, authoritative findings/evidence, valid-pass requirements, subject binding, and current-state `agent-table.json`.
- Soft dependencies:
  glossary, ADRs, architecture heuristics, and prototype notes; use them when present, but do not create auxiliary review gates for them.

## Independent Verification Plan (STANDARD/STRICT)

Document verification using shared sequence `verify-sequence/default` from:
`openspec/schemas/ai-enforced-workflow/verification-sequence.md`
and shared verification-cycle contracts:

- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`

Runtime profile policy:

- Use verifier runtime profile from
  `openspec/schemas/ai-enforced-workflow/agents/verify-reviewer.toml`.

Loop rule:

- an `active` agent that reports `block` stays authoritative until that same agent returns `pass`
- `agent-table.json` stays current-state-only; recovery lives in `continuation_probe`
- valid `pass` requires `review_coverage.coverage_status=complete` and `review_coverage.exhaustive=true`
- partial verification requires explicit `review_scope.scope`
- only the main orchestrator may authorize resume/spawn/repair/terminate, and it must not substitute its own judgment for verifier output

Shared field groups from `verification-cycle-core-v1.json` and
`verification-cycle-openspec-adapter-v1.json`:

- `invocation_common_required`
- `output_paths_required`
- `verifier_evidence_required`
- `valid_pass_requirements`
- `partial_scope_rule`

Review completion contract:

- execution evidence MUST record `review_goal`, `review_phase`, `review_scope`, `review_coverage`, `reviewed_paths`, `skipped_paths`, `reviewed_axes`, and `unreviewed_axes`
- each checkpoint MUST maintain `agent-table.json`

### Review Checkpoints

- Shared sequence reference: `verify-sequence/default`
- Review goal: `implementation_correctness`
- Verifier agent path:
  `openspec/schemas/ai-enforced-workflow/agents/verify-reviewer.toml`
- Invocation template id: `verify-reviewer-inline-v3`
- Default loop behavior:
  - resume `active` first
  - prefer `send_input` while that same `active` agent is still open
  - use `continuation_probe` to distinguish resume from dedicated recovery spawn
  - spawn when no usable `active` agent exists
  - repair follows `block`
  - only `block -> pass` marks `non_active`
  - final termination requires a valid `active` pass
- Authoritative verifier-subagent findings JSON path:
  `openspec/changes/restore-circle-v2-inner-edge-path/verification/artifact-findings.json`
- Verifier execution evidence JSON path:
  `openspec/changes/restore-circle-v2-inner-edge-path/verification/artifact-evidence.json`
- Agent table path:
  `openspec/changes/restore-circle-v2-inner-edge-path/verification/agent-table.json`
- Continuation target on pass:
  apply implementation, then source-first verification.

Checkpoint-specific primary surfaces:

- artifact-completion docs-first review: changed `proposal/specs/design/tasks`
- active-change source-first review: changed CircleV2 runtime code, config/docs cleanup, archive files, and focused tests

## Migration Plan

1. Archive current V3 boundary-override implementation under `new/code/archive/circle_v2_v3_fixed_slope_entry_guide/`.
2. Remove active boundary-override files from CMake/runtime path.
3. Change `InnerTrace` geometry to observe the locked-side inner edge.
4. Change composer/adapter/pipeline so `InnerTrace` uses `CircleV2ReferencePlan` directly.
5. Remove active fixed-slope parameter surface if no active code uses it.
6. Update tests and specs.
7. Run local verification only; do not touch the board during this change unless the user explicitly asks.

## Open Questions

- None blocking. The accepted path behavior is that `InnerTrace` may follow the inner edge closely rather than offsetting to the road center.

## Risks / Trade-offs

- A path close to the inner edge may be more aggressive than a centerline-like path. This is intentional for the requested rollback and should be tuned through geometry observation rather than hidden offsets.
- Removing the active fixed-slope parameters simplifies the runtime surface but requires historical users to consult archive code rather than live defaults.
- Not running a board smoke means this change closes on local build/test evidence only until board access is explicitly requested.
