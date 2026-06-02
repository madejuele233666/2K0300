## ADDED Requirements

### Requirement: Public Steering Snapshots Expose Element Evidence Facts
The project-owned public steering snapshot SHALL include a read-only `element_evidence` group. The first accepted element evidence group SHALL be `cross_exit`, and it SHALL expose detector facts plus candidate summary status without becoming a control authority.

#### Scenario: Steering snapshot includes cross-exit evidence facts
- **WHEN** reviewers inspect `control.steering_snapshot`
- **THEN** they SHALL find `element_evidence.cross_exit` fields for presence, confidence, metric bounds, support counts, and reason
- **AND** they SHALL find `element_evidence.cross_exit.candidate` fields for `built`, `takeover_enabled`, `included_in_arbitration`, and `reason`
- **AND** they SHALL NOT need archive-only topology, scene, or policy fields to explain the evidence

### Requirement: Steering Media Mirrors Element Evidence In Image Headers
The steering media `image_frame.steering_snapshot` object SHALL mirror the public steering snapshot `element_evidence` group so image evidence and control evidence can be aligned from the same media bundle.

#### Scenario: Image frame header carries cross-exit evidence
- **WHEN** the runtime publishes an accepted steering media `image_frame`
- **THEN** the JSON header SHALL include `steering_snapshot.element_evidence.cross_exit`
- **AND** the mirrored `cross_exit.candidate` object SHALL include `built`, `takeover_enabled`, `included_in_arbitration`, and `reason`
- **AND** the raw grayscale payload framing and read-only media session semantics SHALL remain unchanged

### Requirement: Steering Media Config Snapshot Includes Element Parameters
The steering media `config_snapshot.param_snapshot` object SHALL include `BEV_ELEMENT` so element evidence and candidate inclusion behavior can be interpreted from a captured media bundle.

#### Scenario: Config snapshot carries cross-exit takeover state
- **WHEN** the runtime publishes a steering media `config_snapshot`
- **THEN** the JSON header SHALL include `param_snapshot.BEV_ELEMENT.CROSS_EXIT_TAKEOVER_ENABLED`
- **AND** the field SHALL reflect the startup-loaded runtime parameter value
