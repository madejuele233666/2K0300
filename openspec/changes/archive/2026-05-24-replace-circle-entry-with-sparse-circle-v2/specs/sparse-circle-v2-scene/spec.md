## ADDED Requirements

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

#### Scenario: Motion arc is an ability view rather than scene-owned history
- **WHEN** `CircleV2EventObserver` needs to evaluate the InnerTrace exit gate
- **THEN** it SHALL query `MotionArcView::YawDeltaRad(from_capture_time_ms, to_capture_time_ms)`
- **AND** `CircleV2Scene` SHALL NOT depend on the concrete `MotionHistory`, IMU adapter, control tick, or yaw integration storage type

### Requirement: Circle V2 FSM Uses The Minimal Four Phase Sequence
`CircleV2Memory` SHALL contain only the circle phase, locked direction, phase enter capture time, and phase frame index. The only valid phase sequence SHALL be:

```text
Idle -> Approach -> InnerTrace -> ExitTrace -> Idle
```

`Approach` SHALL only be entered from `Idle`. `ExitTrace` hold SHALL serve as the cooldown; no separate cooldown phase SHALL be introduced.

#### Scenario: Direction and phase invariants are maintained
- **WHEN** `CircleV2Memory.phase` is `Idle`
- **THEN** `CircleV2Memory.dir` SHALL be `None`
- **WHEN** `CircleV2Memory.phase` is not `Idle`
- **THEN** `CircleV2Memory.dir` SHALL be `left` or `right`
- **AND** `EnterIdle()` SHALL clear phase, direction, enter timestamp, and phase frame index

#### Scenario: Reducer rejects implicit transitions
- **WHEN** `CircleV2Reducer` receives events for any frame
- **THEN** it SHALL only produce `Idle -> Approach`, `Approach -> InnerTrace`, `InnerTrace -> ExitTrace`, or `ExitTrace -> Idle`
- **AND** it SHALL NOT produce `Approach -> Idle`, `InnerTrace -> Idle`, `ExitTrace -> Approach`, `Idle -> InnerTrace`, or `Idle -> ExitTrace`

#### Scenario: ExitTrace hold has fixed frame semantics
- **WHEN** `ExitTrace` begins with `phase_frame_index = 0`
- **AND** `CIRCLE_V2_EXIT_HOLD_FRAMES` is `3`
- **THEN** the runtime SHALL output exactly three `ExitTrace` reference frames
- **AND** the third frame SHALL still expose `frame_phase = ExitTrace`
- **AND** the third frame's `next_memory.phase` SHALL become `Idle`

### Requirement: Circle V2 Events Are Phase-Gated
`CircleV2EventObserver` SHALL translate visual and motion facts into transition events, and `CircleV2Reducer` SHALL read only those events. The event observer SHALL gate event production by prior phase:

- `Idle`: only `detected_dir`
- `Approach`: only locked-direction `entry_gate_reached`
- `InnerTrace`: only `exit_gate_reached`
- `ExitTrace`: no detected, entry, or exit events

#### Scenario: Idle uses existing Phase1 direction semantics
- **WHEN** prior phase is `Idle`
- **THEN** `ObserveCirclePhase1Cue` SHALL return `left`, `right`, or `none` with the same Phase1 direction semantics as the old runtime Phase1 circle cue for equivalent sparse-row input
- **AND** golden parity tests SHALL cover old Phase1 `left`, `right`, and `none` results

#### Scenario: Approach consumes only locked-direction bottom expansion
- **WHEN** prior phase is `Approach` and locked direction is `left`
- **THEN** only left-side bottom-row expansion SHALL be allowed to set `entry_gate_reached`
- **AND** right-side expansion SHALL NOT move the FSM to `InnerTrace`
- **WHEN** locked direction is `right`
- **THEN** only right-side bottom-row expansion SHALL be allowed to set `entry_gate_reached`

#### Scenario: InnerTrace exit yaw is direction-normalized
- **WHEN** prior phase is `InnerTrace`
- **THEN** `exit_gate_reached` SHALL compare `directed_turn_angle` to `CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG`
- **AND** `directed_turn_angle` SHALL be `CircleTurnSign(dir) * MotionArcView::YawDeltaRad(InnerTrace.enter_capture_time_ms, current_capture_time_ms)`
- **AND** the implementation SHALL NOT use `abs(yaw_delta)` as the exit criterion

