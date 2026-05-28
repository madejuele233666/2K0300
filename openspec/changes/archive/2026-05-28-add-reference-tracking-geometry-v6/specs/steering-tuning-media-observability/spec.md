## MODIFIED Requirements

### Requirement: Runtime Steering Snapshot Exposes The Current Reference/Control Chain
The runtime SHALL expose a project-owned steering tuning snapshot that can explain one control cycle from perception health through selected reference facts, reference usability, tracking geometry, reference-control readiness, safety gate, yaw target, and final actuator output. At minimum, the accepted snapshot SHALL include these grouped objects:

- `perception_health`
- `reference`
- `eligibility`
- `tracking_geometry`
- `reference_control`
- `safety_gate`
- `degraded`
- `yaw_control`
- `actuator`
- `element_evidence`
- `circle_v2`

The `tracking_geometry` group SHALL include computed state, lateral offset, heading error, curvature, sample count, and reason. The `yaw_control` group SHALL include lateral, heading, and curvature term decomposition in addition to the final turn target.

#### Scenario: Steering-chain evidence exposes tracking geometry
- **WHEN** reviewers inspect `control.steering_snapshot` or steering media image-frame `steering_snapshot`
- **THEN** they SHALL be able to identify `tracking_geometry.computed`, `tracking_geometry.lateral_offset_m`, `tracking_geometry.heading_error_rad`, `tracking_geometry.curvature_m_inv`, `tracking_geometry.sample_count`, and `tracking_geometry.reason`
- **AND** they SHALL be able to identify `yaw_control.lateral_term`, `yaw_control.heading_term`, `yaw_control.curvature_term`, and `yaw_control.turn_output_target`

#### Scenario: Legacy lateral error is not the main control fact
- **WHEN** legacy lateral-error fields remain visible for comparison during migration
- **THEN** the public evidence SHALL still expose tracking geometry as the V6 control input
- **AND** consumers SHALL NOT need to infer curvature-aware control behavior from `lateral_error.weighted_lateral_error_m`
