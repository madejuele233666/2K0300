# ordinary-bev-reference Specification

## Purpose
Define how sparse BEV ordinary reference generation interprets row intervals into current-frame road-center samples, including one-side-lost boundary handling and reusable single-boundary geometry.

## Requirements
### Requirement: Ordinary Reference Interprets Interval Boundary Visibility
The ordinary sparse BEV reference builder SHALL interpret each row interval endpoint as a low/high lateral edge before producing a current-frame center sample. `low_edge` SHALL mean the endpoint with smaller `lateral_m`; `high_edge` SHALL mean the endpoint with larger `lateral_m`. Existing row-scan fields `interval.left_m` and `interval.right_m` SHALL be treated as low/high lateral coordinates, not physical left/right road ownership.

For each row interval, `low_edge` SHALL be considered visible when it does not touch `row.sampleable_left_m`, and `high_edge` SHALL be considered visible when it does not touch `row.sampleable_right_m`. The touch tolerance SHALL be derived from `BEV_GEOMETRY.lateral_step_m` as a sampling-geometry tolerance, not from a new business parameter.

#### Scenario: Both visible endpoints produce midpoint center
- **WHEN** a leading row interval has visible low and high edges
- **THEN** the ordinary reference builder SHALL produce that row's center sample at `0.5 * (low_edge + high_edge)`
- **AND** it SHALL keep the sample source compatible with the existing ordinary reference debug mode
- **AND** it SHALL NOT call the single-boundary offset helper for that row

#### Scenario: Low edge visible and high edge lost uses positive normal offset
- **WHEN** a leading row interval has a visible low edge
- **AND** the high edge touches `row.sampleable_right_m`
- **THEN** the ordinary reference builder SHALL treat the low edge trace as the visible boundary
- **AND** it SHALL request a signed normal offset of `+BEV_GEOMETRY.nominal_road_half_width_m`
- **AND** the produced center sample SHALL be used only if it remains inside that row's white interval

#### Scenario: High edge visible and low edge lost uses negative normal offset
- **WHEN** a leading row interval has a visible high edge
- **AND** the low edge touches `row.sampleable_left_m`
- **THEN** the ordinary reference builder SHALL treat the high edge trace as the visible boundary
- **AND** it SHALL request a signed normal offset of `-BEV_GEOMETRY.nominal_road_half_width_m`
- **AND** the produced center sample SHALL be used only if it remains inside that row's white interval

#### Scenario: No visible endpoint produces no current visual sample
- **WHEN** a leading row has no white interval
- **OR** the selected interval's low and high edges both touch the sampleable range
- **THEN** the ordinary reference builder SHALL produce no current visual center sample for that row
- **AND** strict leading reference extraction SHALL stop at that row

### Requirement: Single-Boundary Offset Helper Is A Pure Geometry Helper
The system SHALL provide a reusable single-boundary normal-offset helper for converting one current-frame BEV boundary trace into a leading BEV reference/path trace. The helper SHALL consume only:

- a same-frame, same-edge BEV boundary trace;
- target `forward_m` samples;
- a signed normal offset in meters.

The helper MUST NOT read CircleV2 state, cross evidence, visual element evidence, `RuntimeParameters` as a whole, reference hold memory, candidate arbitration, or control/safety state.

#### Scenario: Helper offsets by local boundary direction
- **WHEN** the helper has a finite single-valued boundary trace `x(y)` over target rows
- **THEN** it SHALL estimate the local direction `s(y) = dx/dy`
- **AND** it SHALL produce `target_x(y) = edge_x(y) + signed_normal_offset_m * sqrt(1 + s(y)^2)`
- **AND** `signed_normal_offset_m = 0` SHALL produce a path on the observed boundary

#### Scenario: Helper stops instead of inventing missing geometry
- **WHEN** the boundary trace has fewer than two finite points, no forward variation, cannot be represented as single-valued `x(y)`, lacks interpolation support for a target row, or produces non-finite direction/output
- **THEN** the helper SHALL stop the leading output at that point
- **AND** it SHALL NOT skip rows, patch holes, use fallback slope zero, or introduce a hidden confidence score

