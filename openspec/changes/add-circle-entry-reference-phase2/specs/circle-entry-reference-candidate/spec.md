## ADDED Requirements

### Requirement: Circle Entry Facts Identify A Rear-Side Inner Frontier
When effective circle evidence is present, the runtime SHALL derive circle entry typed facts from the current-frame `BEVElementRasterFrame` by extracting a rear-side sampleable black-white frontier belonging to the near-connected white component. The detector MUST NOT identify or remember an inner black object, use raster/FOV clipping boundaries as frontier support, or consume cross evidence, line candidates, hold memory, safety state, IMU, encoder, yaw, actuator state, `PerceptionResult`, or debug overlay pixels.

For `circle_left`, the selected frontier chain SHALL have net direction toward upper-left in BEV metric space. For `circle_right`, the selected frontier chain SHALL have net direction toward upper-right. The minimum net lateral direction SHALL be controlled by `BEV_ELEMENT.CIRCLE_ENTRY_DIRECTION_MIN_LATERAL_M`, default `0.08`.

#### Scenario: Left circle entry facts report upper-left frontier
- **WHEN** effective `circle_left.present` is true
- **AND** the near-connected white component touches a rear-side sampleable black frontier chain
- **AND** the chain has at least `BEV_ELEMENT.CIRCLE_ENTRY_MIN_FRONTIER_POINTS` points
- **AND** the chain's net direction is forward and at least `BEV_ELEMENT.CIRCLE_ENTRY_DIRECTION_MIN_LATERAL_M` toward the left
- **THEN** left circle entry facts SHALL be present
- **AND** they SHALL include finite frontier points, direction delta, inferred road half-width, centerline points, and reason `present`

#### Scenario: Right circle entry facts report upper-right frontier
- **WHEN** effective `circle_right.present` is true
- **AND** the near-connected white component touches a rear-side sampleable black frontier chain
- **AND** the chain has at least `BEV_ELEMENT.CIRCLE_ENTRY_MIN_FRONTIER_POINTS` points
- **AND** the chain's net direction is forward and at least `BEV_ELEMENT.CIRCLE_ENTRY_DIRECTION_MIN_LATERAL_M` toward the right
- **THEN** right circle entry facts SHALL be present
- **AND** they SHALL include finite frontier points, direction delta, inferred road half-width, centerline points, and reason `present`

#### Scenario: Non-sampleable or clipped boundary is not frontier support
- **WHEN** a candidate edge is supported only by unknown cells, invalid cells, projection-unavailable cells, outside-frame cells, or the raster/FOV boundary
- **THEN** circle entry facts SHALL be absent
- **AND** the reason SHALL distinguish unsupported frontier from ordinary circle-evidence absence

#### Scenario: Direction below threshold fails closed
- **WHEN** a candidate frontier chain has enough points
- **AND** its net lateral motion toward the circle side is less than `BEV_ELEMENT.CIRCLE_ENTRY_DIRECTION_MIN_LATERAL_M`
- **THEN** circle entry facts SHALL be absent
- **AND** no circle entry candidate SHALL be built from that chain

### Requirement: Circle Entry Centerline Uses Inferred Road Half-Width
The circle entry path SHALL use the selected frontier as one road edge and infer the road centerline from the current frame. The runtime SHALL compute `road_half_width_m` as the median of the first `BEV_ELEMENT.CIRCLE_MIN_SUPPORT_ROWS` near-connected component row widths divided by two. It SHALL NOT use a fixed road-width parameter for Phase 2.

For `circle_left`, the frontier-derived centerline lateral coordinate SHALL be `frontier_lateral_m + road_half_width_m`. For `circle_right`, the frontier-derived centerline lateral coordinate SHALL be `frontier_lateral_m - road_half_width_m`.

#### Scenario: Left centerline is offset from the left inner frontier toward road center
- **WHEN** left circle entry facts contain a selected frontier chain and a finite inferred road half-width
- **THEN** each frontier-derived centerline point SHALL use the corresponding frontier forward coordinate
- **AND** each lateral coordinate SHALL equal `frontier_lateral_m + road_half_width_m` within implementation tolerance

#### Scenario: Right centerline is offset from the right inner frontier toward road center
- **WHEN** right circle entry facts contain a selected frontier chain and a finite inferred road half-width
- **THEN** each frontier-derived centerline point SHALL use the corresponding frontier forward coordinate
- **AND** each lateral coordinate SHALL equal `frontier_lateral_m - road_half_width_m` within implementation tolerance

