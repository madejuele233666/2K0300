## Context

Circle V2 currently has a clean scene pipeline: events, reducer, geometry, and composer. The weak point is `InnerTrace` geometry: it waits for a visible inner edge and can withhold or shorten the circle reference during entrance frames. The V3 design keeps the same scene and FSM but changes the `InnerTrace` geometry source to a fixed-slope entrance guide based on the locked-direction outer entrance corner estimate.

## Goals / Non-Goals

**Goals:**

- Generate a usable `InnerTrace` reference before the inner circle edge is sufficiently visible.
- Reuse one internal side-expansion observation for Phase1 cue, Approach entry gate, and `P_est`.
- Make the fixed entrance slopes runtime parameters with explicit BEV `dx/dy` semantics.
- Keep `CircleV2Reducer`, adapter, ordinary road builder, and arbitration decoupled from V3 geometry internals.

**Non-Goals:**

- No new FSM phase, cooldown state, or adapter scoring.
- No rear-black / side-rear-black rule.
- No full-raster dependency or global mutation of sparse rows / ordinary road model.
- No board-side autonomous tuning of the slope values in this change.

## Decisions

### Decision 1: Make expansion observation a Circle scene internal helper

- **Problem:** Phase1 cue, Approach entry gate, and `P_est` all need locked-side expansion, but duplicated searches would drift.
- **Alternatives considered:** Keep legacy `DetectCircleElementEvidence()` as the cue source; expose expansion facts in `SceneFrameView`; create a private helper.
- **Chosen option:** Add `detail::ObserveCircleSideExpansion(...)` and pass its result to event and geometry observers.
- **Stack Equivalent:** A private feature extractor under the scene, not a public facts builder.
- **Named Deliverables:** `CircleSideExpansionObservation`, event observer callsite, geometry observer callsite.
- **Failure Semantics:** If expansion observation lacks a usable component, events remain false and geometry is unavailable; reducer memory is not reset by geometry absence.
- **Boundary Examples:** `SceneFrameView` does not gain `left_open`, `right_open`, `P_est`, or expansion fields.
- **Contrast Structure:** This reuses row/expansion geometry, not the final legacy evidence result, because final evidence loses component location.
- **Side Observation Rule:** A side-specific reach, growth, or straight-baseline calculation consumes only rows whose requested-side boundary is actually on that side. A widest interval that jumps to the opposite side is missing for that side, not a valid zero-reach sample.
- **Verification Hook:** Host `steering_circle_v2_scene_test` covers Phase1 parity and `P_est` behavior. On board, `circle_v2.frame_phase`, `circle_v2.reason`, and candidate path snapshots in steering media show cue/gate/reference behavior.

### Decision 2: Generate InnerTrace from `P_est + fixed_slope`

- **Problem:** Visible inner-edge following can be too short at entrance.
- **Alternatives considered:** Continue nearest inner-edge following; connect `P` to a near opposite anchor; use fixed slope through `P`.
- **Chosen option:** Estimate `P_est`, construct `x = P.x + slope_dx_dy * (y - P.y)`, then offset the virtual opposite boundary by road half width toward the locked direction.
- **Stack Equivalent:** A scene-owned virtual boundary generator, equivalent to replacing only the local source edge used to compose the reference.
- **Named Deliverables:** V3 `InnerTrace` branch in geometry observer, `reference_offset_m` in `CircleV2Geometry`, composer offset simplification.
- **Failure Semantics:** Missing `P_est`, invalid slope, or insufficient finite leading samples yields no reference plan for the frame.
- **Sampling Rule:** Use leading finite `ordinary_road.center_path.sampled_path` `forward_m` values as virtual-boundary `y` coordinates, starting at index `0` and stopping at the first absent or non-finite center sample. Do not use later samples to fill gaps.
- **Boundary Examples:** ExitTrace still uses current outer-edge logic; adapter still wraps only present plans.
- **Contrast Structure:** The virtual boundary is not an inner edge, not a shifted outer circle, and not a repaired candidate.
- **Verification Hook:** Unit tests assert left/right mirror behavior and that `P.x` uses the baseline rather than expanded observed edge. On board, compare `circle_v2_inner` media path with entrance frames from `verification/cross - 副本.jpg` style captures.

### Decision 3: Put fixed slopes in runtime parameters

