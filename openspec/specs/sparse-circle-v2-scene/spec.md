# sparse-circle-v2-scene Specification

## Purpose
Define the sparse-BEV Circle V2 scene owner, its phase memory, transition events, reference-plan composition, and lifecycle boundaries.

## Requirements
### Requirement: Circle V2 Scene Owns Stateful Circle Semantics
The runtime SHALL route circle recognition, circle phase state, circle telemetry, and circle reference plan generation through `CircleV2Scene`. `CircleV2Scene` SHALL be the only runtime owner of circle semantics after this change.

`RunVisualElementPipeline()` MUST NOT own circle state, circle evidence records, circle_entry diagnostics, rear-black entry facts, or circle visual-reference candidate construction.

#### Scenario: Circle scene is invoked after ordinary BEV perception
- **WHEN** `SteeringFramePerceptionPipeline::ProcessFrame()` has produced sparse BEV rows and an ordinary BEV reference path for a frame
- **THEN** the runtime SHALL build a `SceneFrameView`
- **AND** it SHALL step `CircleV2Scene` through that `SceneFrameView`, prior `CircleV2Memory`, and `CircleV2Params`
- **AND** circle candidates SHALL originate only from a `CircleV2ReferencePlan` adapted after `CircleV2Scene::Step()`

#### Scenario: Visual element pipeline remains non-circle
- **WHEN** the runtime calls `RunVisualElementPipeline()`
- **THEN** that pipeline SHALL produce cross and other non-circle visual element evidence only
- **AND** it SHALL NOT call `DetectCircleElementEvidence`
- **AND** it SHALL NOT emit `circle_left_raw`, `circle_right_raw`, `circle_left`, `circle_right`, `circle_entry` diagnostics, or `kCircleLeft` / `kCircleRight` candidates

### Requirement: Scene Frame View Is A Strong Public Fact Contract
`CircleV2Scene::Step()` SHALL read only a stable public fact surface composed of `SceneFrameView`, `CircleV2Memory`, and `CircleV2Params`. `SceneFrameView` SHALL contain non-empty sparse BEV rows, an `OrdinaryRoadModel` with center path and road half width, a non-null `MotionArcView`, and a `CaptureStamp`.

`CircleV2Scene` MUST NOT read a `VisualReferenceCandidate` as input, MUST NOT read a concrete `MotionHistory` container as input, and MUST NOT save references to `SceneFrameView` data across frames.

#### Scenario: Ordinary road model is complete before scene step
- **WHEN** the composition layer calls `CircleV2Scene::Step()`
- **THEN** `SceneFrameView.rows.rows` SHALL be non-empty
- **AND** `ordinary_road.center_path`, `ordinary_road.half_width`, and `stamp.capture_time_ms` SHALL correspond to the same captured frame
- **AND** `CircleV2Scene` SHALL consume `ordinary_road.half_width` instead of guessing a half width or falling back to a magic constant
- **AND** the composition layer SHALL construct `ordinary_road.half_width` from a stable road-geometry parameter such as `BEV_GEOMETRY.NOMINAL_ROAD_HALF_WIDTH_M`, not by recomputing a rows-derived width each frame

#### Scenario: Motion arc is an ability view rather than scene-owned history
- **WHEN** `CircleV2EventObserver` needs to evaluate the InnerTrace exit gate
- **THEN** it SHALL query `MotionArcView::YawDeltaRad(from_capture_time_ms, to_capture_time_ms)`
- **AND** `CircleV2Scene` SHALL NOT depend on the concrete `MotionHistory`, IMU adapter, control tick, or yaw integration storage type

### Requirement: Circle V2 FSM Uses The Minimal Four Phase Sequence
`CircleV2Memory` SHALL contain only the circle phase, locked direction, phase enter capture time, phase frame index, and the maximum direction-normalized InnerTrace yaw progress reached during the current phase. The only valid phase sequence SHALL be:

```text
Idle -> Approach -> InnerTrace -> ExitTrace -> Idle
```

`Approach` SHALL only be entered from `Idle`. `ExitTrace` hold SHALL serve as the cooldown; no separate cooldown phase SHALL be introduced.

