# reference-tracking-geometry Specification

## Purpose
Define the neutral reference-tracking geometry facts used between selected BEV reference paths, reference-control readiness, and yaw target computation.

## Requirements
### Requirement: Selected Reference Produces Tracking Geometry
The runtime SHALL compute a neutral `ReferenceTrackingGeometry` from the selected, held, or control-time-aligned `BEVReferencePath` after reference usability has been evaluated and before reference-control readiness consumes the control input.

The geometry SHALL include:

- `computed`
- `lateral_offset_m`
- `heading_error_rad`
- `curvature_m_inv`
- `sample_count`
- `reason`

The geometry helper SHALL NOT read visual candidate kind, CircleV2 state, cross evidence, element evidence, reference arbitration policy, safety-gate state, wheel mixer state, PWM state, or motor adapter state.

#### Scenario: Geometry is computed after reference selection
- **WHEN** the control loop has a selected, held, or time-aligned reference path with usable leading samples
- **THEN** it SHALL compute `ReferenceTrackingGeometry` from that final control reference path
- **AND** the geometry SHALL be independent of whether the path came from ordinary line following, single-boundary repair, CircleV2, cross, or hold-last continuity

#### Scenario: Geometry helper remains neutral
- **WHEN** reviewers inspect the geometry helper dependencies
- **THEN** the helper SHALL depend only on reference path, reference usability, and BEV control-model parameters needed for geometry fitting
- **AND** it SHALL NOT depend on scene memory, visual reference selection, control gates, wheel mixing, or actuator output

### Requirement: Tracking Geometry Uses A Simple Quadratic Fit Contract
The first release SHALL estimate tracking geometry from the leading usable prefix of the selected reference path using a deterministic quadratic fit:

```text
y = a*x^2 + b*x + c
```

where `x = forward_m` and `y = lateral_m`. It SHALL report:

- `lateral_offset_m = c`
- `heading_error_rad = atan(b)`
- `curvature_m_inv = 2a / (1 + b^2)^(3/2)`

It SHALL require at least `BEV_CONTROL_MODEL.TRACKING_FIT_MIN_SAMPLES` usable leading samples before reporting `computed=true`.

The first release SHALL NOT introduce forward-window or anchor parameters unless they are explicitly added by a later change.

#### Scenario: Insufficient samples fail closed
- **WHEN** the selected reference path has fewer than `TRACKING_FIT_MIN_SAMPLES` usable leading samples
- **THEN** `ReferenceTrackingGeometry.computed` SHALL be `false`
- **AND** `ReferenceTrackingGeometry.reason` SHALL explain that tracking geometry was unavailable because of insufficient samples or unusable reference input

#### Scenario: Degenerate or non-finite fit fails closed
- **WHEN** the leading usable samples cannot produce a finite quadratic fit or the derived lateral offset, heading error, or curvature is non-finite
- **THEN** `ReferenceTrackingGeometry.computed` SHALL be `false`
- **AND** the helper SHALL NOT invent fallback curvature, reuse old geometry, or silently downgrade to a hidden line-fit policy

#### Scenario: Straight and curved paths expose separate facts
- **WHEN** a straight path is fitted
- **THEN** curvature SHALL be near zero while lateral offset and heading reflect the fitted line
- **AND** when a curved path is fitted, curvature SHALL be exposed as a separate fact instead of being hidden inside a weighted future lateral average

### Requirement: Reference Control Readiness Consumes Tracking Geometry
Reference-control readiness SHALL require `ReferenceTrackingGeometry.computed=true` and finite control geometry facts before allowing reference steering control. It SHALL NOT treat the old weighted lateral-error estimate as the authoritative readiness input.

#### Scenario: Uncomputed tracking geometry vetoes reference control
- **WHEN** reference usability is acceptable but tracking geometry is not computed
- **THEN** reference-control readiness SHALL report not ready
- **AND** the not-ready reason SHALL identify tracking geometry as the missing control input

#### Scenario: Readiness stays separate from control gains
- **WHEN** reviewers inspect reference-control readiness
- **THEN** it SHALL NOT read lateral, heading, curvature, gyro, wheel, or PWM gain values
- **AND** it SHALL only decide whether required reference-control facts are present and usable

### Requirement: Yaw Controller Consumes Geometry Terms
The yaw turn target computation SHALL consume `ReferenceTrackingGeometry` instead of a single weighted lateral-error float. It SHALL expose separate term outputs for:

- `lateral_term`
- `heading_term`
- `curvature_term`
- `turn_output_target`

The first release SHALL support separate runtime gains:

- `BEV_CONTROL_MODEL.LATERAL_OFFSET_TO_WHEEL_DELTA_GAIN`
- `BEV_CONTROL_MODEL.HEADING_ERROR_TO_WHEEL_DELTA_GAIN`
- `BEV_CONTROL_MODEL.CURVATURE_TO_WHEEL_DELTA_GAIN`

The three geometry terms SHALL use the same `speed_scale = effective_speed_target / RUNNING_SPEED_TARGET` convention, so each gain represents the nominal turn-output gain at `RUNNING_SPEED_TARGET`.

#### Scenario: Term decomposition is explicit
- **WHEN** yaw control computes a turn target from tracking geometry
- **THEN** the computation SHALL make lateral, heading, and curvature contributions separately observable
- **AND** the final `turn_output_target` SHALL be derived from those terms before existing output limiting and downstream wheel mixing

#### Scenario: Wheel mixer remains unaware of geometry
- **WHEN** wheel targets are generated
- **THEN** the wheel mixer SHALL consume the applied turn output as before
- **AND** it SHALL NOT read `lateral_offset_m`, `heading_error_rad`, `curvature_m_inv`, or their gains

### Requirement: Runtime Parameter Surface Separates Geometry Gains
The runtime parameter surface SHALL expose separate BEV control-model parameters for lateral offset gain, heading error gain, curvature gain, and tracking fit minimum sample count. The old `LATERAL_ERROR_TO_WHEEL_DELTA_GAIN` name MAY be accepted only as a migration alias for lateral offset gain and MUST NOT preserve the old weighted-future-lateral control semantics.

#### Scenario: Defaults describe the new control surface
- **WHEN** runtime default parameters and generated config snapshots are inspected
- **THEN** the BEV control-model group SHALL include lateral offset, heading error, curvature, and tracking fit minimum sample parameters
- **AND** the values SHALL be sufficient to construct `ReferenceTrackingGeometry` and yaw-control term decomposition without hidden constants