### Requirement: Circle V2 Reference Plans Replace Rear-Black Entry Paths
Runtime circle reference construction SHALL NOT use rear / side-rear black frontier facts. `InnerTrace` SHALL derive its reference from the nearest locked-direction side edge relative to the ordinary center path, offset by road half width toward the opposite side. `ExitTrace` SHALL derive its reference from the locked direction's opposite straight edge, offset by road half width toward the locked direction.

#### Scenario: InnerTrace follows the inner circle edge
- **WHEN** current-frame reference context is `InnerTrace` and direction is `left`
- **THEN** `CircleV2GeometryObserver` SHALL search from the ordinary center path toward the left side and select the nearest left-side edge as the inner edge
- **AND** `CircleV2ReferenceComposer` SHALL offset that edge rightward by `ordinary_road.half_width`
- **WHEN** direction is `right`
- **THEN** it SHALL mirror the same rule using the right-side nearest edge and leftward offset

#### Scenario: ExitTrace follows the outer straight edge
- **WHEN** current-frame reference context is `ExitTrace` and direction is `left`
- **THEN** `CircleV2GeometryObserver` SHALL find the right-side straight edge and offset it leftward by `ordinary_road.half_width`
- **WHEN** direction is `right`
- **THEN** it SHALL find the left-side straight edge and offset it rightward by `ordinary_road.half_width`

#### Scenario: Missing geometry does not mutate the FSM
- **WHEN** `CircleV2GeometryObserver` cannot construct the geometry required for the current reference role
- **THEN** `CircleV2StepResult.reference_plan` SHALL be empty
- **AND** `CircleV2Reducer` state progression for that frame SHALL remain the authoritative `next_memory`
- **AND** geometry absence SHALL NOT reset, roll back, or skip a phase

### Requirement: Circle V2 Candidate Adaptation Is Fixed And Confidence-Free
`CircleV2Scene` SHALL output an optional `CircleV2ReferencePlan`, not a `VisualReferenceCandidate`. A reference adapter SHALL convert present plans into the existing visual-reference arbitration type using fixed role and direction mapping.

#### Scenario: Inner and exit plans adapt to circle candidates
- **WHEN** `CircleV2ReferencePlan.role` is `InnerTrace` or `ExitTrace`
- **THEN** the adapter SHALL set candidate kind from `dir` as `kCircleLeft` or `kCircleRight`
- **AND** it SHALL set the candidate source to a V2-specific source such as `circle_v2_inner` or `circle_v2_exit`
- **AND** it SHALL not compute or consume a scene confidence score

#### Scenario: Idle and Approach do not produce circle candidates
- **WHEN** current-frame circle phase is `Idle` or `Approach`
- **THEN** `CircleV2StepResult.reference_plan` SHALL be empty
- **AND** no circle candidate SHALL be appended for visual-reference arbitration

### Requirement: Circle V2 Lifecycle And Parameters Are Explicit
The runtime SHALL expose `CIRCLE_V2_ENABLED`, `CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG`, and `CIRCLE_V2_EXIT_HOLD_FRAMES`. `CircleV2Params` SHALL contain only `exit_yaw_threshold_rad` and `exit_hold_frames`, and SHALL NOT provide a dangerous zero business default for the yaw threshold.

`CIRCLE_V2_ENABLED` SHALL be owned by scene composition / scene registry, not by `CircleV2Reducer`.

#### Scenario: Exit hold frames are validated
- **WHEN** runtime parameters are loaded
- **THEN** `CIRCLE_V2_EXIT_HOLD_FRAMES` SHALL be accepted only at values greater than or equal to `2`
- **AND** invalid values SHALL follow the existing runtime parameter parse-failure fallback behavior

#### Scenario: Active scene is not silently skipped
- **WHEN** `CircleV2Memory.phase` is not `Idle`
- **THEN** the composition layer SHALL continue calling `CircleV2Scene::Step()` for each perception frame
- **AND** missing `MotionArcView` or incomplete `OrdinaryRoadModel` SHALL be handled by an explicit scene reset or global fail-safe path, not by silently freezing memory

#### Scenario: Enable switch is a lifecycle reset
- **WHEN** an implementation supports runtime changes to `CIRCLE_V2_ENABLED`
- **THEN** disabling V2 while memory is active SHALL reset `CircleV2Memory`
- **AND** enabling V2 SHALL start from `Idle` memory
- **AND** this reset SHALL be treated as scene lifecycle management, not as a normal reducer transition