#### Scenario: Direction and phase invariants are maintained
- **WHEN** `CircleV2Memory.phase` is `Idle`
- **THEN** `CircleV2Memory.dir` SHALL be `None`
- **WHEN** `CircleV2Memory.phase` is not `Idle`
- **THEN** `CircleV2Memory.dir` SHALL be `left` or `right`
- **AND** `EnterIdle()` SHALL clear phase, direction, enter timestamp, phase frame index, and maximum directed yaw progress

#### Scenario: Reducer rejects implicit transitions
- **WHEN** `CircleV2Reducer` receives events for any frame
- **THEN** it SHALL only produce `Idle -> Approach`, `Approach -> InnerTrace`, `InnerTrace -> ExitTrace`, `InnerTrace -> Idle`, or `ExitTrace -> Idle`
- **AND** `InnerTrace -> Idle` SHALL only occur for the explicit `inner_trace_stalled` fallback event
- **AND** it SHALL NOT produce `Approach -> Idle`, `ExitTrace -> Approach`, `Idle -> InnerTrace`, or `Idle -> ExitTrace`

#### Scenario: ExitTrace hold has fixed frame semantics
- **WHEN** `ExitTrace` begins with `phase_frame_index = 0`
- **AND** `CIRCLE_V2_EXIT_HOLD_FRAMES` is `3`
- **THEN** the runtime SHALL output exactly three `ExitTrace` reference frames
- **AND** the third frame SHALL still expose `frame_phase = ExitTrace`
- **AND** the third frame's `next_memory.phase` SHALL become `Idle`

### Requirement: Circle V2 Events Are Phase-Gated
`CircleV2EventObserver` SHALL translate visual and motion facts into transition events, and `CircleV2Reducer` SHALL read only those events. Phase1 circle cue and Approach entry gate SHALL derive from the same Circle scene internal locked-side expansion observation, but SHALL NOT reuse the same boolean condition. Phase1 circle cue SHALL use full-trace side opening and opposite-boundary constraints. Approach entry gate SHALL use only a bottom / near-row contiguous ROI, and SHALL require locked-direction same-side boundary growth plus a straight bottom opposite boundary in that same ROI. Approach entry gate SHALL NOT compare the locked-side reach against the opposite-side reach as its opening condition. The event observer SHALL gate event production by prior phase:

- `Idle`: only `detected_dir`
- `Approach`: only locked-direction `entry_gate_reached`
- `InnerTrace`: only `exit_gate_reached` or `inner_trace_stalled`
- `ExitTrace`: no detected, entry, or exit events

#### Scenario: Shared expansion observation preserves Phase1 cue parity
- **WHEN** prior phase is `Idle`
- **THEN** the event observer SHALL infer `detected_dir` from internal side-expansion observation
- **AND** the result SHALL match the old Phase1 circle cue for equivalent sparse-row input for `left`, `right`, and `none`
- **AND** the public `SceneFrameView` SHALL NOT expose left-open, right-open, bottom-expansion, or entrance-corner facts

#### Scenario: Side-specific observation keeps road-connected boundary traces
- **WHEN** the shared expansion observation computes left-side reach, growth, straight baseline, or entrance point
- **THEN** it SHALL consume the left boundary of the white interval connected to the ordinary road center path
- **AND** it SHALL NOT discard that boundary merely because `left_m` is greater than or equal to zero
- **WHEN** it computes right-side reach, growth, straight baseline, or entrance point
- **THEN** it SHALL consume the right boundary of the white interval connected to the ordinary road center path
- **AND** it SHALL NOT discard that boundary merely because `right_m` is less than or equal to zero
- **AND** disconnected far-side white artifacts SHALL NOT be merged into the road boundary trace
- **AND** a normal bend whose opposite boundary only appears straight after sign-based filtering or detached-artifact merging SHALL NOT produce a Phase1 circle cue

