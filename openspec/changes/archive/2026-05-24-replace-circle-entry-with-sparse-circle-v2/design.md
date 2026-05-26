## Context

`new/docs/visual-element-sparse-circle-v2.zh-CN.md` defines the target V2 circle behavior: preserve the existing Phase1 circle direction cue, delete rear / side-rear black frontier as the inner-circle or entry-path standard, and replace the old single-frame circle_entry candidate with a minimal state machine:

```text
Idle -> Approach -> InnerTrace -> ExitTrace -> Idle
```

The current runtime still routes circle through `RunVisualElementPipeline()`:

- `new/code/legacy/steering_visual_element_pipeline.cpp` calls `DetectCircleElementEvidence()`, appends `circle_left_raw` / `circle_right_raw` / effective circle records, builds optional circle_entry candidates, and stores `CircleEntryPipelineDiagnostics`.
- `new/code/legacy/steering_circle_element_evidence.cpp` contains Phase1 sparse-row cue logic and Phase2 rear-black frontier / circle_entry path construction.
- `new/code/runtime/steering_frame_perception_pipeline.cpp` builds sparse rows, a line candidate, visual element candidates, and then calls `SelectVisualReference()`.
- `new/code/runtime/runtime_state.hpp` already contains `SteeringPerceptionMemory` and `MotionHistory`.
- `new/code/runtime/steering_reference_time_alignment.cpp` already has yaw integration logic over `MotionHistory`.
- `new/code/platform/param_store.cpp`, `new/code/platform/steering_media_protocol.cpp`, `new/config/default_params.json`, `new/user/scene_overlay_probe.cpp`, and current tests still expose old `CIRCLE_ENTRY_*` and `circle_entry` behavior.

The project builds as C++17 (`new/user/CMakeLists.txt` and verification scripts), so the V2 document's `std::span` intent must land as a C++17-compatible non-owning view rather than literal `std::span`.

### Alignment Reference Scope

| Reference Module | Target Module | Action | Notes |
|---|---|---|---|
| `new/docs/visual-element-sparse-circle-v2.zh-CN.md` | V2 scene contracts and tests | Adapt | Primary behavior source; adjusted for C++17 span-equivalent implementation. |
| `new/code/legacy/steering_circle_element_evidence.cpp` | `CircleV2EventObserver::ObserveCirclePhase1Cue` | Adapt | Preserve Phase1 direction cue semantics; do not migrate rear-black Phase2 path facts. |
| `new/code/legacy/steering_visual_element_pipeline.cpp` | non-circle visual element pipeline | Reduce | Keep cross/non-circle evidence; remove circle ownership and circle_entry candidate construction. |
| `new/code/runtime/steering_frame_perception_pipeline.cpp` | `SceneFrameView` composition and scene runner | Adapt | Insert ordinary-road model, scene step, reference-plan adapter, and candidate arbitration. |
| `new/code/runtime/runtime_state.hpp` | `CircleV2Memory` and motion-arc source | Adapt | Add circle memory to perception memory; expose motion history through a composed view. |
| `new/code/runtime/steering_reference_time_alignment.cpp` | `MotionArcView` yaw query helper | Adapt | Reuse yaw integration mechanics behind a narrow ability interface. |
| `new/code/platform/param_store.cpp` / `new/config/default_params.json` | V2 parameter surface | Replace | Remove old `CIRCLE_ENTRY_*`; add `CIRCLE_V2_*` with validation. |
| `new/code/platform/steering_media_protocol.cpp` and probes/tests | V2 observability | Replace | Stop asserting old circle_entry output; publish V2 telemetry and sources. |

Coverage report:

| Contract | Coverage |
|---|---|
| Phase1 circle cue semantics | Covered by adapting old helper logic with golden parity tests. |
| Rear-black Phase2 deletion | Covered by removing old Phase2 builders from runtime and tests. |
| Stateful V2 phase lifecycle | Covered by new reducer tests and scene facade tests. |
| Motion-angle B -> C gate | Covered by `MotionArcView` event observer tests over left/right yaw signs. |
| Reference generation B/C roles | Covered by geometry observer, composer, and adapter tests. |
| Public observability migration | Covered by steering media/probe/selftest updates. |

Critical adaptation boundary: archive topology scene FSMs and old reference-policy state are historical context only. They are not copied because the active runtime uses sparse BEV simple perception, not the archived topology graph / trusted-reference policy stack.

## Goals / Non-Goals

**Goals:**

