## Context

The current sparse BEV ordinary reference path is built in `new/code/legacy/steering_bev_simple_perception.cpp` by selecting a white interval per row and using `interval.center_m`. That works only when both endpoints are real visible road boundaries. In one-side-lost frames, an endpoint can be the sampleable range edge or an opening, so midpoint selection turns a visibility artifact into a road-center fact.

The V4 design documents define the target contract:

- ordinary reference owns row-interval to current-frame center interpretation;
- reference continuity owns hold-last;
- CircleV2 and cross do not learn ordinary lost-line internals;
- a neutral single-boundary helper owns only BEV geometry offset from a boundary trace.

## Goals / Non-Goals

**Goals:**

- Add a reusable single-boundary normal-offset helper with no scene/FSM/arbitration dependencies.
- Change ordinary BEV reference extraction to interpret low/high endpoint visibility before selecting center candidates.
- Preserve strict leading extraction and existing hold-last ownership.
- Use the helper for CircleV2 InnerTrace and ExitTrace single-boundary path composition.
- Add focused tests that prove midpoint, one-side-lost, helper geometry, strict-leading, and CircleV2 reuse behavior.

**Non-Goals:**

- No CircleV2 FSM transition changes.
- No cross evidence or visual element recognition changes.
- No visual-reference arbitration priority changes.
- No new runtime business parameter.
- No realtime road width re-estimation.
- No platform adapter or motor-control changes.

## Decisions

### Decision 1: Place the helper in the legacy reference geometry layer

**Problem:** Ordinary reference and CircleV2 both need the same geometry operation, but neither should depend on the other's detail namespace.

**Chosen approach:** Add neutral helper files under `new/code/legacy/`, for example `steering_single_boundary_offset.hpp/.cpp`. The helper consumes `port::BEVPoint` traces plus target forward samples and returns a `port::BEVReferencePath`-compatible leading path.

**Alternatives considered:**

- Put the helper under `runtime/detail/`: this would force ordinary reference to depend on CircleV2/runtime detail and violate ownership.
- Keep one formula in ordinary and another in CircleV2: this preserves local simplicity but guarantees semantic drift.

**Stack Equivalent:** C++17 free function in `legacy/` with `port::BEVPoint` / `port::BEVReferencePath` data, compiled into the same user/test targets that already build BEV perception.

**Named Deliverables:** `new/code/legacy/steering_single_boundary_offset.hpp`, `new/code/legacy/steering_single_boundary_offset.cpp`, helper unit tests.

**Failure Semantics:** If the trace cannot provide finite leading samples, the helper returns an insufficient path prefix. It does not fall back to slope zero, fixed lateral offset, history, or confidence scoring.

**Boundary Examples:** Ordinary passes low/high endpoint traces and nominal half width. CircleV2 passes inner/outer edge traces and its caller-owned signed offset. The helper does not read `CircleV2Memory`, element evidence, `RuntimeParameters`, or hold state.

**Contrast Structure:** The old CircleV2 composer applied `sample.point.lateral_m += offset`; the new helper applies offset along local boundary direction using the same `forward_m` resampling contract as ordinary reference.

**Verification Hook:** Helper tests exercise zero slope, nonzero slope, zero offset, positive/negative offset, leading stop, and direction-unavailable cases. On-board smoke hook is steering media path-candidate observation after deploying a build; it should show ordinary/circle reference points but no new telemetry dependency.

**Feedback Loop:** Local helper tests prove pure geometry; source-first verifier checks that no scene or arbitration dependency leaked into the helper.

### Decision 2: Ordinary extraction builds center candidates after endpoint visibility interpretation

**Problem:** Choosing intervals by raw midpoint before correction means the wrong midpoint can decide which interval survives.

**Chosen approach:** For each leading row, interpret every interval as:

- both visible: direct midpoint center candidate;
- low visible only: low-edge trace candidate through helper with positive nominal half-width;
- high visible only: high-edge trace candidate through helper with negative nominal half-width;
- neither visible: unavailable.

