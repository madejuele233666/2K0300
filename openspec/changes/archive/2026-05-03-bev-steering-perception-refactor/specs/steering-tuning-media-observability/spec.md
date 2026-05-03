## MODIFIED Requirements

### Requirement: Runtime Steering Snapshot Exposes The Steering Chain
The runtime SHALL expose a project-owned steering tuning snapshot that explains one control cycle's BEV-side geometry/control-error result, current scene/reference owner, camera outer-loop contribution, gyro inner-loop contribution, and final turn application. At minimum, the accepted snapshot SHALL include:

- `near_lateral_error`
- `far_heading_error`
- `preview_curvature`
- `visible_range_m`
- `track_confidence`
- `active_module`
- `scene_phase`
- `reference_mode`
- `resolved_fuzzy_p`
- `camera_p_term`
- `camera_d_term`
- `w_target`
- `gyro_z`
- `gyro_error`
- `gyro_p_term`
- `gyro_d_term`
- `raw_turn_output`
- `applied_turn_output`

Compatibility projection fields MAY be exported during migration, but they SHALL NOT be the authoritative explanation surface for the steering chain.

#### Scenario: Steering-chain evidence is visible without compatibility authority
- **WHEN** reviewers inspect project-owned diagnostics, structured export, or harness-visible evidence during a steering tuning run
- **THEN** they SHALL be able to identify the accepted BEV-first steering snapshot fields above from a project-owned non-assistant evidence surface such as `control.steering_snapshot`
- **AND** compatibility-only fields SHALL NOT be required to explain the accepted steering chain

### Requirement: Steering Media Protocol Uses One Length-Prefixed Binary Envelope
The accepted first-release steering media protocol SHALL continue to use one length-prefixed binary envelope carrying a UTF-8 JSON header plus an optional raw payload. The accepted `image_frame` payload for the BEV-first steering stack SHALL carry a raw `320x240` grayscale payload from the accepted runtime camera frame. Its JSON header SHALL expose steering snapshot data consistent with the accepted BEV-first steering-chain contract, including scene/reference ownership and the primary BEV control-error terms.

#### Scenario: First-release host tooling can parse the BEV-first media contract deterministically
- **WHEN** implementers build the board-side steering media publisher and the accepted host recorder
- **THEN** they SHALL be able to interoperate through one documented length-prefixed binary envelope with the accepted `config_snapshot` and `image_frame` families
- **AND** the host SHALL NOT need undocumented image-size assumptions or legacy geometry semantics to recover the payload or steering metadata

#### Scenario: Image payload size matches the accepted runtime frame
- **WHEN** the runtime publishes an accepted `image_frame`
- **THEN** the payload SHALL contain exactly `320 * 240` grayscale bytes
- **AND** the header and envelope lengths SHALL be sufficient for the host to validate that payload deterministically

### Requirement: Accepted Host Evidence Bundle Covers Steering Media And Control Alignment
The accepted steering tuning workflow SHALL preserve a project-owned evidence bundle that can correlate BEV-first steering media, runtime control behavior, and any retained compatibility projection from the same run.

#### Scenario: Steering tuning evidence bundle explains BEV and compatibility surfaces together
- **WHEN** the accepted workflow records a steering tuning session
- **THEN** the preserved evidence SHALL include enough geometry/control alignment metadata to distinguish primary BEV steering fields from compatibility projection fields
- **AND** reviewers SHALL be able to explain a steering event without undocumented manual inference about which surface is authoritative

### Requirement: Accepted Steering Evidence MUST Support BEV Overlay Review
The accepted BEV-first steering evidence surface SHALL include either a deterministic dual-view artifact or the preserved projector metadata and geometry outputs needed to reconstruct a raw-image plus BEV overlay review surface from the same run. The accepted reconstruction surface SHALL preserve, at minimum, the raw frame id, projector calibration id/version/hash, sampled centerline, sampled left/right boundaries when available, selected reference path, primary BEV control-error terms, visible range/confidence/fallback state, and any compatibility projection fields in a distinguishable compatibility group.

#### Scenario: Reviewers can inspect BEV path and raw image together
- **WHEN** reviewers investigate a BEV-first steering event or scene transition
- **THEN** they SHALL be able to inspect a raw-image/BEV overlay surface or reconstruct that surface deterministically from preserved evidence
- **AND** the accepted evidence bundle SHALL make the authoritative BEV path and any compatibility projection distinguishable in that review
- **AND** reviewers SHALL be able to verify that the raw frame, BEV geometry, reference path, and control-error terms came from the same frame/projector identity
