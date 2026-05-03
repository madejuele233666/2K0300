## ADDED Requirements

### Requirement: BEV MUST Be The Sole Steering Geometry Truth
The steering perception stack MUST treat BEV geometry as the only authoritative geometry model for ordinary lane tracking and special-scene evidence. Image-space row-band statistics, bottom-tracker outputs, and compatibility projection fields MUST NOT remain authoritative inputs to scene classification, reference policy selection, or control-error generation.

#### Scenario: Ordinary control and special-scene evidence share one geometry truth
- **WHEN** one perception cycle produces both ordinary-lane geometry and special-scene evidence
- **THEN** both outputs MUST be derived from the same BEV geometry model
- **AND** the runtime MUST NOT classify special scenes from one geometry source while controlling ordinary steering from another

### Requirement: BEVTrackEstimate MUST Use Fixed Forward-Distance Samples
The steering perception stack MUST represent the ordinary lane path as a fixed forward-distance sampled `BEVTrackEstimate` plus geometry summaries such as visible range, lane-width profile, track confidence, near lateral offset, far heading error, and preview curvature.

#### Scenario: Ordinary path output is stable and reviewable
- **WHEN** reviewers inspect the accepted steering geometry model for a frame or fixture
- **THEN** they MUST be able to identify a fixed-distance sampled center path and the accepted geometry summaries
- **AND** the main ordinary-path contract MUST NOT depend on ad hoc image-row anchors as its primary form

### Requirement: BEV Coordinate And Calibration Contract MUST Be Reconstructable
The accepted BEV geometry contract MUST define one project-owned vehicle-frame coordinate convention and one reconstructable projector calibration contract. The accepted coordinate convention SHALL use the left/right wheel axle midpoint on the vehicle centerline as origin, positive `forward_m` ahead of the vehicle, and positive `lateral_m` to the vehicle right. Projector source image points, vehicle-frame target points, sampling distances, lateral search range, optional debug-grid sizing, and projector version/hash MUST be startup-loaded or reconstructable from accepted calibration artifacts.

#### Scenario: Reviewers reconstruct the board-side BEV mapping
- **WHEN** reviewers inspect the accepted calibration artifact, runtime parameter surface, and preserved evidence for a BEV-first run
- **THEN** they MUST be able to reconstruct the board-side projector mapping and sampled path coordinate convention without undocumented constants
- **AND** sampled boundaries, sampled centerline, reference path, control-error terms, and overlay evidence MUST share the same projector identity or version/hash

### Requirement: Board Runtime Geometry MUST Support Sparse BEV Sampling
The accepted board-side steering geometry path MUST support a sparse BEV sampling strategy that produces fixed forward-distance boundary/path samples and geometry summaries without requiring a dense BEV bitmap as the authoritative runtime intermediate. Dense BEV images MAY be produced for host/debug evidence, but they MUST NOT become a separate geometry truth.

#### Scenario: Board-side geometry is generated from the accepted sparse lattice
- **WHEN** the runtime processes a frame for steering authority
- **THEN** it MUST be able to produce the accepted sampled path, boundary, width, visible-range, and confidence outputs through the calibrated BEV sampling contract
- **AND** any dense debug or overlay artifact MUST be traceable back to the same projector and sampled geometry outputs

### Requirement: IPM Calibration MUST Remain Separate From Scene And Control Tuning
The accepted BEV architecture MUST keep camera/IPM calibration parameters separate from scene classifier thresholds and control-model tuning. Scene or control tuning MUST NOT silently redefine the camera/IPM mapping contract.

#### Scenario: A scene threshold update does not change camera geometry
- **WHEN** implementers adjust circle, cross, bend, or control thresholds
- **THEN** the accepted IPM source points, projection geometry, and BEV output sizing contract MUST remain unchanged unless a camera/IPM calibration change is explicitly made

### Requirement: BEV Calibration And Tuning Ownership MUST Be Explicit
The accepted steering stack MUST load BEV projector calibration, BEV geometry tuning, FSM thresholds, and BEV control-model tuning through explicit project-owned parameter ownership and persistence surfaces. The accepted workflow MUST also define a reviewable calibration artifact or table that explains the board-side values used for the BEV projector.

#### Scenario: Reviewers can trace BEV calibration from artifact to runtime load
- **WHEN** reviewers inspect the accepted runtime parameter surface and preserved steering evidence for a BEV-first run
- **THEN** they MUST be able to identify where projector calibration lives, where BEV/FSM/control tuning lives, and which accepted calibration artifact or table explains the loaded values
- **AND** implementers MUST NOT need undocumented hardcoded constants to reconstruct the accepted BEV mapping

### Requirement: Compatibility Projection MUST Be Read-Only
The accepted steering stack MAY continue to publish compatibility projection fields such as image-space reference columns or line-height markers during migration, but those fields MUST be derived from the BEV model and MUST NOT regain ownership over ordinary steering, special-scene decisions, or control-error computation.

#### Scenario: Compatibility fields remain observational only
- **WHEN** a runtime snapshot or host tool includes compatibility projection fields
- **THEN** those fields MUST be explainable as projections from the authoritative BEV model
- **AND** source review MUST be able to show that core geometry, scene, and control layers do not read them back as authority

### Requirement: Geometry Output MUST Expose Confidence And Fallback State
The accepted BEV geometry contract MUST surface visible-range, confidence, and fallback/source state explicitly so runtime and verification can distinguish true geometry loss from normal scene transitions.

#### Scenario: Partial boundary loss is visible without collapsing the contract
- **WHEN** one BEV boundary becomes degraded or partially unavailable
- **THEN** the geometry contract MUST expose reduced visible range, confidence, or fallback state explicitly
- **AND** reviewers MUST NOT need to infer this loss only from downstream control behavior
- **AND** the runtime MUST NOT silently recover by treating legacy image-space geometry as the authoritative fallback truth
