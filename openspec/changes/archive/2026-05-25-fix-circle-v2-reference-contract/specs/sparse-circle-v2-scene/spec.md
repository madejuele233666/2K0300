## MODIFIED Requirements

### Requirement: Circle V2 Reference Plans Replace Rear-Black Entry Paths
Runtime Circle V2 reference construction SHALL emit a `CircleV2ReferencePlan` only when the role-specific edge geometry forms a finite leading-contiguous path segment. Single-sample, gapped, or otherwise structurally incomplete edge observations SHALL be treated as unavailable geometry rather than adapted into circle visual-reference candidates.

#### Scenario: InnerTrace only emits a structurally valid leading path
- **WHEN** current-frame reference context is `InnerTrace`
- **AND** the locked-direction inner edge search finds fewer than the required leading contiguous finite samples
- **THEN** `CircleV2GeometryObserver` SHALL mark geometry unavailable
- **AND** `CircleV2StepResult.reference_plan` SHALL be empty
- **AND** no Circle V2 visual-reference candidate SHALL be appended for that frame
- **AND** `CircleV2Reducer` SHALL keep the authoritative `next_memory` for that frame without resetting or rolling back phase

#### Scenario: InnerTrace rejects gapped leading geometry
- **WHEN** current-frame reference context is `InnerTrace`
- **AND** edge samples are present after an absent sample in the leading path segment
- **THEN** the geometry SHALL be treated as unavailable
- **AND** the composer SHALL NOT emit a `CircleV2ReferencePlan`

#### Scenario: InnerTrace emits only contiguous offset paths
- **WHEN** current-frame reference context is `InnerTrace`
- **AND** the locked-direction inner edge search produces a finite leading contiguous edge segment with enough samples
- **THEN** `CircleV2ReferenceComposer` SHALL offset that edge by `ordinary_road.half_width`
- **AND** it SHALL emit a `CircleV2ReferencePlan` whose leading samples remain contiguous

### Requirement: Circle V2 Candidate Adaptation Is Fixed And Confidence-Free
The Circle V2 reference adapter SHALL remain a packaging boundary. It SHALL convert present scene-owned `CircleV2ReferencePlan` values into the existing visual-reference candidate type, and it SHALL NOT repair, densify, score, or validate unavailable geometry.

#### Scenario: Adapter does not repair missing or malformed plans
- **WHEN** `CircleV2StepResult.reference_plan` is empty because role-specific geometry is unavailable
- **THEN** the Circle V2 reference adapter SHALL return no candidate
- **AND** it SHALL NOT synthesize, pad, densify, score, or otherwise repair a candidate path

#### Scenario: Adapter wraps only scene-owned valid plans
- **WHEN** `CircleV2ReferencePlan.role` is `InnerTrace` or `ExitTrace`
- **AND** the plan is present
- **THEN** the adapter SHALL map direction and role to the existing candidate kind and V2 source string
- **AND** it SHALL not consume a scene confidence score to decide whether the candidate is valid
