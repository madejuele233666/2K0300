## ADDED Requirements

### Requirement: Boundary Trace Continuity Clips Ordinary Edge Facts
The ordinary sparse BEV reference builder SHALL apply boundary-trace continuity clipping to raw same-side edge points before using those edge facts to create ordinary midpoint or single-boundary center candidates.

The clipping helper SHALL consume only ordered boundary trace points, each carrying sparse row index and BEV `forward_m`/`lateral_m`, plus `max_adjacent_distance_m`. It SHALL output the kept boundary points in original order. It MUST NOT receive interval visibility semantics, candidate kind, single-edge or midpoint semantics, screen-edge facts, CircleV2/cross state, visual-reference arbitration facts, hold-last state, or control/safety state.

#### Scenario: Single outlier point is deleted without truncating the trace
- **WHEN** raw same-side boundary points `A`, `B`, and `C` are ordered from near to far
- **AND** `B` is farther than `1 * BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M` from `A`
- **AND** `C` is no farther than `2 * BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M` from `A`
- **THEN** the clipping helper SHALL keep `A`
- **AND** it SHALL delete `B`
- **AND** it SHALL keep `C`
- **AND** it SHALL compare each candidate point only against the last kept point

#### Scenario: Clipped side naturally degrades candidate interpretation
- **WHEN** a row interval's low edge remains after boundary-trace clipping
- **AND** the same interval's high edge is removed by boundary-trace clipping
- **THEN** ordinary candidate generation SHALL treat that row as a low-edge-only candidate according to existing single-boundary semantics
- **AND** the clipping helper SHALL NOT report or know that the row degraded to single-edge semantics

#### Scenario: Both clipped sides remove the row candidate
- **WHEN** both low and high edge facts for a row are removed by boundary-trace clipping
- **THEN** ordinary candidate generation SHALL produce no current-frame center candidate for that row
- **AND** strict leading reference extraction SHALL apply its existing first-gap stop behavior

### Requirement: Boundary Trace Distance Is Explicitly Parameterized
The system SHALL expose `BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M` as the sole boundary-trace adjacent-distance input used by ordinary boundary continuity clipping. The default SHALL be `0.45`.

The ordinary reference builder MUST NOT derive this distance from `BEV_GEOMETRY.NOMINAL_ROAD_HALF_WIDTH_M`, `BEV_GEOMETRY.LATERAL_STEP_M`, row-scan sampleable span, visual confidence, screen-edge state, or any image-derived tolerance. The helper SHALL compare BEV plane distance directly:

`hypot(delta_forward_m, delta_lateral_m) <= max_adjacent_distance_m * row_gap`.

#### Scenario: Parameter loads from configuration
- **WHEN** runtime parameters are loaded with `BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M`
- **THEN** `port::BEVGeometryParameters.boundary_trace_max_adjacent_distance_m` SHALL reflect that value
- **AND** invalid non-positive or non-finite values SHALL follow the existing runtime parameter parse-failure behavior

#### Scenario: Parameter appears in evidence
- **WHEN** steering media publishes a config snapshot
- **THEN** the snapshot SHALL include `BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M`
- **AND** the value SHALL match the loaded runtime parameter

### Requirement: Discontinuous Boundary Trace Does Not Enter Single-Boundary Offset
The ordinary reference builder SHALL call `BuildSingleBoundaryOffsetReference()` only with boundary trace points that remain associated after boundary-trace continuity clipping.

The single-boundary offset helper SHALL keep its current pure geometry contract. It SHALL NOT perform boundary-trace clipping itself and SHALL NOT learn runtime parameters, screen-edge state, row intervals, or ordinary candidate semantics.

#### Scenario: Discontinuous adjacent row is rejected before offset
- **WHEN** a one-side-visible row has a same-side edge in an adjacent sparse row
- **AND** the adjacent edge is farther than `1 * BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M`
- **AND** no farther kept same-side edge is within the row-gap-scaled distance
- **THEN** ordinary candidate generation SHALL delete that row's single-boundary candidate
- **AND** it SHALL NOT call `BuildSingleBoundaryOffsetReference()` for that row

#### Scenario: Farther associated row may support offset after an outlier
- **WHEN** a one-side-visible row has adjacent same-side edge `B` removed by continuity clipping
- **AND** a farther same-side edge `C` remains within `row_gap * BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M` from the last kept point
- **THEN** ordinary candidate generation MAY use `C` as the associated boundary support for that row
- **AND** the produced center candidate SHALL still pass existing strict-leading, finite-sample, and connectivity checks before it can be selected

### Requirement: Boundary Continuity Does Not Change Screen Edge Or Path Connectivity Ownership
Boundary-trace continuity clipping SHALL NOT replace screen-edge visibility, current-frame path connectivity, visual-reference arbitration, reference hold-last, reference-control readiness, CircleV2, cross, or control behavior.

#### Scenario: Screen edge semantics remain separate
- **WHEN** a row interval touches a known or configured unknown sampleable screen edge
- **THEN** existing edge-visibility logic SHALL decide whether that endpoint is visible before ordinary candidate interpretation
- **AND** boundary-trace continuity clipping SHALL NOT treat screen edges as real line evidence

#### Scenario: Paths may still leave the sampleable span
- **WHEN** a continuous visible single boundary supports ordinary single-boundary offset
- **THEN** the generated reference point MAY lie outside the current row's sampleable span
- **AND** boundary-trace continuity clipping SHALL NOT clamp that path point to the screen edge or search range

## MODIFIED Requirements

### Requirement: Ordinary Reference Interprets Interval Boundary Visibility
For one-side-lost ordinary rows, the ordinary sparse BEV reference builder SHALL treat the visible endpoint as boundary evidence after screen-edge visibility and boundary-trace continuity clipping. A generated single-boundary center sample SHALL NOT be rejected merely because it lies outside the current row's white interval, sampleable span, screen edge, or BEV search range.

#### Scenario: Low edge visible and high edge lost uses positive normal offset
- **WHEN** a leading row interval has a visible low edge after screen-edge visibility and boundary-trace continuity clipping
- **AND** the high edge is unavailable because it touches the sampleable boundary or was removed by boundary-trace continuity clipping
- **AND** an associated kept low-edge support point exists for the single-boundary helper
- **THEN** the ordinary reference builder SHALL treat the low edge trace as the visible boundary
- **AND** it SHALL request a signed normal offset of `+BEV_GEOMETRY.nominal_road_half_width_m`
- **AND** it SHALL accept the generated finite center sample even when that sample lies outside the row's white interval or sampleable span

#### Scenario: High edge visible and low edge lost uses negative normal offset
- **WHEN** a leading row interval has a visible high edge after screen-edge visibility and boundary-trace continuity clipping
- **AND** the low edge is unavailable because it touches the sampleable boundary or was removed by boundary-trace continuity clipping
- **AND** an associated kept high-edge support point exists for the single-boundary helper
- **THEN** the ordinary reference builder SHALL treat the high edge trace as the visible boundary
- **AND** it SHALL request a signed normal offset of `-BEV_GEOMETRY.nominal_road_half_width_m`
- **AND** it SHALL accept the generated finite center sample even when that sample lies outside the row's white interval or sampleable span
