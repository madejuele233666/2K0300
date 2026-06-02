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
- Authoritative docs-first findings path:
  - `openspec/changes/replace-circle-entry-with-sparse-circle-v2/verification/docs-first/attempt-<n>/findings.json`
- Docs-first verifier evidence path:
  - `openspec/changes/replace-circle-entry-with-sparse-circle-v2/verification/docs-first/attempt-<n>/verifier-evidence.json`
- Docs-first agent table path:
  - `openspec/changes/replace-circle-entry-with-sparse-circle-v2/verification/docs-first/agent-table.json`
- Authoritative source-first findings path:
  - `openspec/changes/replace-circle-entry-with-sparse-circle-v2/verification/source-first/attempt-<n>/findings.json`
- Source-first verifier evidence path:
  - `openspec/changes/replace-circle-entry-with-sparse-circle-v2/verification/source-first/attempt-<n>/verifier-evidence.json`
- Source-first agent table path:
  - `openspec/changes/replace-circle-entry-with-sparse-circle-v2/verification/source-first/agent-table.json`
- Supported continuation overrides:
  - `verify-only`
  - `dry-run`
  - `manual_pause`
- Artifact-completion gate ownership:
  - this task list completes the schema's `applyRequires` set, so the active artifact-creation caller that completed it (`openspec-propose` or `openspec-continue-change`) owns the docs-first artifact review before implementation entry
  - `openspec-apply-change` does not own the docs-first artifact gate

## 1. Vertical Slice: Scene Contracts, Params, And Memory

- [x] 1.1 Add C++17-compatible scene public types in `new/code/runtime/steering_scene_frame_view.hpp`: `ConstArrayView`, `BevRowsView`, `RoadHalfWidth`, `OrdinaryRoadModel`, `MotionArcView`, `CaptureStamp`, and `SceneFrameView`.
- [x] 1.2 Add CircleV2 public scene types in `new/code/runtime/steering_circle_v2_scene.hpp`: `CirclePhase`, `CircleDir` if not already reusable, `CircleV2StageClock`, `CircleV2Memory`, `CircleV2Params`, `CircleV2ReferenceRole`, `CircleV2ReferencePlan`, `CircleV2TelemetryReason`, `CircleV2Telemetry`, `CircleV2StepResult`, and `CircleV2Scene::Step()`.
- [x] 1.3 Add `CircleV2Memory` to `SteeringPerceptionMemory` and update `ResetSteeringPerceptionMemory()` so idle memory clears direction and clock invariants.
- [x] 1.4 Replace old `CIRCLE_ENTRY_*` runtime defaults and parser fields with `CIRCLE_V2_ENABLED`, `CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG`, and `CIRCLE_V2_EXIT_HOLD_FRAMES` in `new/config/default_params.json`, `new/config/default_params.md`, `new/code/port/runtime_parameter_types.hpp`, and `new/code/platform/param_store.cpp`.
- [x] 1.5 Add focused parameter/default tests covering required V2 keys, no dangerous zero yaw-threshold default, `CIRCLE_V2_EXIT_HOLD_FRAMES >= 2`, and removal of old `CIRCLE_ENTRY_*` expectations.

## 2. Vertical Slice: Event Observer And Pure Reducer

- [x] 2.1 Add `new/code/runtime/detail/steering_circle_v2_internal.hpp` and `detail/steering_circle_v2_reducer.cpp` with private `CircleV2Events`, `CircleV2ReferenceContext`, `ReduceCircleV2()`, `EnterPhase()`, and `EnterIdle()`.
- [x] 2.2 Add `steering_circle_v2_reducer_test` covering allowed transitions, disallowed transitions, direction invariants, `ExitTrace` hold off-by-one, final-frame `frame_phase` / `next_phase` divergence, and `exit_hold_frames >= 2`.
- [x] 2.3 Add `detail/steering_circle_v2_event_observer.cpp` with phase-gated event production: `Idle` Phase1 cue, `Approach` locked-direction bottom expansion, `InnerTrace` directed yaw exit, and no transition events in `ExitTrace`.
- [x] 2.4 Migrate old Phase1 circle direction semantics into private `ObserveCirclePhase1Cue` and add golden parity tests against old Phase1 left/right/none sparse-row cases before deleting the old runtime owner.
- [x] 2.5 Add directed-yaw tests covering left-positive yaw, right-negative yaw, the configured yaw sign convention, and reverse wobble that must not satisfy exit by `abs(yaw_delta)`.

## 3. Vertical Slice: Geometry, Composer, And Candidate Adapter