#### Scenario: Helper has neutral ownership
- **WHEN** ordinary reference, CircleV2 InnerTrace, CircleV2 ExitTrace, or any future scene needs a path from a single visible boundary
- **THEN** that caller SHALL map its own facts to `boundary_trace`, `target_forward_samples`, and `signed_normal_offset_m`
- **AND** the helper SHALL remain unaware of the caller's scene, FSM phase, element type, or arbitration role

### Requirement: Ordinary Reference Selects Center Candidates After Interpretation
When a row has multiple white intervals, the ordinary reference builder SHALL first interpret endpoint visibility and form center candidates before selecting the strict leading reference trace. It SHALL NOT choose an interval by raw interval midpoint and then repair that midpoint afterward.

#### Scenario: Multi-interval selection includes midpoint and helper candidates
- **WHEN** a leading row contains multiple white intervals
- **THEN** the ordinary reference builder SHALL retain valid midpoint candidates from both-edge intervals
- **AND** it SHALL retain valid single-boundary helper candidates from one-side-lost intervals
- **AND** it SHALL select among center candidates by near-to-far leading continuity and same-frame adjacent-sample geometry continuity

#### Scenario: Strict leading remains unchanged
- **WHEN** an otherwise valid far row exists after a row without an available current visual center
- **THEN** ordinary reference extraction SHALL stop at the first unavailable row
- **AND** it SHALL NOT restart from the far row
- **AND** it SHALL NOT fill the missing row from history or fixed lateral fallback

### Requirement: Hold Ownership Remains In Reference Continuity
Ordinary reference building SHALL produce only the current-frame visual reference. Reference hold-last behavior SHALL remain owned by the existing reference continuity layer.

#### Scenario: Current visual reference absence may enter existing hold
- **WHEN** ordinary reference extraction produces no sufficient current-frame leading reference because both boundaries are unavailable or helper output is insufficient
- **THEN** the ordinary builder SHALL return an insufficient current visual reference
- **AND** the existing reference continuity layer MAY produce a hold-last candidate according to `BEV_CLASSIFICATION.hold_last_max_cycles`
- **AND** the ordinary builder SHALL NOT store historical path state, predict curvature, or synthesize a hold candidate itself

### Requirement: Ordinary Candidate Connectivity Starts At Vehicle Origin
When ordinary reference extraction skips near rows and publishes the first later continuous segment, the visual-reference connectivity gate SHALL verify the current-frame segment from vehicle origin `(forward_m=0, lateral_m=0)` to the first published reference sample before that sample can contribute to the connected prefix.

The vehicle origin SHALL be a connectivity-only anchor. It SHALL NOT be inserted into `BEVReferencePath.sampled_path`, SHALL NOT change reference sample count, and SHALL NOT alter tracking geometry input except by clipping disconnected candidate prefixes through the existing connectivity gate.

#### Scenario: Later segment must connect to vehicle origin
- **WHEN** ordinary reference generation produces a later first sample after near rows are unavailable
- **AND** the current frame contains black support on the segment from vehicle origin to that first sample
- **THEN** the connectivity gate SHALL clip the candidate before that first sample
- **AND** downstream usability SHALL judge the clipped candidate using the existing minimum-sample rules

### Requirement: Ordinary Lost-Boundary Repair Does Not Change Element Recognition
This change SHALL NOT modify CircleV2 FSM transition events, cross evidence, visual element detection ownership, visual reference arbitration priority, or control safety gates.

#### Scenario: Basis facts remain internal
- **WHEN** ordinary reference points are generated from midpoint or single-boundary offset candidates
- **THEN** any basis classification such as `both_edges`, `low_edge_normal_offset`, `high_edge_normal_offset`, or `unavailable` SHALL remain internal or debug-only
- **AND** it SHALL NOT become a visual element fact, candidate kind, arbitration priority, or FSM transition input

#### Scenario: Existing reference mode can remain ordinary-compatible
- **WHEN** V4 ordinary reference generation uses single-boundary offset for some samples
- **THEN** the output MAY continue using `ReferenceMode::kIntervalCenter` and source `simple_interval_center` to mean ordinary current-frame visual reference
- **AND** downstream orchestration SHALL NOT infer whether a point came from midpoint or boundary offset from the reference mode

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