- **Problem:** Slope values are track/calibration dependent and must be tunable without code edits.
- **Alternatives considered:** Internal constants; angle-in-degrees parameters; `dx/dy` parameters.
- **Chosen option:** Add `CIRCLE_V2_ENTRY_FIXED_SLOPE_LEFT_DX_DY` and `CIRCLE_V2_ENTRY_FIXED_SLOPE_RIGHT_DX_DY` under `BEV_ELEMENT`.
- **Stack Equivalent:** A small numeric control surface next to Circle V2 yaw/hold parameters.
- **Named Deliverables:** parameter structs, param store read/validate, default JSON/docs, media snapshot fields, parameter tests.
- **Failure Semantics:** Left slope must be finite negative, right slope finite positive, both within absolute value `10.0`; invalid values follow existing parse-failure fallback.
- **Boundary Examples:** `CIRCLE_V2_ENABLED` remains a composition switch; slopes do not enter reducer logic.
- **Contrast Structure:** `dx/dy` avoids an angle conversion layer and matches path sampling where `y` is forward distance.
- **Verification Hook:** Parameter load/default tests cover parse and fallback. On board, config snapshots and steering media parameter headers expose active slope values.

## Engineering Discipline

- Principles reference:
  `openspec/schemas/ai-enforced-workflow/engineering-principles.md`
- Domain language / ADRs consulted: existing `sparse-circle-v2-scene` spec and `new/docs/visual-element-sparse-circle-v3.zh-CN.md`.
- Primary feedback loop: host unit tests for scene behavior and parameter parsing, then source-first verifier review.
- Prototype question: whether `P_est + fixed_slope` can replace InnerTrace inner-edge following without leaking new facts outside CircleV2Scene.
- Hard dependencies:
  `verify-sequence/default`, authoritative findings/evidence, valid-pass requirements, subject binding, and current-state `agent-table.json`.
- Soft dependencies:
  captured roundabout media for later tuning; not required for this implementation gate.

## Independent Verification Plan (STANDARD/STRICT)

Document verification using shared sequence `verify-sequence/default` from:
`openspec/schemas/ai-enforced-workflow/verification-sequence.md`
and shared verification-cycle contracts:

- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`

Stage A flow:

- docs-first checkpoints use changed `proposal/specs/design/tasks` as the primary surface
- source-first checkpoints use changed code, tests, and directly impacted code as the primary surface
- approved docs remain reference material when source-first review runs
- verification continues a usable `active` agent first; if none exists, the orchestrator spawns one
- only `block -> pass` marks an agent `non_active`
- termination depends only on a valid `active` pass

Runtime profile policy:

- Use verifier runtime profile from
  `openspec/schemas/ai-enforced-workflow/agents/verify-reviewer.toml`.

Loop rule:

- an `active` agent that reports `block` stays authoritative until that same agent returns `pass`
- `agent-table.json` stays current-state-only; recovery lives in `continuation_probe`
- valid `pass` requires `review_coverage.coverage_status=complete` and `review_coverage.exhaustive=true`
- only the main orchestrator may authorize resume/spawn/repair/terminate, and it must not substitute its own judgment for verifier output

Shared field groups from `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`:

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
- Default loop behavior: resume active first, repair on blocking findings, terminate only on valid active pass
- Authoritative verifier-subagent findings JSON path:
  `openspec/changes/add-circle-v3-fixed-slope-entry-guide/verification/source-first/attempt-N/findings.json`
- Verifier execution evidence JSON path:
  `openspec/changes/add-circle-v3-fixed-slope-entry-guide/verification/source-first/attempt-N/verifier-evidence.json`
- Agent table path:
  `openspec/changes/add-circle-v3-fixed-slope-entry-guide/verification/source-first/agent-table.json`
- Continuation target on pass: sync specs, validate main spec, archive change

Checkpoint-specific primary surfaces:

- artifact-completion docs-first review: changed `proposal/specs/design/tasks`
- active-change source-first review: changed code, changed tests, directly impacted code

## Migration Plan

1. Add the OpenSpec delta and validate it.
2. Implement parameter plumbing and V3 scene internals with host tests.
3. Run local tests and source-first OpenSpec verification to convergence.
4. Sync the delta spec into the main `sparse-circle-v2-scene` spec and archive the change.
5. Board smoke-test after implementation: deploy normal runtime, record a short disarmed/low-speed camera run, and verify media/header reports the configured slope values plus `circle_v2_inner` path behavior during entrance frames.

## Open Questions

None. The slope coordinate system, defaults, and `P.x` baseline source are fixed for this change.

## Risks / Trade-offs

- The default slopes are starting values for track tuning, not proven optimal on board.
- Moving Phase1 cue to the shared expansion helper risks behavior drift; golden parity tests mitigate this.
- `P_est` from sparse rows is approximate; geometry absence must withhold the plan rather than synthesize one.
