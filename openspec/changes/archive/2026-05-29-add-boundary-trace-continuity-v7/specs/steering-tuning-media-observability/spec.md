## MODIFIED Requirements

### Requirement: Steering Media Config Snapshot Exposes V5 BEV Geometry Controls
The steering media `config_snapshot.param_snapshot.BEV_GEOMETRY` object SHALL include the BEV geometry controls needed to interpret sparse reference behavior from V5 through V7:

- `SPARSE_ROW_COUNT`
- `REFERENCE_LATERAL_JUMP_GATE_M`
- `BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M`

#### Scenario: Config snapshot carries sparse row, lateral jump, and boundary trace settings
- **WHEN** the runtime publishes a steering media `config_snapshot`
- **THEN** the header SHALL include `param_snapshot.BEV_GEOMETRY.SPARSE_ROW_COUNT`
- **AND** it SHALL include `param_snapshot.BEV_GEOMETRY.REFERENCE_LATERAL_JUMP_GATE_M`
- **AND** it SHALL include `param_snapshot.BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M`
- **AND** all three fields SHALL reflect startup-loaded runtime parameters