Then select the strict-leading trace across produced center candidates using both near-to-far leading continuity and same-frame adjacent-sample geometry continuity, preserving strict leading stop on the first unavailable row.

**Alternatives considered:**

- Keep `ChooseInterval()` and adjust the chosen center afterward: simpler patch, but interval selection remains poisoned by missing-side geometry.
- Add special circle or bend patches: narrower short-term behavior but violates V4's statement that this is an ordinary reference problem.

**Stack Equivalent:** Replace `ChooseInterval()`/`interval.center_m` use inside `ExtractStrictLeadingReferenceSegment()` with an ordinary-center candidate builder local to `steering_bev_simple_perception.cpp`.

**Named Deliverables:** Updated ordinary reference builder, focused `bev_simple_perception_test` cases for both-edge midpoint and one-side-lost interpretation.

**Failure Semantics:** If no center candidate is available at a leading row, current visual extraction stops. Existing reference continuity can later produce hold-last; ordinary extraction does not hold or predict.

**Boundary Examples:** `interval.left_m` maps to low lateral edge; `interval.right_m` maps to high lateral edge. The field names are not treated as physical left/right ownership.

**Contrast Structure:** V3/V4 docs reject "white interval midpoint always equals road center"; the new builder makes the endpoint visibility assumption explicit before outputting a center.

**Verification Hook:** Tests construct synthetic row scans where one endpoint touches `sampleable_left_m` or `sampleable_right_m`, and assert the resulting path follows normal-offset semantics and stops on double lost. On-board smoke hook is a no-motion steering-media capture from a one-side-lost placement to inspect ordinary reference points.

**Feedback Loop:** Local tests prove the behavior without requiring CircleV2 activation; a later track run can tune camera/geometry separately if needed.

### Decision 3: CircleV2 composes role paths through the same helper

**Problem:** CircleV2 currently observes edge paths, then applies lateral offsets in composer. That duplicates the one-boundary path formula and ignores local edge direction.

**Chosen approach:** Keep CircleV2 geometry observer responsible for role-specific edge selection. Move offset composition to the shared helper by passing:

- InnerTrace: locked-side inner edge trace and signed offset mapped from `CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M`;
- ExitTrace: role-specific outer edge trace and existing signed road-half-width offset.

**Alternatives considered:**

- Move helper call into geometry observer: makes geometry observation own path composition and couples geometry availability to composer output.
- Leave CircleV2 as direct lateral addition: least diff, but violates the helper reuse document and behaves differently from ordinary one-side repair on curves.

**Stack Equivalent:** `CircleV2Geometry` continues to carry `edge_path` and signed offset facts; `ComposeCircleV2Reference()` calls the neutral helper to create `reference_path`.

**Named Deliverables:** Updated `new/code/runtime/detail/steering_circle_v2_composer.cpp`, adapted CircleV2 scene tests.

**Failure Semantics:** If helper output is insufficient, composer returns no plan. Reducer memory remains authoritative and is not repaired by geometry/composition.

**Boundary Examples:** InnerTrace offset `0.0` returns the observed edge path. ExitTrace remains road-half-width from the selected outer edge, but now uses the boundary's local direction.

**Contrast Structure:** Scene still owns "which edge and which offset"; helper owns only "how to offset an edge trace."

**Verification Hook:** CircleV2 tests assert zero-offset inner path equals edge samples and nonzero offset follows helper semantics for a sloped edge. On-board smoke hook is a normal-mode run with steering media path candidate overlay if hardware is available and safe to drive.

**Feedback Loop:** Local scene tests detect path construction regression before any board run; steering media can later confirm runtime overlays.

### Decision 4: Keep output encoding stable

**Problem:** Renaming ordinary reference modes or candidate kinds would spread the change into arbitration and telemetry without improving the lost-line fix.

