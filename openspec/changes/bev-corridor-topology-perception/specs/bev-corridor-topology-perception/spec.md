## ADDED Requirements

### Requirement: Runtime Geometry MUST Use Classified Sparse BEV Samples
The steering perception runtime MUST use a metric sparse BEV sampling layer as the first authoritative geometry source after thresholding and projector configuration. Each sample MUST carry a `BEVPoint`, source `ImagePoint`, raw intensity, confidence, image-projection validity, and one of these classes: invalid outside image, unknown low confidence, background, or drivable.

#### Scenario: Samples preserve image validity and confidence
- **WHEN** the sparse sampler evaluates a configured forward/lateral lattice point
- **THEN** it MUST classify out-of-image projection as invalid outside image
- **AND** it MUST classify insufficient-confidence projected samples as unknown low confidence
- **AND** it MUST NOT collapse invalid, unknown, background, and drivable samples into one binary road/not-road value

### Requirement: BEV Coordinates MUST Keep The Existing Projector Contract
The topology pipeline MUST preserve the existing `BEVPoint.forward_m` and `BEVPoint.lateral_m` coordinate semantics. The projector MUST remain limited to image/vehicle coordinate mapping and MUST NOT depend on scene, FSM, reference policy, PID, or motor state.

#### Scenario: Topology sampling uses the existing coordinate signs
- **WHEN** reviewers inspect sparse samples, corridor intervals, reference paths, and control-error terms for one frame
- **THEN** all BEV facts MUST use the same `forward_m` and `lateral_m` sign convention as the current projector contract
- **AND** the projector code MUST NOT need scene/FSM/control inputs to map between image and BEV coordinates

### Requirement: Corridor Intervals MUST Distinguish Invalid Edges From Openings
The interval extractor MUST convert sparse samples into per-forward-layer `CorridorInterval` values that include lateral bounds, center, width, edge-valid flags, opening scores, valid-sample ratio, and confidence. Invalid outside-image samples MUST NOT be counted as road openings.

#### Scenario: Image border does not create a false opening
- **WHEN** a drivable interval reaches the camera field-of-view boundary and adjacent BEV lattice samples project outside the image
- **THEN** the interval edge adjacent to invalid samples MUST be marked not edge-valid or penalized as invalid
- **AND** the interval extractor MUST NOT increase left or right opening score from those invalid samples

#### Scenario: Unknown samples lower confidence without proving background
- **WHEN** low-confidence samples occur next to or inside a corridor interval
- **THEN** the interval valid-sample ratio or confidence MUST decrease
- **AND** the extractor MUST NOT treat unknown low-confidence samples as real background evidence

### Requirement: Corridor Graph MUST Select The Ordinary Chain
The ordinary path MUST be selected from corridor intervals through a local graph or DAG/DP that scores layer-to-layer overlap, center jump, width change, curvature, confidence, and prior carry. The runtime MUST NOT select the ordinary path by scanning an image row for a white run.

#### Scenario: Ordinary track is graph-derived
- **WHEN** one perception cycle produces a `BEVTrackEstimate`
- **THEN** its centerline and width summaries MUST be derived from the accepted corridor graph ordinary chain
- **AND** the `BEVTrackEstimate` source MUST identify corridor topology, such as `bev_corridor_topology`
- **AND** source review MUST NOT find active ordinary authority in image-row run extraction

### Requirement: Graph Selection MUST Resist Branches And Short Edge Loss
The corridor graph MUST preserve ordinary-chain continuity through short single-side edge loss and branch ambiguity without sticking to image borders or jumping to a branch solely because that branch is wider.

#### Scenario: Temporary single-side loss does not jump the track
- **WHEN** one side of the ordinary corridor is missing or unknown for a short forward span
- **THEN** graph selection MAY use prior carry or valid opposite-side evidence with reduced confidence
- **AND** it MUST NOT create a high-confidence branch jump or screen-edge-following track

### Requirement: Road Hypotheses MUST Be Generated From Topology
The perception stack MUST generate `RoadHypotheses` from corridor intervals, graph edges, the ordinary chain, and prior topology memory. Hypotheses MUST include ordinary path and, when supported by evidence, left/right arc, forward exit, left/right branch, and zebra-hold candidates.

#### Scenario: Cross and circle candidates share topology inputs
- **WHEN** a frame contains branch or opening geometry
- **THEN** cross, circle, and ordinary hypotheses MUST be generated from the same interval/graph topology facts
- **AND** they MUST NOT be generated from unrelated image-space or bottom-tracker authority

### Requirement: TopologyEvidence MUST Score Ordinary, Cross, Circle, Zebra, Bend Veto, And Lost
The topology evidence layer MUST produce scores for ordinary, cross, left circle, right circle, zebra, lost, bend curvature/veto, bilateral opening sync, forward reacquire, side openings, and invalid-edge penalty. These scores MUST be facts consumed by FSM/reference/constraint layers, not direct motor commands.

#### Scenario: Evidence is complete for formal scene classes
- **WHEN** topology evidence is emitted for a frame
- **THEN** it MUST include ordinary, cross, left-circle, right-circle, zebra, and lost scores
- **AND** it MUST include enough supporting scores to explain cross bilateral opening, circle side opening, bend veto, forward reacquire, and invalid-edge penalties

### Requirement: Legacy Geometry Heuristics MUST Be Debug-Only Or Removed
Legacy row-scan, bottom tracker, `LaneMetrics`, `highest_line`, `farthest_line`, `steering_reference_col`, `track_seed_col`, `track_source`, `width_expand_ratio`, `open_score`, and `bottom_transition_density` style heuristics MUST NOT participate in formal FSM, reference policy, `BEVTrackEstimate`, or control authority.

#### Scenario: Source search finds no formal legacy authority
- **WHEN** reviewers search active runtime sources for removed authority fields and modules
- **THEN** any surviving match MUST be compatibility/debug/offline/test-only
- **AND** no formal runtime decision path MAY read those fields as geometry, scene, reference, or control authority

### Requirement: Dense BEV Images MAY Only Be Debug Artifacts
The runtime authority MUST remain sparse samples, intervals, graph, hypotheses, and evidence. Dense BEV images MAY be generated for host or debug artifacts only when they are traceable to the same sparse topology source.

#### Scenario: Overlay renders dense BEV without changing authority
- **WHEN** host tooling renders a dense BEV overlay for review
- **THEN** the overlay MUST be explainable from sparse samples, intervals, graph chains, hypotheses, and reference paths
- **AND** the dense image MUST NOT be a separate input to FSM/reference/control authority

### Requirement: Topology Parameters MUST Be Runtime-Owned
Topology sampler, corridor graph, topology evidence, and topology reference policy tuning MUST be loaded through project-owned runtime parameter surfaces and surfaced in `config_snapshot` or equivalent accepted evidence.

#### Scenario: Board evidence explains topology thresholds
- **WHEN** reviewers inspect preserved board or host evidence for a topology run
- **THEN** they MUST be able to identify the loaded sampler lattice, graph limits, evidence thresholds, and reference-policy memory limits
- **AND** implementers MUST NOT need undocumented source constants to tune topology behavior
