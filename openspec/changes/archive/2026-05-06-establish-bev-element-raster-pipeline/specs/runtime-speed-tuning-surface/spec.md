## ADDED Requirements

### Requirement: Runtime Parameter Snapshots Include BEV Element Raster Settings
The accepted runtime parameter snapshot surfaces SHALL include `BEV_ELEMENT_RASTER` so host tools and evidence bundles can interpret whether runtime raster facts were enabled and what raster width was used.

#### Scenario: Parameter snapshot carries raster settings
- **WHEN** the runtime publishes a config or telemetry parameter snapshot that includes BEV steering context
- **THEN** the snapshot SHALL include `BEV_ELEMENT_RASTER.ENABLED`
- **AND** it SHALL include `BEV_ELEMENT_RASTER.WIDTH`
- **AND** the values SHALL reflect the startup-loaded runtime parameters or their documented fallback defaults
