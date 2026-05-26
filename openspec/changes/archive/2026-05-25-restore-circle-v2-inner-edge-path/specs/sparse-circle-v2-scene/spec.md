## MODIFIED Requirements

### Requirement: Circle V2 Reference Plans Replace Rear-Black Entry Paths
Runtime circle reference construction SHALL NOT use rear / side-rear black frontier facts. `InnerTrace` SHALL derive its entrance reference directly from the locked-direction inner circle edge and emit a scene-owned `CircleV2ReferencePlan`. `InnerTrace` MAY place the reference path on or close to that inner edge; it SHALL NOT require a road-half-width offset toward the road center.

`InnerTrace` SHALL NOT use the V3 fixed-slope `P_est` boundary override, patched ordinary row intervals, or ordinary path-builder row override to create the active circle candidate. `ExitTrace` SHALL keep deriving its reference from the locked direction's opposite straight edge, offset by road half width toward the locked direction.

Runtime Circle V2 reference construction SHALL emit a `CircleV2ReferencePlan` only when the role-specific geometry forms a finite leading-contiguous path segment. Single-sample, gapped, or otherwise structurally incomplete observations SHALL be treated as unavailable geometry rather than adapted into circle visual-reference candidates.

#### Scenario: InnerTrace uses inner edge for left circle
- **WHEN** current-frame reference context is `InnerTrace` and direction is `left`
- **AND** the geometry observer can observe a finite leading-contiguous left inner circle edge
- **THEN** `CircleV2ReferenceComposer` SHALL emit a `CircleV2ReferencePlan` with role `InnerTrace`
- **AND** the plan path SHALL use the observed left inner edge samples directly or within a minimal implementation-defined near-edge adjustment
- **AND** it SHALL NOT construct or consume a V3 right-side boundary override

#### Scenario: InnerTrace uses inner edge for right circle
- **WHEN** current-frame reference context is `InnerTrace` and direction is `right`
- **AND** the geometry observer can observe a finite leading-contiguous right inner circle edge
- **THEN** `CircleV2ReferenceComposer` SHALL emit a `CircleV2ReferencePlan` with role `InnerTrace`
- **AND** the plan path SHALL use the observed right inner edge samples directly or within a minimal implementation-defined near-edge adjustment
- **AND** it SHALL NOT construct or consume a V3 left-side boundary override

#### Scenario: InnerTrace no longer depends on P-point boundary override
- **WHEN** current-frame reference context is `InnerTrace`
- **THEN** active runtime CircleV2 reference generation SHALL NOT require `P_est`
- **AND** it SHALL NOT call the ordinary path builder with patched boundary intervals
- **AND** missing P-point estimation SHALL NOT affect whether an inner-edge reference can be emitted

#### Scenario: Missing inner edge geometry does not mutate the FSM
- **WHEN** current-frame reference context is `InnerTrace`
- **AND** the locked-side inner edge cannot produce a finite leading-contiguous segment
- **THEN** `CircleV2StepResult.reference_plan` SHALL be empty
- **AND** `CircleV2Reducer` state progression for that frame SHALL remain the authoritative `next_memory`
- **AND** geometry absence SHALL NOT reset, roll back, or skip a phase

#### Scenario: ExitTrace keeps existing outer straight edge behavior
- **WHEN** current-frame reference context is `ExitTrace` and direction is `left`
- **THEN** `CircleV2GeometryObserver` SHALL find the right-side straight edge and offset it leftward by `ordinary_road.half_width`
- **WHEN** direction is `right`
- **THEN** it SHALL find the left-side straight edge and offset it rightward by `ordinary_road.half_width`

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

### Requirement: Circle V2 Lifecycle And Parameters Are Explicit
The runtime SHALL expose `CIRCLE_V2_ENABLED`, `CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG`, `CIRCLE_V2_EXIT_HOLD_FRAMES`, `CIRCLE_V2_INNER_TRACE_STALL_TIMEOUT_MS`, and `CIRCLE_V2_INNER_TRACE_STALL_YAW_MIN_DEG`. `CircleV2Params` SHALL contain the exit yaw threshold, exit hold frames, and InnerTrace stall fallback parameters. The yaw threshold SHALL NOT provide a dangerous zero business default.

The retired V3 fixed-slope entry guide parameters SHALL NOT remain in the active CircleV2 runtime parameter surface unless a future active change reintroduces that behavior.

`CIRCLE_V2_ENABLED` SHALL be owned by scene composition / scene registry, not by `CircleV2Reducer`.

#### Scenario: Retired fixed-slope parameters are inactive
- **WHEN** active runtime parameters are loaded
- **THEN** `CIRCLE_V2_ENTRY_FIXED_SLOPE_LEFT_DX_DY` and `CIRCLE_V2_ENTRY_FIXED_SLOPE_RIGHT_DX_DY` SHALL NOT be required by active CircleV2 path generation
- **AND** active defaults and user-facing parameter docs SHALL NOT advertise those keys as current tuning controls

#### Scenario: Active scene is not silently skipped
- **WHEN** `CircleV2Memory.phase` is not `Idle`
- **THEN** the composition layer SHALL continue calling `CircleV2Scene::Step()` for each perception frame
- **AND** missing `MotionArcView` or incomplete `OrdinaryRoadModel` SHALL be handled by an explicit scene reset or global fail-safe path, not by silently freezing memory

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