#### Scenario: Insufficient near-row width support fails closed
- **WHEN** fewer than `BEV_ELEMENT.CIRCLE_MIN_SUPPORT_ROWS` near-connected component row widths are available
- **THEN** circle entry facts SHALL be absent
- **AND** no circle entry candidate SHALL be built

### Requirement: Circle Entry Candidate Is Continuous From The Leading Sample
The circle entry candidate builder SHALL convert effective circle evidence and typed entry facts into a `BEVReferencePath` only when the path can be represented as a finite, contiguous visual reference from sample index 0. Leading samples before the visible frontier MAY use the near-connected component centerline, and samples on the observed frontier SHALL use the frontier-derived centerline. Small interpolation is allowed only within the same selected circle frontier chain.

The builder SHALL reject joins whose lateral jump exceeds `BEV_ELEMENT.CIRCLE_ENTRY_MAX_JOIN_JUMP_M`, default `0.12`, and interpolation gaps exceeding `BEV_ELEMENT.CIRCLE_ENTRY_MAX_INTERPOLATION_GAP_M`, default `0.12`. It SHALL NOT interpolate across unknown, invalid, unavailable, outside-frame, FOV-boundary, or unrelated-chain gaps.

#### Scenario: Near component centerline joins to frontier-derived centerline
- **WHEN** effective circle evidence is present
- **AND** typed entry facts provide near-component centerline support and frontier-derived centerline support
- **AND** the join jump and interpolation gaps are within configured limits
- **THEN** the builder SHALL produce a `BEVReferencePath` with `ReferenceMode::kIntervalCenter`
- **AND** `sampled_path[0].present` SHALL be true
- **AND** all present samples SHALL be contiguous and finite

#### Scenario: Join jump exceeding the limit rejects candidate build
- **WHEN** the near-component centerline and frontier-derived centerline would require a lateral jump greater than `BEV_ELEMENT.CIRCLE_ENTRY_MAX_JOIN_JUMP_M`
- **THEN** the candidate summary SHALL report `built=false`
- **AND** the summary reason SHALL identify the join jump failure
- **AND** no circle candidate SHALL enter arbitration

#### Scenario: Interpolation gap exceeding the limit rejects candidate build
- **WHEN** the selected circle frontier chain has a gap greater than `BEV_ELEMENT.CIRCLE_ENTRY_MAX_INTERPOLATION_GAP_M`
- **THEN** the candidate summary SHALL report `built=false`
- **AND** the summary reason SHALL identify the interpolation gap failure
- **AND** no circle candidate SHALL enter arbitration

### Requirement: Circle Entry Candidate Takeover Is Explicit And Default-Off
The runtime SHALL add `BEV_ELEMENT.CIRCLE_ENTRY_TAKEOVER_ENABLED`, default `false`. A built circle entry candidate SHALL enter visual-reference arbitration only when this parameter is true. When enabled, the candidate SHALL still pass the existing visual-reference validation, special-candidate confidence threshold, hold, usability, lateral-error, readiness, safety, yaw, and actuator chains.

#### Scenario: Default-off takeover builds but excludes candidate from arbitration
- **WHEN** effective circle evidence and typed entry facts are sufficient to build a circle entry candidate
- **AND** `BEV_ELEMENT.CIRCLE_ENTRY_TAKEOVER_ENABLED=false`
- **THEN** the effective circle record candidate summary SHALL report `built=true`
- **AND** `takeover_enabled=false`
- **AND** `included_in_arbitration=false`
- **AND** `reason=takeover_disabled`
- **AND** `RunVisualElementPipeline` SHALL NOT push a circle candidate into its candidate vector

#### Scenario: Enabled takeover pushes a valid circle candidate
- **WHEN** effective circle evidence and typed entry facts are sufficient to build a circle entry candidate
- **AND** `BEV_ELEMENT.CIRCLE_ENTRY_TAKEOVER_ENABLED=true`
- **THEN** `RunVisualElementPipeline` SHALL push a `VisualReferenceCandidate` with kind `kCircleLeft` or `kCircleRight`
- **AND** the candidate source SHALL be `circle_left` or `circle_right`
- **AND** the candidate confidence SHALL be derived from the effective circle evidence
- **AND** the candidate SHALL remain subject to existing visual-reference orchestration validation and downstream control gates

#### Scenario: Cross-suppressed effective circle cannot build candidate
- **WHEN** `cross_exit.present=true`
- **AND** raw circle facts are present
- **THEN** effective circle records SHALL be absent with reason `suppressed_by_cross_exit`
- **AND** the circle entry builder SHALL NOT build or push a circle candidate from the raw facts