#### Scenario: Detached far-side artifacts do not create a circle cue
- **WHEN** a normal road row contains the ordinary-road-connected white interval
- **AND** the same row also contains a detached white interval farther on the side being evaluated
- **THEN** Phase1 side opening SHALL be computed from the ordinary-road-connected interval boundary
- **AND** the detached interval SHALL NOT by itself create `detected_dir`

#### Scenario: Approach consumes only locked-direction expansion
- **WHEN** prior phase is `Approach` and locked direction is `left`
- **THEN** only left-side same-boundary growth inside the bottom contiguous ROI with a straight right bottom boundary SHALL be allowed to set `entry_gate_reached`
- **AND** right-side expansion SHALL NOT move the FSM to `InnerTrace`
- **WHEN** locked direction is `right`
- **THEN** only right-side same-boundary growth inside the bottom contiguous ROI with a straight left bottom boundary SHALL be allowed to set `entry_gate_reached`

#### Scenario: Approach requires bottom opposite straightness
- **WHEN** prior phase is `Approach`
- **AND** the locked side bottom rows show expansion
- **AND** the opposite bottom boundary is not straight
- **THEN** `entry_gate_reached` SHALL be false
- **AND** the FSM SHALL remain in `Approach`

#### Scenario: Approach does not consume Phase1 far opening as entry gate
- **WHEN** prior phase is `Approach`
- **AND** the locked side still has the full-trace Phase1 opening cue
- **AND** the locked side bottom rows do not show bottom expansion
- **THEN** `entry_gate_reached` SHALL be false
- **AND** the FSM SHALL remain in `Approach`

#### Scenario: Approach bottom ROI does not jump across missing near support
- **WHEN** prior phase is `Approach`
- **AND** the locked side has apparent expansion only after skipping missing or non-contiguous near rows
- **THEN** `entry_gate_reached` SHALL be false
- **AND** the FSM SHALL remain in `Approach`

#### Scenario: InnerTrace exit yaw is direction-normalized
- **WHEN** prior phase is `InnerTrace`
- **THEN** `directed_turn_angle` SHALL be `CircleTurnSign(dir) * MotionArcView::YawDeltaRad(InnerTrace.enter_capture_time_ms, current_capture_time_ms)`
- **AND** `directed_turn_progress` SHALL be the maximum of current `directed_turn_angle` and the InnerTrace maximum directed yaw progress already stored in `CircleV2Memory`
- **AND** `exit_gate_reached` SHALL compare `directed_turn_progress` to `CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG`
- **AND** for the current runtime yaw convention, `CircleTurnSign(left)` SHALL be `-1` and `CircleTurnSign(right)` SHALL be `+1`
- **AND** the implementation SHALL NOT use `abs(yaw_delta)` as the exit criterion

#### Scenario: InnerTrace yaw-stall fallback is explicit and parameterized
- **WHEN** prior phase is `InnerTrace`
- **AND** elapsed time since `InnerTrace.enter_capture_time_ms` is at least `CIRCLE_V2_INNER_TRACE_STALL_TIMEOUT_MS`
- **AND** `directed_turn_progress` is less than `CIRCLE_V2_INNER_TRACE_STALL_YAW_MIN_DEG`
- **THEN** `inner_trace_stalled` SHALL be true
- **AND** `CircleV2Reducer` SHALL enter `Idle` and clear the locked direction
- **WHEN** `directed_turn_progress` is at least `CIRCLE_V2_INNER_TRACE_STALL_YAW_MIN_DEG`
- **THEN** the yaw-stall fallback SHALL NOT fire

### Requirement: Circle V2 Reference Plans Replace Rear-Black Entry Paths
Runtime circle reference construction SHALL NOT use rear / side-rear black frontier facts. `InnerTrace` SHALL derive its entrance reference directly from the locked-direction inner circle edge and emit a scene-owned `CircleV2ReferencePlan`. `InnerTrace` SHALL apply `CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M` as a path offset from the inner edge toward the road interior; `0.0` SHALL mean the path is on the observed inner edge. `InnerTrace` SHALL NOT require a road-half-width offset toward the road center.