- [x] 3.1 Add `detail/steering_circle_v2_geometry_observer.cpp` to construct only the geometry needed for the current reference role: nearest locked-direction inner edge for `InnerTrace`, opposite-side straight outer edge for `ExitTrace`, and no geometry for `Idle` / `Approach`.
- [x] 3.2 Add `detail/steering_circle_v2_composer.cpp` to offset the selected edge by `ordinary_road.half_width` and return `std::optional<CircleV2ReferencePlan>` without changing state.
- [x] 3.3 Add `new/code/runtime/steering_circle_v2_reference_adapter.hpp/.cpp` to map `CircleV2ReferencePlan` to `VisualReferenceCandidate` using fixed role/direction mapping and V2 source strings such as `circle_v2_inner` and `circle_v2_exit`.
- [x] 3.4 Add geometry/composer/adapter tests proving B/InnerTrace and C/ExitTrace offset direction for left and right circles, absent geometry yields `nullopt`, and no confidence score is consumed.

## 4. Vertical Slice: Runtime Pipeline Integration And Old Circle Cleanup

- [x] 4.1 Update `new/code/runtime/steering_frame_perception_pipeline.cpp` to build `OrdinaryRoadModel`, construct `MotionArcView`, step `CircleV2Scene`, adapt any `CircleV2ReferencePlan`, and insert adapted candidates before `SelectVisualReference()`.
- [x] 4.2 Enforce active lifecycle semantics: once `CircleV2Memory.phase != Idle`, the pipeline must not silently skip `CircleV2Scene::Step()` due to missing ordinary road or motion arc; it must reset the scene lifecycle or enter an existing global fail-safe path.
- [x] 4.3 Remove circle ownership from `new/code/legacy/steering_visual_element_pipeline.*`: no `DetectCircleElementEvidence()` call, no `AppendCircleEvidence`, no `MaybeBuildCircleCandidate`, no `CircleEntryPipelineDiagnostics`, and no circle records/candidates from `RunVisualElementPipeline()`.
- [x] 4.4 Remove old rear-black circle entry runtime functions from active builds or leave only non-runtime test scaffolding until deleted: `CircleEntryPathFacts`, `BEVMetricClassSampler`, `HasRearSideBlack`, `FindRearFrontierPoint`, `BuildCircleEntryPathFacts`, and `BuildCircleEntryVisualReferenceCandidate`.
- [x] 4.5 Update `visual_element_evidence_test`, `visual_reference_orchestration_test`, and runtime pipeline tests so visual element evidence remains cross/non-circle while CircleV2 is the sole source of circle candidates.

## 5. Vertical Slice: Observability, Probe, And Board Smoke Evidence

- [x] 5.1 Add CircleV2 telemetry to `PerceptionResult` / steering snapshot surfaces with `frame_phase`, `next_phase`, `dir`, `reference_role`, and enum reason serialization.
- [x] 5.2 Update `new/code/platform/steering_media_protocol.cpp`, steering media selftests, and host-capture selftests so config snapshots publish `CIRCLE_V2_*` and no longer require old `CIRCLE_ENTRY_*` fields.
- [x] 5.3 Update `new/user/scene_overlay_probe.cpp` and `run_scene_overlay_probe_authority_baseline_test.sh` to inspect CircleV2 telemetry and V2 candidate source names instead of `circle_entry.*` diagnostics or circle element records.
- [x] 5.4 Run focused local verification commands for parameter/default tests, CircleV2 reducer/event/geometry/adapter tests, visual element evidence tests, steering media selftest, host capture selftest, and `git diff --check`; record commands and outputs in the apply summary.
- [x] 5.5 Run a no-upload user build through `new/user/build.sh` or the accepted local build command to prove runtime/platform changes compile under C++17.
- [x] 5.6 Attempt board smoke gate after local verification; board SSH at `10.100.170.226:22` timed out in this environment, so no board upload/restart was performed. Local steering-media and host-capture smoke tests verify `CIRCLE_V2_*` / CircleV2 telemetry presence and old `circle_entry` absence.

## 6. Verification, Sync, And Archive

- [x] 6.1 Run `openspec validate replace-circle-entry-with-sparse-circle-v2 --strict` or the repository's accepted strict validation command and fix schema/spec/task issues before source-first review.
- [x] 6.2 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for the implemented CircleV2 source changes and tests. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON and verifier evidence JSON, and require caller/orchestrator-maintained `agent-table.json`.
- [x] 6.3 Ensure source-first verifier evidence uses `subject_required_any_of` binding to the active change and changed implementation paths, carries `verifier_evidence_required`, satisfies `valid_pass_requirements`, and routes findings according to `findings_required`, `finding_object_required`, `finding_semantics`, and `repair_routing_rules` with blocking findings routed to `openspec-repair-change`.
- [x] 6.4 Use `openspec-verify-change` until the active source-first verifier reaches a valid pass with complete exhaustive coverage, repairing blocking auto-fixable findings and rerunning focused tests as required.
- [x] 6.5 After valid source-first pass, run `openspec-sync-specs` for this change and verify the main specs reflect `sparse-circle-v2-scene`, modified `bev-visual-element-evidence`, and modified `steering-tuning-media-observability`.
- [x] 6.6 Archive the completed change with `openspec-archive-change` only after implementation, strict validation, source-first verification, and spec sync evidence are present.
