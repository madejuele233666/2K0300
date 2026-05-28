## ADDED Requirements

### Requirement: Sparse Row Count Uses The Original Forward-Sample Prefix
The ordinary sparse BEV scanner SHALL expose `BEV_GEOMETRY.SPARSE_ROW_COUNT` as the number of enabled sparse forward rows. The default SHALL be `24`. A value of `N` SHALL enable only `FORWARD_SAMPLE_0` through `FORWARD_SAMPLE_(N-1)` and SHALL NOT redistribute those rows across the original forward range.

#### Scenario: Twelve sparse rows means the first twelve configured rows
- **WHEN** `BEV_GEOMETRY.SPARSE_ROW_COUNT` is `12`
- **THEN** sparse scanning SHALL scan exactly the original `FORWARD_SAMPLE_0` through `FORWARD_SAMPLE_11`
- **AND** it SHALL not scan or output current-frame visual samples for `FORWARD_SAMPLE_12` through `FORWARD_SAMPLE_23`

#### Scenario: Sparse row count validates at load time
- **WHEN** runtime parameters are loaded
- **THEN** `BEV_GEOMETRY.SPARSE_ROW_COUNT` SHALL be accepted only in the inclusive range `1..24`
- **AND** invalid values SHALL follow the existing runtime parameter parse-failure fallback behavior

### Requirement: Lateral Jump Gate Is Explicit And Disabled By Default
The ordinary reference builder SHALL expose `BEV_GEOMETRY.REFERENCE_LATERAL_JUMP_GATE_M` as the explicit gate used by existing adjacent lateral-jump checks. The default SHALL be `1000.0`, which disables this gate for normal BEV search ranges.

The system SHALL NOT use `BEV_GEOMETRY.LATERAL_STEP_M` as a hidden business substitute for reference path continuity. `LATERAL_STEP_M` SHALL remain only the BEV lateral sampling resolution.

#### Scenario: Default lateral jump gate does not reject normal visual paths
- **WHEN** adjacent current-frame visual samples differ laterally by normal track-scale amounts
- **AND** `BEV_GEOMETRY.REFERENCE_LATERAL_JUMP_GATE_M` is the default `1000.0`
- **THEN** the ordinary builder SHALL NOT reject the candidate because of the old lateral jump gate
- **AND** path output SHALL rely on leading continuity, finite samples, downstream minimum samples, and the BEV connectivity gate

#### Scenario: Parameter remains available for explicit experiments
- **WHEN** a developer intentionally configures a smaller `BEV_GEOMETRY.REFERENCE_LATERAL_JUMP_GATE_M`
- **THEN** existing adjacent-jump code MAY use that value as its threshold
- **AND** that behavior SHALL be explicit in the loaded parameter snapshot