- Add `CircleV2Scene` as the only runtime owner of circle semantics.
- Preserve original Phase1 circle direction behavior through a private `ObserveCirclePhase1Cue` helper.
- Replace rear-black circle entry with V2 `Approach`, `InnerTrace`, and `ExitTrace` semantics.
- Keep `CircleV2Scene` input narrow: strong scene facts, memory, and params.
- Keep internal events, geometry, edge traces, and reference context private to circle V2 implementation files.
- Add V2 parameters, memory, telemetry, reference plan adaptation, and tests.
- Remove old `circle_entry` runtime parameters, diagnostics, candidates, and visual element circle records.

**Non-Goals:**

- No new external dependency.
- No archive topology scene FSM restoration.
- No separate cooldown phase.
- No confidence scoring inside `CircleV2ReferencePlan`.
- No public API for intermediate facts such as `left_open`, `bottom_expansion`, `inner_edge`, or `outer_edge`.
- No control-loop actuator redesign.

## Decisions

### Decision 1: Circle Semantics Move To A Single Runtime Scene Owner

- Problem: circle ownership is split across element evidence, candidate construction, diagnostics, and future state-machine expectations.
- Alternatives considered:
  - Keep circle in `RunVisualElementPipeline()` and add state there.
  - Add a public facts-builder API that exposes circle transition facts to a separate FSM.
  - Move all circle semantics to `CircleV2Scene`.
- Choice: introduce `CircleV2Scene` as the only circle scene interpreter. `RunVisualElementPipeline()` becomes cross / non-circle only.
- Stack Equivalent: `SteeringFramePerceptionPipeline` builds `SceneFrameView` -> `CircleV2Scene::Step()` -> optional `CircleV2ReferencePlan` -> `VisualReferenceAdapter` -> `SelectVisualReference()`.
- Named Deliverables:
  - `new/code/runtime/steering_circle_v2_scene.hpp/.cpp`
  - `new/code/runtime/steering_circle_v2_reference_adapter.hpp/.cpp`
  - cleanup in `new/code/legacy/steering_visual_element_pipeline.*`
  - focused runtime pipeline tests
- Failure Semantics: if no circle plan is present, ordinary line / non-circle candidates continue through existing arbitration. Circle geometry absence produces no plan but does not mutate reducer state.
- Boundary Examples: `CircleV2Scene` may read sparse rows, ordinary road model, motion arc, memory, and params; it may not read `VisualReferenceCandidate` line wrappers, element-pipeline circle evidence, safety state, actuator output, or arbitration result.
- Contrast Structure: this replaces V1 circle as an element candidate producer with V2 circle as a scene interpreter.
- Verification Hook: `steering_circle_v2_scene_test`, `visual_element_evidence_test`, `visual_reference_orchestration_test`, and board no-motion steering media capture showing V2 source names without old circle_entry diagnostics.

### Decision 2: Public Scene Input Is A Strong C++17 Fact Surface

- Problem: direct `VisualReferenceCandidate` and concrete `MotionHistory` inputs would couple the scene to packaging and storage details it does not own.
- Alternatives considered:
  - Feed the line candidate and `MotionHistory` directly into the scene.
  - Expose many explicit public facts such as side openings, expansion, and straightness.
  - Build a narrow `SceneFrameView` with ordinary-road and motion-arc views.
- Choice: use `SceneFrameView` containing `BevRowsView`, `OrdinaryRoadModel`, `MotionArcView`, and `CaptureStamp`. Because the project is C++17, `BevRowsView` uses a small project-owned `ConstArrayView<T>` stack equivalent rather than literal `std::span`.
- Stack Equivalent: `std::span` design intent -> C++17 `ConstArrayView<BEVSimpleRowScan>` with construction-time non-empty invariant; `MotionHistory` -> non-null `MotionArcView` query; line candidate -> `OrdinaryRoadModel`.
- Named Deliverables:
  - `new/code/runtime/steering_scene_frame_view.hpp`
  - ordinary-road builder in or near the frame perception pipeline
  - `MotionArcView` adapter helper around current motion history / yaw integration
- Failure Semantics: incomplete scene facts are composition failures, not scene branches. In active non-Idle scene, composition must reset scene lifecycle or use global fail-safe rather than silently skip `Step()`.
- Boundary Examples: `ordinary_road.half_width` is required before scene step; `CircleV2Scene` does not guess width. `MotionArcView` is non-null and queryable over the active phase time range.
- Contrast Structure: this keeps public facts stable while leaving circle-specific facts private.
- Verification Hook: compile-time interface use in scene tests, lifecycle tests for active-step continuity, and board config/media evidence that V2 can run without exposing `MotionHistory` internals.