Runtime Circle V2 single-boundary reference construction SHALL reuse the neutral single-boundary normal-offset helper used by ordinary BEV reference generation. CircleV2 SHALL remain responsible only for choosing the role-specific boundary trace and signed offset; the helper SHALL remain unaware of circle direction, FSM phase, scene telemetry, or candidate arbitration.

`InnerTrace` SHALL NOT use the V3 fixed-slope `P_est` boundary override, patched ordinary row intervals, or ordinary path-builder row override to create the active circle candidate. `ExitTrace` SHALL keep deriving its reference from the locked direction's opposite straight edge, offset by road half width toward the locked direction.

Runtime Circle V2 reference construction SHALL emit a `CircleV2ReferencePlan` only when the role-specific geometry forms a finite leading-contiguous path segment. Single-sample, gapped, or otherwise structurally incomplete observations SHALL be treated as unavailable geometry rather than adapted into circle visual-reference candidates.

#### Scenario: InnerTrace uses inner edge for left circle
- **WHEN** current-frame reference context is `InnerTrace` and direction is `left`
- **AND** the geometry observer can observe a finite leading-contiguous left inner circle edge
- **THEN** `CircleV2ReferenceComposer` SHALL emit a `CircleV2ReferencePlan` with role `InnerTrace`
- **AND** the plan path SHALL use the observed left inner edge samples plus `CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M` rightward
- **AND** it SHALL NOT construct or consume a V3 right-side boundary override

#### Scenario: InnerTrace uses inner edge for right circle
- **WHEN** current-frame reference context is `InnerTrace` and direction is `right`
- **AND** the geometry observer can observe a finite leading-contiguous right inner circle edge
- **THEN** `CircleV2ReferenceComposer` SHALL emit a `CircleV2ReferencePlan` with role `InnerTrace`
- **AND** the plan path SHALL use the observed right inner edge samples plus `CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M` leftward
- **AND** it SHALL NOT construct or consume a V3 left-side boundary override

#### Scenario: InnerTrace path composition uses shared helper
- **WHEN** current-frame reference context is `InnerTrace`
- **AND** the geometry observer can observe a finite leading-contiguous locked-side inner edge trace
- **THEN** CircleV2 reference composition SHALL call the shared single-boundary normal-offset helper with that inner edge trace
- **AND** it SHALL pass `CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M` mapped to the caller-owned signed normal offset for the locked direction
- **AND** `CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M = 0.0` SHALL produce a path on the observed inner edge
- **AND** CircleV2 SHALL NOT maintain a separate private fixed-lateral-offset formula for InnerTrace

#### Scenario: InnerTrace no longer depends on P-point boundary override
- **WHEN** current-frame reference context is `InnerTrace`
- **THEN** active runtime CircleV2 reference generation SHALL NOT require `P_est`
- **AND** it SHALL NOT call the ordinary path builder with patched boundary intervals
- **AND** missing P-point estimation SHALL NOT affect whether an inner-edge reference can be emitted

#### Scenario: ExitTrace keeps existing outer straight edge behavior
- **WHEN** current-frame reference context is `ExitTrace` and direction is `left`
- **THEN** `CircleV2GeometryObserver` SHALL find the right-side straight edge and offset it leftward by `ordinary_road.half_width`
- **WHEN** direction is `right`
- **THEN** it SHALL find the left-side straight edge and offset it rightward by `ordinary_road.half_width`

#### Scenario: ExitTrace path composition uses shared helper
- **WHEN** current-frame reference context is `ExitTrace`
- **AND** the geometry observer can observe the role-specific outer straight edge trace
- **THEN** CircleV2 reference composition SHALL call the shared single-boundary normal-offset helper with that outer edge trace
- **AND** it SHALL pass the existing caller-owned signed road-half-width offset for the locked direction
- **AND** CircleV2 SHALL NOT duplicate the helper's local-direction offset formula

#### Scenario: Missing inner edge geometry does not mutate the FSM
- **WHEN** current-frame reference context is `InnerTrace`
- **AND** the locked-side inner edge cannot produce a finite leading-contiguous segment
- **THEN** `CircleV2StepResult.reference_plan` SHALL be empty
- **AND** `CircleV2Reducer` state progression for that frame SHALL remain the authoritative `next_memory`
- **AND** geometry absence SHALL NOT reset, roll back, or skip a phase

