## MODIFIED Requirements

### Requirement: Circle V2 Reference Plans Replace Rear-Black Entry Paths
Runtime Circle V2 single-boundary reference construction SHALL reuse the neutral single-boundary normal-offset helper used by ordinary BEV reference generation. CircleV2 SHALL remain responsible only for choosing the role-specific boundary trace and signed offset; the helper SHALL remain unaware of circle direction, FSM phase, scene telemetry, or candidate arbitration.

#### Scenario: InnerTrace path composition uses shared helper
- **WHEN** current-frame reference context is `InnerTrace`
- **AND** the geometry observer can observe a finite leading-contiguous locked-side inner edge trace
- **THEN** CircleV2 reference composition SHALL call the shared single-boundary normal-offset helper with that inner edge trace
- **AND** it SHALL pass `CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M` mapped to the caller-owned signed normal offset for the locked direction
- **AND** `CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M = 0.0` SHALL produce a path on the observed inner edge
- **AND** CircleV2 SHALL NOT maintain a separate private fixed-lateral-offset formula for InnerTrace

#### Scenario: ExitTrace path composition uses shared helper
- **WHEN** current-frame reference context is `ExitTrace`
- **AND** the geometry observer can observe the role-specific outer straight edge trace
- **THEN** CircleV2 reference composition SHALL call the shared single-boundary normal-offset helper with that outer edge trace
- **AND** it SHALL pass the existing caller-owned signed road-half-width offset for the locked direction
- **AND** CircleV2 SHALL NOT duplicate the helper's local-direction offset formula

#### Scenario: Helper output absence does not mutate CircleV2 memory
- **WHEN** CircleV2 geometry is available as an edge trace but the shared helper cannot produce a finite leading-contiguous role path
- **THEN** `CircleV2StepResult.reference_plan` SHALL be empty for that frame
- **AND** `CircleV2Reducer` SHALL remain the only owner of `next_memory`
- **AND** helper failure SHALL NOT reset, roll back, or skip a CircleV2 phase

### Requirement: Circle V2 Composition Respects Cross Suppression Evidence
The runtime composition layer SHALL suppress CircleV2 stepping when cross-exit evidence is present and cross takeover is enabled. This suppression SHALL use the public `cross_exit.present` evidence fact, not the existence of a built cross visual-reference candidate, because V4 ordinary reference repair can intentionally leave the line candidate unavailable.

CircleV2Scene MUST NOT read cross detector internals, cross evidence internals, or cross candidate internals to decide FSM transitions.

#### Scenario: Cross evidence suppresses active CircleV2 without a built cross candidate
- **WHEN** `element_evidence.cross_exit.present` is true
- **AND** cross takeover is enabled
- **AND** the cross candidate cannot be built because the ordinary line candidate is unavailable
- **THEN** the composition layer SHALL not step `CircleV2Scene` for that frame
- **AND** active CircleV2 memory SHALL be reset through scene lifecycle handling
- **AND** this SHALL NOT require cross evidence to enter visual-reference arbitration