### Decision 3: Events And Geometry Are Observed In Separate Internal Steps

- Problem: computing geometry before state reduction can calculate InnerTrace geometry on a frame that has already transitioned to ExitTrace.
- Alternatives considered:
  - Have one observer compute events and all possible geometries every frame.
  - Let the FSM read raw expansion, yaw, and edge facts directly.
  - Split event observation, reducer, geometry observation, and composition.
- Choice: implement four internal modules: `CircleV2EventObserver`, `CircleV2Reducer`, `CircleV2GeometryObserver`, and `CircleV2ReferenceComposer`.
- Stack Equivalent: public frame facts -> phase-gated `CircleV2Events` -> pure reducer decision -> role-specific geometry -> optional plan.
- Named Deliverables:
  - `new/code/runtime/detail/steering_circle_v2_internal.hpp`
  - `detail/steering_circle_v2_event_observer.cpp`
  - `detail/steering_circle_v2_reducer.cpp`
  - `detail/steering_circle_v2_geometry_observer.cpp`
  - `detail/steering_circle_v2_composer.cpp`
- Failure Semantics: event absence holds phase; geometry absence yields `reference_plan=nullopt`; composer never changes state.
- Boundary Examples: event observer can query yaw delta; reducer cannot. geometry observer can search edges; reducer cannot. composer can offset paths; event observer cannot decide arbitration.
- Contrast Structure: FSM remains event-based instead of becoming a perception pipeline.
- Verification Hook: separate reducer tests, event-observer tests, geometry-observer tests, composer/adapter tests, and board media telemetry confirming frame phase and next phase can diverge on the final ExitTrace frame.

### Decision 4: Phase Lifecycle Is Minimal And Fully Specified

- Problem: V1 lacks a complete exit state, and vague hold counters create off-by-one behavior.
- Alternatives considered:
  - Add an explicit cooldown phase.
  - Permit A/B fallback transitions to Idle on missing geometry.
  - Use only `Idle`, `Approach`, `InnerTrace`, and `ExitTrace` with fixed frame-index semantics.
- Choice: use only the four V2 phases. `ExitTrace` hold is the cooldown. `phase_frame_index` means frames already fully output at frame start; the reducer stores the next frame's index in `next_memory`.
- Stack Equivalent: `CircleV2Memory{phase, dir, clock.enter_capture_time_ms, clock.phase_frame_index}` and pure `ReduceCircleV2()`.
- Named Deliverables: `CircleV2Memory` in perception memory, reducer helper, reducer tests, reset helper.
- Failure Semantics: `EnterIdle()` clears direction and clock. `exit_hold_frames < 2` is invalid and follows parameter fallback. Missing geometry cannot reset memory.
- Boundary Examples: `Idle` must have `dir=None`; non-Idle must lock left/right; `Approach` can only come from `Idle`.
- Contrast Structure: this replaces implicit candidate lifetime and diagnostics with an explicit scene lifecycle.
- Verification Hook: reducer unit tests for every allowed and disallowed transition, off-by-one tests for `exit_hold_frames=2/3`, and board telemetry showing `ExitTrace` hold frames before returning to idle.

### Decision 5: Phase1 Cue Is Migrated, Rear-Black Entry Is Deleted

- Problem: the user wants to preserve the existing circle cue for `Idle -> Approach` but rejects rear-black as the inner-circle standard.
- Alternatives considered:
  - Keep `DetectCircleElementEvidence()` and consume its public records.
  - Rewrite Phase1 detection now.
  - Extract only the Phase1 cue logic and delete Phase2 rear-black runtime ownership.
- Choice: move Phase1 cue semantics into `ObserveCirclePhase1Cue` and golden-test parity against old Phase1. Delete rear / side-rear black frontier path builders from runtime.
- Stack Equivalent: selected helpers from `steering_circle_element_evidence.cpp` (`CollectRows`, `BuildRowObservation`, `WidestInterval`, `SustainedGrowthEvidence`, `FitBoundaryLine`, `AssessSides`) -> private event helper; old `BuildCircleEntryPathFacts` / `BuildCircleEntryVisualReferenceCandidate` -> removed.
- Named Deliverables: private Phase1 helper, parity tests, cleanup of old functions/params/probe output.
- Failure Semantics: Phase1 cue returns `none` when evidence is absent or ambiguous. Rear-black facts no longer create any path or candidate.
- Boundary Examples: `DetectCircleElementEvidence` is not the target name after migration; `ObserveCirclePhase1Cue` is not element evidence and does not publish records.
- Contrast Structure: preserve recognition semantics, replace runtime ownership and path semantics.
- Verification Hook: golden parity tests for left/right/none Phase1 rows, negative tests proving rear-black-only fixtures do not produce V2 paths, and board/probe output lacking old circle_entry diagnostics.

