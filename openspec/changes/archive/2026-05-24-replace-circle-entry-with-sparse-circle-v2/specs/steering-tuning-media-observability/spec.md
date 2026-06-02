## ADDED Requirements

### Requirement: Steering Observability Exposes Circle V2 State
The project-owned steering snapshot and steering media image-frame header SHALL expose CircleV2 telemetry when CircleV2 is registered. The telemetry SHALL distinguish the current-frame visible phase from the next stored memory phase.

At minimum, the accepted telemetry SHALL include:

- `frame_phase`
- `next_phase`
- `dir`
- `reference_role`
- `reason`

#### Scenario: ExitTrace final frame is explainable
- **WHEN** an `ExitTrace` frame is the final held frame before returning to idle
- **THEN** public telemetry SHALL be able to report `frame_phase=ExitTrace`
- **AND** it SHALL be able to report `next_phase=Idle`
- **AND** the selected or candidate reference source SHALL remain explainable as a CircleV2 exit reference for that frame

#### Scenario: Geometry absence is visible without changing phase
- **WHEN** CircleV2 cannot construct the reference geometry for the current reference role
- **THEN** telemetry SHALL expose a deterministic reason such as `GeometryUnavailable`
- **AND** public evidence SHALL allow reviewers to distinguish an absent reference plan from a reducer reset

### Requirement: Steering Media Config Snapshot Uses Circle V2 Parameters
The steering media `config_snapshot.param_snapshot` object SHALL include the active V2 circle parameter surface needed to interpret CircleV2 telemetry and candidate output:

- `CIRCLE_V2_ENABLED`
- `CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG`
- `CIRCLE_V2_EXIT_HOLD_FRAMES`

Old `CIRCLE_ENTRY_*` and old circle evidence fields SHALL NOT be required to interpret V2 behavior.

#### Scenario: Config snapshot carries V2 circle lifecycle and exit gates
- **WHEN** the runtime publishes a steering media `config_snapshot`
- **THEN** the header SHALL include the active `CIRCLE_V2_*` values
- **AND** those values SHALL match startup-loaded runtime parameters
- **AND** reviewers SHALL be able to determine the configured B -> C yaw threshold and C hold duration from the snapshot

#### Scenario: Old circle_entry parameter expectations are removed
- **WHEN** steering media selftests or host parsers validate a V2 config snapshot
- **THEN** they SHALL not require `CIRCLE_ENTRY_TAKEOVER_ENABLED`
- **AND** they SHALL not require `CIRCLE_ENTRY_MIN_FRONTIER_POINTS`, `CIRCLE_ENTRY_DIRECTION_MIN_LATERAL_M`, `CIRCLE_ENTRY_MAX_INTERPOLATION_GAP_M`, or `CIRCLE_ENTRY_MAX_JOIN_JUMP_M`
- **AND** they SHALL not require old circle evidence keys such as `CIRCLE_EVIDENCE_ENABLED`, `CIRCLE_OPEN_EXPANSION_MIN_M`, or `CIRCLE_PRESENT_CONFIDENCE_MIN`

### Requirement: Circle V2 Reference Sources Are Observable
When CircleV2 adapts a `CircleV2ReferencePlan` into a `VisualReferenceCandidate`, the selected-reference and media surfaces SHALL expose V2-specific source names rather than old `circle_entry` mode/source names.

#### Scenario: InnerTrace candidate source identifies V2 ownership
- **WHEN** CircleV2 outputs an `InnerTrace` reference plan and that candidate is selected
- **THEN** public steering evidence SHALL identify the reference source as a CircleV2 inner trace source such as `circle_v2_inner`
- **AND** it SHALL not identify the selected reference as old `circle_entry`

#### Scenario: ExitTrace candidate source identifies V2 ownership
- **WHEN** CircleV2 outputs an `ExitTrace` reference plan and that candidate is selected
- **THEN** public steering evidence SHALL identify the reference source as a CircleV2 exit trace source such as `circle_v2_exit`
- **AND** it SHALL not identify the selected reference as old `circle_entry`
