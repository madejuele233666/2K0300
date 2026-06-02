## ADDED Requirements

### Requirement: Steering Media Config Snapshot Exposes V5 BEV Geometry Controls
The steering media `config_snapshot.param_snapshot.BEV_GEOMETRY` object SHALL include the BEV geometry controls needed to interpret V5 sparse reference behavior:

- `SPARSE_ROW_COUNT`
- `REFERENCE_LATERAL_JUMP_GATE_M`

#### Scenario: Config snapshot carries sparse row and lateral jump settings
- **WHEN** the runtime publishes a steering media `config_snapshot`
- **THEN** the header SHALL include `param_snapshot.BEV_GEOMETRY.SPARSE_ROW_COUNT`
- **AND** it SHALL include `param_snapshot.BEV_GEOMETRY.REFERENCE_LATERAL_JUMP_GATE_M`
- **AND** both fields SHALL reflect startup-loaded runtime parameters