### Decision 6: Motion Angle Is Queried, Not Integrated By The FSM

- Problem: B -> C depends on cumulative yaw, but the FSM should not own IMU integration or motion-history storage.
- Alternatives considered:
  - Store yaw accumulator inside `CircleV2Memory`.
  - Let the FSM read `MotionHistory` and integrate.
  - Let event observation query a motion-arc view and pass only `exit_gate_reached` to the reducer.
- Choice: `MotionArcView` answers yaw delta over capture-time bounds; `CircleV2EventObserver` direction-normalizes that value and emits `exit_gate_reached`.
- Stack Equivalent: current `MotionHistory` + yaw integration helper -> `MotionArcView::YawDeltaRad()` -> `directed_turn_angle` -> `CircleV2Events.exit_gate_reached`.
- Named Deliverables: motion arc adapter, directed yaw sign helper, event tests for left/right sign and reverse wobble.
- Failure Semantics: `abs(yaw_delta)` is not accepted. If active phase cannot query the needed time range, the composition layer must reset lifecycle or fail safe rather than freeze memory.
- Boundary Examples: reducer sees only `exit_gate_reached`; telemetry may expose directed-turn observation for debugging if kept internal-to-V2 public telemetry.
- Contrast Structure: the FSM consumes events, not sensors.
- Verification Hook: unit tests with positive left yaw, negative right yaw, and reverse yaw; board media evidence comparing configured yaw threshold and phase transition timing.

### Decision 7: V2 Observability Replaces Old Circle Entry Surfaces

- Problem: media, probes, and tests currently explain circle through `CIRCLE_ENTRY_*`, `circle_entry.*`, and element records that will no longer exist.
- Alternatives considered:
  - Preserve old telemetry as deprecated fields.
  - Keep old config keys but reinterpret them.
  - Replace observability with V2 telemetry and V2 source names.
- Choice: remove old runtime observability and publish V2 telemetry: `frame_phase`, `next_phase`, `dir`, `reference_role`, and reason enum, plus `circle_v2_inner` / `circle_v2_exit` candidate source names.
- Stack Equivalent: `CircleV2StepResult.telemetry` -> `PerceptionResult` / steering snapshot -> steering media image header; `CircleV2Params` -> config snapshot.
- Named Deliverables: port telemetry types, media serializer updates, selftest/probe updates, config default docs.
- Failure Semantics: absent plan due geometry reports `GeometryUnavailable`; old `circle_entry` names are not retained as compatibility aliases.
- Boundary Examples: selected reference may still use existing `VisualReferenceCandidateKind::kCircleLeft/kCircleRight`; source strings must identify V2 role.
- Contrast Structure: observability follows the new owner rather than keeping a compatibility shell.
- Verification Hook: steering media selftest, host capture selftest, scene overlay probe updates, and board no-motion capture of config snapshot and image-frame telemetry.

## Engineering Discipline

- Principles reference:
  `openspec/schemas/ai-enforced-workflow/engineering-principles.md`
- Domain language / ADRs consulted:
  `new/docs/visual-element-sparse-circle-v2.zh-CN.md`, `openspec/specs/bev-visual-element-evidence/spec.md`, `openspec/specs/steering-tuning-media-observability/spec.md`, current runtime and legacy steering code.
- Primary feedback loop:
  focused C++ unit tests for reducer/event/geometry/adapter slices, then runtime pipeline/probe/media tests, then source-first verifier review.
- Prototype question, if any:
  none required before implementation; edge search can be built directly against current sparse row facts with focused tests.
- Hard dependencies:
  `verify-sequence/default`, authoritative findings/evidence, valid-pass requirements, subject binding, and current-state `agent-table.json`
- Soft dependencies:
  glossary, ADRs, architecture heuristics, and prototype notes; use them when present, but do not create auxiliary review gates for them

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

- Use verifier runtime profile from `openspec/schemas/ai-enforced-workflow/agents/verify-reviewer.toml`.
- Invoke verifier reviews through the built-in subagent API with `fork_context=false`.
- Pass only the minimal verification bundle, optional index context when explicitly useful, and the required output paths.
- Use invocation template `verify-reviewer-inline-v3`.

