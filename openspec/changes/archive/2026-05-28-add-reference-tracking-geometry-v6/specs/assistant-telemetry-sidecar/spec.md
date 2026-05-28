## MODIFIED Requirements

### Requirement: Assistant Telemetry Exposes Read-Only Steering Facts
The assistant telemetry sidecar SHALL expose the same read-only V6 tracking geometry and yaw-term decomposition facts as the project-owned steering snapshot while preserving the existing assistant command/session boundary.

The telemetry SHALL include:

- `tracking_geometry.computed`
- `tracking_geometry.lateral_offset_m`
- `tracking_geometry.heading_error_rad`
- `tracking_geometry.curvature_m_inv`
- `tracking_geometry.sample_count`
- `tracking_geometry.reason`
- `yaw_control.lateral_term`
- `yaw_control.heading_term`
- `yaw_control.curvature_term`
- `yaw_control.turn_output_target`

#### Scenario: Assistant telemetry mirrors control facts without owning control
- **WHEN** the assistant telemetry JSON is emitted during a steering run
- **THEN** it SHALL include the tracking geometry and yaw-term fields produced by the runtime control snapshot
- **AND** it SHALL NOT recompute tracking geometry, alter reference-control readiness, mutate runtime parameters, or create a new command path

#### Scenario: Assistant command boundary remains unchanged
- **WHEN** reviewers inspect assistant protocol and service changes for V6
- **THEN** accepted inbound commands SHALL remain limited to the existing assistant command set
- **AND** the new tracking geometry and yaw-term fields SHALL remain telemetry-only facts