#### Scenario: Helper output absence does not mutate CircleV2 memory
- **WHEN** CircleV2 geometry is available as an edge trace but the shared helper cannot produce a finite leading-contiguous role path
- **THEN** `CircleV2StepResult.reference_plan` SHALL be empty for that frame
- **AND** `CircleV2Reducer` SHALL remain the only owner of `next_memory`
- **AND** helper failure SHALL NOT reset, roll back, or skip a CircleV2 phase

### Requirement: Circle V2 Candidate Adaptation Is Fixed And Confidence-Free
`CircleV2Scene` SHALL output scene-owned reference intent, not a `VisualReferenceCandidate`. Both `InnerTrace` and `ExitTrace` MAY output an optional `CircleV2ReferencePlan`; a reference adapter SHALL convert only accepted reference plans into the existing visual-reference arbitration type using fixed role and direction mapping.

The Circle V2 reference adapter SHALL remain a packaging boundary. It SHALL convert accepted scene-owned reference outputs into the existing visual-reference candidate type, and it SHALL NOT repair, densify, score, validate unavailable geometry, patch sparse rows, or invoke ordinary path generation.

#### Scenario: Accepted inner and exit plans adapt to circle candidates
- **WHEN** `CircleV2ReferencePlan.role` is `InnerTrace` or `ExitTrace`
- **AND** the plan is present
- **THEN** the adapter SHALL set candidate kind from `dir` as `kCircleLeft` or `kCircleRight`
- **AND** it SHALL set the candidate source to a V2-specific source such as `circle_v2_inner` or `circle_v2_exit`
- **AND** it SHALL not compute or consume a scene confidence score

#### Scenario: Idle and Approach do not produce circle candidates
- **WHEN** current-frame circle phase is `Idle` or `Approach`
- **THEN** `CircleV2StepResult.reference_plan` SHALL be empty
- **AND** no circle candidate SHALL be appended for visual-reference arbitration

#### Scenario: Adapter does not repair missing or malformed plans
- **WHEN** `CircleV2StepResult.reference_plan` is empty because role-specific geometry is unavailable
- **THEN** the Circle V2 reference adapter SHALL return no candidate
- **AND** it SHALL NOT synthesize, pad, densify, score, patch rows, call the ordinary path builder, or otherwise repair a candidate path

### Requirement: Circle V2 Composition Respects Cross Suppression Evidence
The runtime composition layer SHALL suppress CircleV2 stepping when cross-exit evidence is present and cross takeover is enabled. This suppression SHALL use the public `cross_exit.present` evidence fact, not the existence of a built cross visual-reference candidate, because ordinary reference repair can intentionally leave the line candidate unavailable.

CircleV2Scene MUST NOT read cross detector internals, cross evidence internals, or cross candidate internals to decide FSM transitions.

#### Scenario: Cross evidence suppresses active CircleV2 without a built cross candidate
- **WHEN** `element_evidence.cross_exit.present` is true
- **AND** cross takeover is enabled
- **AND** the cross candidate cannot be built because the ordinary line candidate is unavailable
- **THEN** the composition layer SHALL not step `CircleV2Scene` for that frame
- **AND** active CircleV2 memory SHALL be reset through scene lifecycle handling
- **AND** this SHALL NOT require cross evidence to enter visual-reference arbitration

### Requirement: Circle V2 Lifecycle And Parameters Are Explicit
The runtime SHALL expose `CIRCLE_V2_ENABLED`, `CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG`, `CIRCLE_V2_EXIT_HOLD_FRAMES`, `CIRCLE_V2_INNER_TRACE_STALL_TIMEOUT_MS`, `CIRCLE_V2_INNER_TRACE_STALL_YAW_MIN_DEG`, and `CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M`. `CircleV2Params` SHALL contain the exit yaw threshold, exit hold frames, InnerTrace stall fallback parameters, and the InnerTrace path offset. The yaw threshold SHALL NOT provide a dangerous zero business default.