Loop rule:

- an `active` agent that reports `block` stays authoritative until that same agent returns `pass`
- `agent-table.json` stays current-state-only; recovery lives in `continuation_probe`
- valid `pass` requires `review_coverage.coverage_status=complete` and `review_coverage.exhaustive=true`
- partial verification requires explicit `review_scope.scope`
- only the main orchestrator may authorize resume/spawn/repair/terminate, and it must not substitute its own judgment for verifier output

Shared field groups from `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`:

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
- Invocation API:
  built-in subagent API
- Parent-context rule:
  `fork_context=false`
- Invocation template id: `verify-reviewer-inline-v3`
- Invocation bundle:
  minimal verification bundle, `evidence_paths_or_diff_scope`, and output paths for findings, verifier evidence, and caller-owned `agent-table.json`
- Default loop behavior:
  - resume `active` first
  - prefer `send_input` while that same `active` agent is still open
  - use `continuation_probe` to distinguish resume from dedicated recovery spawn
  - spawn when no usable `active` agent exists
  - repair follows `block`
  - only `block -> pass` marks `non_active`
  - final termination requires a valid `active` pass
- Authoritative docs-first findings JSON path:
  `openspec/changes/replace-circle-entry-with-sparse-circle-v2/verification/docs-first/attempt-<n>/findings.json`
- Docs-first verifier evidence JSON path:
  `openspec/changes/replace-circle-entry-with-sparse-circle-v2/verification/docs-first/attempt-<n>/verifier-evidence.json`
- Docs-first agent table path:
  `openspec/changes/replace-circle-entry-with-sparse-circle-v2/verification/docs-first/agent-table.json`
- Authoritative source-first findings JSON path:
  `openspec/changes/replace-circle-entry-with-sparse-circle-v2/verification/source-first/attempt-<n>/findings.json`
- Source-first verifier evidence JSON path:
  `openspec/changes/replace-circle-entry-with-sparse-circle-v2/verification/source-first/attempt-<n>/verifier-evidence.json`
- Source-first agent table path:
  `openspec/changes/replace-circle-entry-with-sparse-circle-v2/verification/source-first/agent-table.json`
- Continuation target on pass:
  docs-first pass returns to the active artifact-completion caller that finished the `applyRequires` set (`openspec-propose` or `openspec-continue-change`); source-first pass returns to `openspec-verify-change`

Checkpoint-specific primary surfaces:

- artifact-completion docs-first review: changed `proposal/specs/design/tasks`
- active-change source-first review: changed code, changed tests, directly impacted code

## Migration Plan

1. Add V2 parameter fields and `CircleV2Memory` without selecting V2 candidates.
2. Add C++17 scene fact view, `OrdinaryRoadModel`, and `MotionArcView` adapter.
3. Add `CircleV2Scene` facade and internal event/reducer/geometry/composer files.
4. Migrate Phase1 cue into `ObserveCirclePhase1Cue` with golden parity tests.
5. Add reducer and event tests before connecting candidate output.
6. Add geometry observer, reference composer, and reference adapter tests.
7. Wire `CircleV2Scene::Step()` into `SteeringFramePerceptionPipeline` and candidate arbitration.
8. Remove runtime circle ownership from `RunVisualElementPipeline()` and delete old circle_entry candidate/diagnostic wiring.
9. Replace `CIRCLE_ENTRY_*` config, parser, media, docs, and test expectations with `CIRCLE_V2_*`.
10. Run focused tests, no-upload user build, `git diff --check`, source-first verifier review, then sync specs and archive after valid pass.

Rollback during development is disabling scene registration or reverting the change. There is no runtime compatibility alias for old `circle_entry` semantics.

## Open Questions

- None before implementation. Edge-search thresholds and geometry tuning values should be introduced only as concrete implementation parameters when tests show they are needed; they should not broaden the public scene API.

## Risks / Trade-offs

- Removing old circle evidence records is cleaner but breaks old probes/tests; the change intentionally updates those surfaces rather than carrying deprecated runtime semantics.
- The narrow scene input boundary makes ownership clear but requires composition code to build `OrdinaryRoadModel` and `MotionArcView` correctly.
- Direction-normalized yaw is more semantically correct than absolute yaw but depends on one documented yaw sign convention.
- Geometry absence not resetting the FSM keeps layering clean, but it means tests and telemetry must make no-plan frames visible.
- C++17 span-equivalent view preserves the V2 strong-contract intent but adds a small project-owned utility type.
