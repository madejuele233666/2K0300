## MODIFIED Requirements

### Requirement: Steering Media Protocol Uses One Length-Prefixed Binary Envelope
The accepted first-release steering media protocol SHALL use one length-prefixed binary envelope carrying a UTF-8 JSON header plus an optional raw payload. The accepted first-release frame families SHALL remain `config_snapshot` and `image_frame`. The accepted `config_snapshot.param_snapshot` object SHALL include the current runtime parameter groups needed to interpret steering media evidence, including both `BEV_ELEMENT` and `BEV_ELEMENT_RASTER`.

#### Scenario: First-release host tooling can parse the media contract deterministically
- **WHEN** implementers build the board-side steering media publisher and the accepted host recorder
- **THEN** they SHALL be able to interoperate through one documented length-prefixed binary envelope with `config_snapshot` and `image_frame`
- **AND** the host SHALL NOT need a second undocumented framing model to recover image payload size or steering metadata

#### Scenario: Config snapshot carries raster context
- **WHEN** the runtime publishes a steering media `config_snapshot`
- **THEN** the JSON header SHALL include `param_snapshot.BEV_ELEMENT`
- **AND** it SHALL include `param_snapshot.BEV_ELEMENT_RASTER.ENABLED` and `param_snapshot.BEV_ELEMENT_RASTER.WIDTH`
- **AND** those fields SHALL reflect startup-loaded runtime parameter values

### Requirement: Public Steering Snapshots Expose Element Evidence Facts
The project-owned public steering snapshot SHALL include a read-only `element_evidence` group serialized through the shared element-evidence serializer. The typed `cross_exit` group SHALL remain stable and generic element records MAY be appended without requiring old consumers to understand them.

#### Scenario: Steering snapshot includes cross-exit evidence facts
- **WHEN** reviewers inspect `control.steering_snapshot`
- **THEN** they SHALL find `element_evidence.cross_exit` fields for presence, confidence, metric bounds, support counts, and reason
- **AND** they SHALL find `element_evidence.cross_exit.candidate` fields for `built`, `takeover_enabled`, `included_in_arbitration`, and `reason`
- **AND** they SHALL NOT need archive-only topology, scene, or policy fields to explain the evidence

#### Scenario: Steering snapshot can append generic element records
- **WHEN** generic element evidence records are present
- **THEN** the public steering snapshot SHALL serialize those records at `element_evidence.records` after the stable `cross_exit` group
- **AND** each record SHALL follow the stable generic record schema defined by `bev-visual-element-evidence`
- **AND** old consumers that only read `cross_exit` SHALL remain compatible

### Requirement: Steering Media Mirrors Element Evidence In Image Headers
The steering media `image_frame.steering_snapshot` object SHALL mirror the public steering snapshot `element_evidence` group through the shared element-evidence serializer so image evidence and control evidence can be aligned from the same media bundle.

#### Scenario: Image frame header carries cross-exit evidence
- **WHEN** the runtime publishes an accepted steering media `image_frame`
- **THEN** the JSON header SHALL include `steering_snapshot.element_evidence.cross_exit`
- **AND** the mirrored `cross_exit.candidate` object SHALL include `built`, `takeover_enabled`, `included_in_arbitration`, and `reason`
- **AND** the raw grayscale payload framing and read-only media session semantics SHALL remain unchanged
