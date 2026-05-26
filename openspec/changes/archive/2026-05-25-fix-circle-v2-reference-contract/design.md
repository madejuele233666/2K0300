## Context

`new/docs/visual-element-sparse-circle-v2.zh-CN.md` defines `CircleV2Scene` as the circle scene interpreter. It reads a narrow `SceneFrameView`, advances its FSM through event/reducer logic, observes role-specific geometry, and emits an optional `CircleV2ReferencePlan`.

The current implementation preserves most of that architecture, but `CircleV2GeometryObserver` marks `InnerTrace` geometry available when `present_count > 0`. `CircleV2ReferenceComposer` then offsets whatever sparse samples exist. This allows a one-point or gapped path to become a `CircleV2ReferencePlan`; the adapter wraps it as a high-priority circle candidate, and only later reference-usability logic may reject it and fall back to hold.

That behavior is a contract leak:

- Geometry availability is decided too weakly.
- Composer is asked to compose paths that are not complete enough to be a path.
- Adapter/arbitration receives malformed special candidates.
- Debug output can report `circle_v2_inner` selection while final reference falls back to hold.

## Goals / Non-Goals

**Goals:**

- Make `CircleV2GeometryObserver` responsible for the minimum structural validity of role-specific edge geometry.
- Require Circle V2 reference geometry to include a contiguous leading path segment before `reference_plan` is present.
- Preserve the existing FSM: missing geometry still yields `reference_plan = nullopt` and must not reset or roll back phase.
- Keep `CircleV2ReferenceComposer` simple: offset available geometry and return a plan; no state transition, no arbitration knowledge.
- Keep `VisualReferenceAdapter` simple: wrap present plans; do not repair, score, or validate sparse geometry.

**Non-Goals:**

- No new cooldown, timeout, or fallback phase.
- No rewrite of Phase1 circle detection.
- No new public facts API such as `inner_edge_valid` or `leading_samples`.
- No control-loop actuator redesign.
- No board tuning or parameter search.

## Decisions

### Decision 1: Geometry Availability Means Leading-Contiguous Edge Geometry

- Problem: `present_count > 0` permits one-point and gapped `InnerTrace` paths.
- Choice: role-specific geometry is available only when the edge path has a leading contiguous segment with enough finite samples to be a plausible reference path.
- Stack Equivalent: reuse the same leading-sample idea as `EvaluateReferenceUsability`, but keep it local to Circle V2 geometry so malformed circle plans never reach candidate arbitration.
- Boundary: GeometryObserver may inspect sample presence, finite coordinates, and leading contiguity. It may not call visual arbitration, reference hold, control readiness, or actuator logic.
- Failure Semantics: insufficient or gapped geometry returns `geometry.available = false`; reducer state remains authoritative and unchanged by the geometry failure.
- Verification Hook: tests for one-point inner geometry, gapped inner geometry, and contiguous inner geometry.

### Decision 2: Composer And Adapter Stay Thin

- Problem: adding repairs in composer or adapter would make those layers know too much about geometry quality.
- Choice: composer offsets only `geometry.available == true`; adapter wraps only present `CircleV2ReferencePlan`.
- Stack Equivalent: `CircleV2GeometryObserver` owns path availability, `CircleV2ReferenceComposer` owns offsetting, `CircleV2ReferenceAdapter` owns packaging.
- Boundary: adapter does not infer confidence or try to make an incomplete plan usable. Existing `VisualReferenceCandidate` still has a confidence field, but Circle V2 does not use confidence to express internal geometry quality.
- Failure Semantics: no reference plan means no circle candidate appended for that frame.
- Verification Hook: adapter tests confirm no candidate for absent plan and fixed mapping for present plans.

### Decision 3: Telemetry Preserves State Semantics

- Problem: geometry unavailability is useful, but it must not imply a reducer reset or hidden phase transition.
- Choice: keep `GeometryUnavailable` as observability only; it does not change `next_memory`.
- Stack Equivalent: event/reducer output remains the state authority; geometry status affects only `reference_plan` presence and telemetry.
- Boundary: telemetry may report geometry absence, but the FSM transition set remains exactly `Idle -> Approach -> InnerTrace -> ExitTrace -> Idle`.
- Verification Hook: tests assert active `InnerTrace` memory survives unavailable geometry while no plan is produced.

## Engineering Discipline

- Primary reference: `new/docs/visual-element-sparse-circle-v2.zh-CN.md`.
- Main spec: `openspec/specs/sparse-circle-v2-scene/spec.md`.
- Primary feedback loop: focused Circle V2 scene test, visual-reference orchestration regression where relevant, build/test script, then source-first OpenSpec verification.
- Implementation style: minimal local helpers, no public API expansion, no defensive fallback chain.

## Independent Verification Plan

- Docs-first: validate proposal/design/spec/tasks against the ai-enforced workflow.
- Source-first pass 1: review changed Circle V2 implementation and focused tests for contract correctness.
- Source-first pass 2: rerun the verifier after any repairs or, if pass 1 is clean, run a second independent source-first review over the same final diff.
- Required local evidence: focused Circle V2 test, relevant visual-reference test if touched, and `git diff --check`.