**Chosen approach:** Keep `ReferenceMode::kIntervalCenter` and source `simple_interval_center` for ordinary current-frame visual reference. If point-source debug detail is added, it remains debug-only and does not change candidate priority.

**Alternatives considered:**

- Add a new reference mode for boundary-offset points: clearer label, but downstream code may start treating it as a new arbitration category.
- Expose basis facts to element evidence: violates V4 scope and risks FSM coupling.

**Stack Equivalent:** Existing `port::BEVReferencePath` and `VisualReferenceCandidate` continue to flow through `SelectVisualReference()`.

**Named Deliverables:** No public type change required; tests verify selected reference mode/source remains ordinary-compatible.

**Failure Semantics:** No consumer may rely on mode/source to distinguish midpoint from helper-generated points.

**Boundary Examples:** `basis=low_edge_normal_offset` is an internal builder detail, not a candidate kind.

**Contrast Structure:** Debug facts may explain construction; arbitration still chooses references, not construction methods.

**Verification Hook:** Visual orchestration tests should not require changes. On-board smoke hook is unchanged assistant/steering media JSON shape for reference mode/source.

**Feedback Loop:** Build and existing orchestration tests catch accidental API changes.

### Decision 5: Suppress CircleV2 from public cross evidence, not only built cross candidates

**Problem:** V4 can intentionally make the ordinary line candidate unavailable when near rows are double-lost. In that state cross evidence may still be present, but the old composition guard that only looked for a built cross candidate no longer stops CircleV2.

**Chosen approach:** Keep cross recognition and arbitration unchanged, but update the composition guard so CircleV2 is not stepped when `element_evidence.cross_exit.present` is true and cross takeover is enabled. This remains outside `CircleV2Scene`; the scene still does not read cross evidence or make cross-specific FSM transitions.

**Alternatives considered:**

- Keep the old built-candidate guard: smaller diff, but active CircleV2 can keep advancing through a present cross if V4 ordinary reference correctly withholds the line candidate.
- Change cross candidate construction to work without a line candidate: larger arbitration-path change and outside V4 scope.

**Stack Equivalent:** Runtime composition predicate in `steering_frame_perception_pipeline.cpp` and matching probe predicate in `scene_overlay_probe.cpp`.

**Named Deliverables:** Updated composition guard and authority baseline coverage for cross evidence suppressing active CircleV2 when cross candidate construction reports `line_candidate_absent`.

**Failure Semantics:** Cross evidence suppresses CircleV2 stepping only when takeover is enabled. It does not fabricate a cross reference, and it does not change cross detector present/absent semantics.

**Boundary Examples:** The composition layer reads `element_result.evidence.cross_exit.present`; `CircleV2Scene` still receives no cross input.

**Contrast Structure:** This is lifecycle composition, not visual-reference arbitration and not cross detector behavior.

**Verification Hook:** `run_scene_overlay_probe_authority_baseline_test.sh` covers a warm active-circle state followed by a cross frame with present evidence but no built cross candidate.

**Feedback Loop:** Local scene overlay probe verifies CircleV2 memory resets to Idle while selected visual reference remains absent when no cross candidate can be built.

## Engineering Discipline

- Principles reference:
  `openspec/schemas/ai-enforced-workflow/engineering-principles.md`
- Domain language / ADRs consulted:
  `new/docs/visual-element-sparse-circle-v4.zh-CN.md`,
  `new/docs/visual-element-sparse-circle-v4-single-boundary-helper.zh-CN.md`,
  `openspec/specs/sparse-circle-v2-scene/spec.md`,
  `new/code/legacy/steering_bev_simple_perception.cpp`,
  `new/code/runtime/detail/steering_circle_v2_composer.cpp`
- Primary feedback loop:
  local focused C++ tests, then OpenSpec docs-first/source-first verifier passes.
- Prototype question, if any:
  none; the helper formula and ordinary mapping are specified by V4 documents.
- Hard dependencies:
  `verify-sequence/default`, authoritative findings/evidence, valid-pass
  requirements, subject binding, and current-state `agent-table.json`