The retired V3 fixed-slope entry guide parameters SHALL NOT remain in the active CircleV2 runtime parameter surface unless a future active change reintroduces that behavior.

`BEV_GEOMETRY.NOMINAL_ROAD_HALF_WIDTH_M` SHALL provide the stable road half-width used to build `OrdinaryRoadModel.half_width`. This is a BEV road-geometry parameter, not a `CircleV2Params` member.

`CIRCLE_V2_ENABLED` SHALL be owned by scene composition / scene registry, not by `CircleV2Reducer`.

#### Scenario: Exit hold frames are validated
- **WHEN** runtime parameters are loaded
- **THEN** `CIRCLE_V2_EXIT_HOLD_FRAMES` SHALL be accepted only at values greater than or equal to `2`
- **AND** invalid values SHALL follow the existing runtime parameter parse-failure fallback behavior

#### Scenario: InnerTrace stall fallback parameters are parsed and validated
- **WHEN** runtime parameters are loaded
- **THEN** `CIRCLE_V2_INNER_TRACE_STALL_TIMEOUT_MS` SHALL default to `4000`
- **AND** `CIRCLE_V2_INNER_TRACE_STALL_YAW_MIN_DEG` SHALL default to `16.5`
- **AND** `CIRCLE_V2_INNER_TRACE_STALL_TIMEOUT_MS` SHALL be accepted only at values greater than or equal to `1`
- **AND** `CIRCLE_V2_INNER_TRACE_STALL_YAW_MIN_DEG` SHALL be accepted only as a finite value in `[0, 720]`
- **AND** invalid values SHALL follow the existing runtime parameter parse-failure fallback behavior

#### Scenario: InnerTrace path offset is parsed and validated
- **WHEN** runtime parameters are loaded
- **THEN** `CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M` SHALL default to `0.0`
- **AND** `CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M` SHALL be accepted only as a finite value in `[0, 2]`
- **AND** invalid values SHALL follow the existing runtime parameter parse-failure fallback behavior

#### Scenario: Retired fixed-slope parameters are inactive
- **WHEN** active runtime parameters are loaded
- **THEN** `CIRCLE_V2_ENTRY_FIXED_SLOPE_LEFT_DX_DY` and `CIRCLE_V2_ENTRY_FIXED_SLOPE_RIGHT_DX_DY` SHALL NOT be required by active CircleV2 path generation
- **AND** active defaults and user-facing parameter docs SHALL NOT advertise those keys as current tuning controls

#### Scenario: Active scene is not silently skipped
- **WHEN** `CircleV2Memory.phase` is not `Idle`
- **THEN** the composition layer SHALL continue calling `CircleV2Scene::Step()` for each perception frame
- **AND** missing `MotionArcView` or incomplete `OrdinaryRoadModel` SHALL be handled by an explicit scene reset or global fail-safe path, not by silently freezing memory

#### Scenario: Enable switch is a lifecycle reset
- **WHEN** an implementation supports runtime changes to `CIRCLE_V2_ENABLED`
- **THEN** disabling V2 while memory is active SHALL reset `CircleV2Memory`
- **AND** enabling V2 SHALL start from `Idle` memory
- **AND** this reset SHALL be treated as scene lifecycle management, not as a normal reducer transition

### Requirement: Retired V3 Entry Guide Code Is Archived Only
The V3 P-point fixed-slope boundary-override implementation SHALL be retained only under `new/code/archive/` as historical reference. Active runtime code, active tests, and active CMake targets SHALL NOT include, compile, or depend on archived V3 entry-guide code.

#### Scenario: Archive code is isolated from active runtime
- **WHEN** the active runtime is built
- **THEN** files under `new/code/archive/circle_v2_v3_fixed_slope_entry_guide/` SHALL NOT be compiled into the runtime target
- **AND** active runtime files SHALL NOT include headers from that archive directory

#### Scenario: Reintroducing V3 entry guide requires a new change
- **WHEN** a future implementation wants to use the archived P-point fixed-slope behavior again
- **THEN** it SHALL do so through a new OpenSpec change and active spec update
- **AND** it SHALL NOT silently include archive files into the active runtime
