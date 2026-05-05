## ADDED Requirements

### Requirement: Cross-Exit Evidence Is A BEV Metric Visual Fact
The runtime SHALL derive `cross_exit` element evidence only from current-frame BEV metric visual facts produced by sparse BEV row scans. The detector MUST publish whether the evidence is present, its confidence, BEV metric bounds, sample support counts, and an explanatory reason.

#### Scenario: Cross evidence reports present from supported wide rows
- **WHEN** sparse BEV row scans contain enough contiguous sampleable rows with wide white support
- **THEN** `element_evidence.cross_exit.present` SHALL be true
- **AND** the evidence SHALL include finite forward and lateral metric bounds, non-zero support counts, confidence, and reason `present`

#### Scenario: Cross evidence fails closed when support is insufficient
- **WHEN** sparse BEV row scans are absent, have insufficient sampleable support, have no supported wide rows, or only low-confidence support
- **THEN** `element_evidence.cross_exit.present` SHALL be false
- **AND** the evidence SHALL expose a deterministic reason such as `no_sparse_rows`, `insufficient_sampleable_support`, `wide_white_rows_absent`, or `low_confidence`

### Requirement: Element Detection Is Isolated From Control Owners
The `cross_exit` detector SHALL NOT read hold memory, safety state, IMU, encoder, low-voltage state, actuator state, yaw memory, or `PerceptionResult`. Downstream usability, lateral-error, readiness, safety, yaw, and actuator logic SHALL NOT read element evidence internals.

#### Scenario: Runtime control remains line-only by default
- **WHEN** `cross_exit` evidence is present and the takeover parameter remains at its default false value
- **THEN** the runtime SHALL continue selecting visual references from the line candidate only
- **AND** the evidence MAY report a built candidate whose `included_in_arbitration` flag is false

### Requirement: Cross Candidate Takeover Is Explicitly Disabled By Default
The runtime SHALL include a runtime parameter controlling whether a built `cross_exit` candidate may enter visual-reference arbitration. The default SHALL be disabled in both `RuntimeParameters{}` and `default_params.json`.

#### Scenario: Candidate summary distinguishes builder outcome from arbitration inclusion
- **WHEN** the runtime evaluates a `cross_exit` candidate
- **THEN** the public evidence SHALL include candidate summary fields for `built`, `takeover_enabled`, `included_in_arbitration`, and `reason`
- **AND** `built=false` SHALL distinguish unsupported evidence or missing line geometry from a built candidate excluded only because takeover remains disabled

#### Scenario: Enabling takeover is an explicit parameter choice
- **WHEN** the takeover parameter is false
- **THEN** a built `cross_exit` candidate SHALL NOT be passed to visual-reference orchestration
- **WHEN** the takeover parameter is true
- **THEN** a built `cross_exit` candidate MAY be passed to visual-reference orchestration, but it still MUST pass existing candidate validation and downstream reference-control gates
