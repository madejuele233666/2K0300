## ADDED Requirements

### Requirement: Steering protocol exposes only the simple contract

Assistant telemetry, steering media headers, and board steering snapshots SHALL expose reference/control facts without obsolete telemetry labels.

#### Scenario: Minimal steering fields are present

- **WHEN** a steering snapshot is emitted
- **THEN** it SHALL include `perception_health.*`, `reference.*`, `eligibility.*`, `curvature.*`, `reference_control.*`, `safety_gate.*`, `degraded.*`, `yaw_control.*`, and `actuator.*`

#### Scenario: Removed fields are absent

- **WHEN** assistant telemetry or steering media headers are emitted
- **THEN** obsolete telemetry labels SHALL be absent

### Requirement: Perception and yaw-rate control are decoupled

Perception SHALL publish `curvature_command`; the control loop SHALL compute `yaw_rate_target` from that curvature and the current effective speed target.

### Requirement: Safety gate owns safety vetoes

Reference-control readiness SHALL NOT consume low voltage, projector health, stale perception, IMU validity, or encoder validity. The safety gate SHALL consume those facts and produce the actuator veto reason.

#### Scenario: Projector is invalid while hold facts are visible

- **WHEN** perception health reports `projector_ok=false`
- **THEN** debug MAY still show hold reference facts
- **AND** safety gate SHALL veto with `perception_invalid`

#### Scenario: Public legacy angular target is absent

- **WHEN** assistant telemetry, steering media headers, stdout snapshots, or config snapshots are emitted
- **THEN** they SHALL NOT expose the removed legacy angular target field or gain key

### Requirement: BEV geometry parameter surface is minimal

`BEV_GEOMETRY` SHALL contain only forward samples, BEV lateral scan range, and lateral sampling step.

#### Scenario: Config snapshot is emitted

- **WHEN** a steering media config snapshot is encoded
- **THEN** it SHALL include `FORWARD_SAMPLE_*`, `SEARCH_LATERAL_LIMIT_M`, and `LATERAL_STEP_M`
- **AND** it SHALL NOT include removed lane-width, continuity, row-step, or image-border BEV geometry keys
