## MODIFIED Requirements

### Requirement: Runtime Steering Snapshot Exposes The Steering Chain
The runtime SHALL expose a project-owned steering tuning snapshot that can explain one control cycle from topology perception through reference policy, curvature control, gyro contribution, and final turn application. The accepted snapshot SHALL include topology-authority fields in addition to the control-chain fields needed to explain actuator output.

At minimum, the accepted topology steering snapshot SHALL include:

- `topology_source`
- `ordinary_score`
- `cross_score`
- `left_circle_score`
- `right_circle_score`
- `zebra_score`
- `lost_score`
- `evidence_accumulators`
- `scene_phase`
- `reference_mode`
- `reference_confidence`
- `ordinary_curvature`
- `lookahead_distance_m`
- `curvature_command`
- `visible_range_m`
- `track_confidence`

Legacy authority fields such as `highest_line`, `farthest_line`, `steering_reference_col`, `track_seed_col`, `track_source`, `LaneMetrics`, bottom-tracker outputs, image-row run authority, width-expansion authority, open-score authority, and bottom-transition authority SHALL NOT appear as formal runtime authority fields. If retained temporarily, they SHALL be explicitly compatibility/debug/offline-only and SHALL NOT be required to explain the accepted control cycle.

#### Scenario: Topology-chain evidence is visible without assistant rendering
- **WHEN** reviewers inspect project-owned diagnostics, structured export, or harness-visible evidence during a steering topology run
- **THEN** they SHALL be able to identify the accepted topology source, topology scores, FSM phase, reference mode/confidence, lookahead, curvature command, visible range, and track confidence from a project-owned non-assistant evidence surface such as `control.steering_snapshot`
- **AND** assistant connectivity or vendor-only image rendering SHALL NOT be required to explain the steering chain
- **AND** old image-row or bottom-tracker fields SHALL NOT be needed to explain formal scene, reference, or control authority

### Requirement: Steering Media Protocol Uses One Length-Prefixed Binary Envelope
The accepted steering media protocol SHALL continue to use one length-prefixed binary envelope carrying a UTF-8 JSON header plus an optional raw payload. The accepted topology-capable frame families SHALL include `config_snapshot` and `image_frame`, and MAY include additional topology/debug frame families only if they use the same envelope and remain read-only.

The accepted topology-capable `config_snapshot.param_snapshot` SHALL include the startup-loaded topology parameter groups needed to reconstruct sampler, graph, evidence, reference-policy, projector, and control-model behavior. The accepted topology-capable `image_frame.steering_snapshot` SHALL include topology-authority fields from the runtime steering snapshot.

The accepted image payload dimensions SHALL be declared in each `image_frame` header and MUST match the raw payload length. Host tooling SHALL NOT assume old fixed geometry when the runtime declares the current camera frame dimensions.

#### Scenario: Host tooling parses topology media deterministically
- **WHEN** implementers build the board-side steering media publisher and the accepted host recorder
- **THEN** they SHALL interoperate through the documented length-prefixed binary envelope
- **AND** the host SHALL be able to recover image payload dimensions, topology snapshot fields, and config snapshot topology parameters without undocumented framing or old fixed-size assumptions

#### Scenario: Media protocol remains read-only
- **WHEN** topology overlays or topology media fields are enabled
- **THEN** the media session SHALL remain lower-priority read-only push traffic
- **AND** it SHALL NOT accept commands, ACKs, tuning updates, or state mutations

### Requirement: Accepted Host Evidence Bundle Covers Steering Media And Control Alignment
The accepted steering tuning workflow SHALL preserve a project-owned evidence bundle that can correlate raw image frames, BEV topology evidence, reference policy, control curvature, gyro contribution, and final turn application from the same run.

#### Scenario: Topology evidence bundle is complete enough for review
- **WHEN** the accepted workflow records a steering topology session
- **THEN** the preserved evidence SHALL include at minimum board log, host control CSV or structured event log, steering media records, topology scores, reference mode/confidence, curvature command, and a time-aligned summary or equivalent alignment metadata
- **AND** reviewers SHALL be able to explain a steering event without depending on undocumented manual alignment steps or old authority fields

## ADDED Requirements

### Requirement: Steering Media Overlay MUST Reconstruct BEV Topology
The accepted steering media and overlay tooling MUST be able to render or verify the raw image plus topology-derived BEV evidence, including sparse samples, sample classes, corridor intervals, ordinary chain, left/right arc hypotheses, forward-exit hypothesis, final reference path, and pure-pursuit target.

#### Scenario: Overlay shows raw and BEV topology evidence
- **WHEN** reviewers run the accepted overlay probe or media capture tooling on preserved topology evidence
- **THEN** the output MUST distinguish invalid outside-image, unknown low-confidence, background, and drivable samples
- **AND** it MUST display enough interval, graph, hypothesis, reference, and pure-pursuit data to explain the accepted control cycle

### Requirement: Authority Baseline Validation MUST Reject Old Formal Authority Fields
The accepted authority baseline validation MUST cover straight, bend, cross, circle, and zebra baselines and MUST reject formal output that still depends on old compatibility authority fields.

#### Scenario: Baseline output contains topology authority only
- **WHEN** `authority_baseline_validate` runs on accepted straight, bend, cross, circle, and zebra fixtures
- **THEN** it MUST output topology scores and reference mode
- **AND** it MUST NOT require `highest_line`, `farthest_line`, `steering_reference_col`, `track_seed_col`, `track_source`, `LaneMetrics`, or bottom-tracker fields as formal authority

### Requirement: Board Evidence MUST Start Motor-Disabled
Before powered low-speed validation, the accepted board workflow MUST capture motor-disabled evidence proving topology field presence, projector/sign consistency, sparse sample/interval rendering, scene phase behavior, reference mode behavior, and curvature command sign.

#### Scenario: Powered run is gated by topology evidence
- **WHEN** implementers prepare for a powered low-speed topology run
- **THEN** they MUST first preserve motor-disabled evidence showing topology fields and sign conventions are coherent
- **AND** only after that evidence is accepted MAY low-speed powered evidence be used to tune behavior