- Soft dependencies:
  glossary, ADRs, architecture heuristics, and prototype notes; use them when
  present, but do not create auxiliary review gates for them

## Independent Verification Plan (STANDARD/STRICT)

Document verification using shared sequence `verify-sequence/default` from:
`openspec/schemas/ai-enforced-workflow/verification-sequence.md`
and shared verification-cycle contracts:

- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`

Stage A flow:

- checkpoints use the same `active/non_active` verification cycle
- docs-first checkpoints use changed `proposal/specs/design/tasks` as the primary surface
- source-first checkpoints use changed code, tests, and directly impacted code as the primary surface
- approved docs remain reference material when source-first review runs
- verification continues a usable `active` agent first
- callers prefer `send_input` while that same `active` agent is still open
- callers use `continuation_probe` to distinguish resume from recovery spawn
- if no usable `active` agent exists, the orchestrator spawns one
- only `block -> pass` marks an agent `non_active`
- termination depends only on a valid `active` pass

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

- execution evidence MUST record:
  - `review_goal`
  - `review_phase`
  - `review_scope`
  - `review_coverage`
  - `reviewed_paths`
  - `skipped_paths`
  - `reviewed_axes`
  - `unreviewed_axes`
- each checkpoint MUST maintain `agent-table.json`

### Review Checkpoints

- Shared sequence reference: `verify-sequence/default`
- Review goal: `implementation_correctness`
- Verifier agent path:
  `openspec/schemas/ai-enforced-workflow/agents/verify-reviewer.toml`
- Invocation:
  built-in subagent API with `fork_context=false` and a minimal verification bundle
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
  `review/review-runs/fix-ordinary-reference-lost-boundary-v4/findings.json`
- Verifier execution evidence JSON path:
  `review/review-runs/fix-ordinary-reference-lost-boundary-v4/verifier-evidence.json`
- Agent table path:
  `review/review-runs/fix-ordinary-reference-lost-boundary-v4/agent-table.json`
- Continuation target on pass:
  continue apply, then sync specs and archive this change

Checkpoint-specific primary surfaces:

- artifact-completion docs-first review: changed `proposal/specs/design/tasks`
- active-change source-first review: changed code, changed tests, directly impacted code

## Migration Plan

1. Add the helper and pure geometry tests.
2. Update ordinary reference extraction to build interpreted center candidates and keep strict leading behavior.
3. Update CircleV2 composition to call the helper for InnerTrace/ExitTrace role paths.
4. Run focused local tests and build checks.
5. Run OpenSpec docs-first and source-first verification to valid pass.
6. Sync delta specs into main specs and archive the completed change.

Board deployment/smoke consideration:

- This change affects runtime steering geometry, so the board-facing smoke hook is a no-motion or carefully supervised steering-media capture after build/upload.
- The implementation can be locally validated without deploying to the board; if board smoke is not run in this turn, the final report must state that explicitly.

Rollback:

- Reverting the helper integration restores raw midpoint ordinary reference and direct lateral CircleV2 offsets. No persistent data migration is required.

## Open Questions

- None for this change. The V4 documents intentionally fix the mapping, helper formula, parameter surface, and ownership boundaries.

## Risks / Trade-offs

- The helper's local slope estimate can expose sparse-trace noise. The first implementation should use deterministic adjacent/interpolated trace geometry and stop on insufficient input rather than smoothing through gaps.
- Keeping `ReferenceMode::kIntervalCenter` preserves API stability but makes the mode name less literal. This is acceptable because V4 defines it as ordinary current-frame visual reference rather than raw midpoint.
- Ordinary one-side repair can produce fewer points than the previous raw midpoint path in ambiguous rows. That is intentional; existing hold-last handles temporal continuity.
- CircleV2 nonzero offsets on curved edges may differ from previous lateral additions. This is the purpose of sharing the helper and should be covered by tests before track tuning.
