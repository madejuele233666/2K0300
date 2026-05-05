## ADDED Requirements

### Requirement: Element Evidence Supports Backward-Compatible Extension Records
`VisualElementEvidenceFrame` SHALL preserve the existing typed `cross_exit` evidence and SHALL support a generic extension record list for future visual elements. Generic records SHALL be append-only evidence facts with an element id, present/confidence status, metric bounds, support counters, reason string, and candidate summary.

The public JSON shape for generic records SHALL be `element_evidence.records`, an array serialized after `element_evidence.cross_exit`. Each record SHALL use these keys:

- `id`: non-empty element identifier string such as `circle_left`, `roadblock`, or `ml_track`
- `present`: boolean
- `confidence`: number
- `reason`: string
- `bounds`: object with `forward_min_m`, `forward_max_m`, `lateral_min_m`, and `lateral_max_m`
- `support`: object with `sampleable_count`, `supporting_white_count`, `supporting_black_count`, and `unknown_count`
- `candidate`: object with `built`, `takeover_enabled`, `included_in_arbitration`, and `reason`

`bounds` and `support` SHALL always be present. When a detector has no meaningful bounds or support for a record, it SHALL encode zero-valued bounds and zero-valued counters rather than omitting the objects or using `null`. Structured JSON surfaces SHALL preserve the ordering `cross_exit` first, then `records`. Text/debug surfaces that flatten evidence SHALL use the same `element_evidence.records[index].<field>` spelling when they emit generic records.

#### Scenario: Cross-exit typed evidence remains stable
- **WHEN** existing code reads `element_evidence.cross_exit`
- **THEN** the typed `cross_exit` fields and candidate summary fields SHALL remain available with their existing names and meanings
- **AND** adding generic records SHALL NOT require existing cross consumers to read those records

#### Scenario: Unknown element records are ignorable
- **WHEN** a consumer receives a generic element evidence record with an unrecognized id
- **THEN** the consumer MAY ignore that record
- **AND** recognized typed evidence such as `cross_exit` SHALL remain parseable without depending on that record

#### Scenario: Generic records have a stable wire shape
- **WHEN** assistant telemetry or steering media image headers serialize generic element evidence records
- **THEN** the JSON SHALL contain `element_evidence.records` after `element_evidence.cross_exit`
- **AND** each record SHALL contain the required `id`, `present`, `confidence`, `reason`, `bounds`, `support`, and `candidate` keys
- **AND** old consumers that read only `element_evidence.cross_exit` SHALL not need to understand `records`

### Requirement: Cross-Exit Implementation Is Isolated In Cross-Specific Files
The cross-exit detector and candidate builder SHALL be implemented in cross-specific legacy files while preserving the existing cross-exit evidence and candidate behavior. Generic element pipeline files SHALL own aggregation, not cross-specific detection logic.

#### Scenario: Cross split does not change detector output
- **WHEN** sparse BEV row facts contain the same cross support as before the split
- **THEN** `DetectCrossExitEvidence` SHALL report the same present/absent facts, confidence semantics, metric bounds, support counts, and reason strings
- **AND** `BuildCrossExitVisualReferenceCandidate` SHALL preserve existing built/takeover/included/reason semantics

#### Scenario: Cross remains default-off for arbitration
- **WHEN** `BEV_ELEMENT.CROSS_EXIT_TAKEOVER_ENABLED=false`
- **THEN** a built cross candidate SHALL continue to report `included_in_arbitration=false`
- **AND** the visual reference orchestration input set SHALL not include that cross candidate

## MODIFIED Requirements

### Requirement: Cross-Exit Evidence Is A BEV Metric Visual Fact
The runtime SHALL derive `cross_exit` element evidence only from current-frame BEV metric visual facts produced by sparse BEV row scans. The detector MUST publish whether the evidence is present, its confidence, BEV metric bounds, sample support counts, and an explanatory reason. Moving cross into cross-specific implementation files SHALL NOT change these public facts.

#### Scenario: Cross evidence reports present from supported wide rows
- **WHEN** sparse BEV row scans contain enough contiguous sampleable rows with wide white support
- **THEN** `element_evidence.cross_exit.present` SHALL be true
- **AND** the evidence SHALL include finite forward and lateral metric bounds, non-zero support counts, confidence, and reason `present`

#### Scenario: Cross evidence fails closed when support is insufficient
- **WHEN** sparse BEV row scans are absent, have insufficient sampleable support, have no supported wide rows, or only low-confidence support
- **THEN** `element_evidence.cross_exit.present` SHALL be false
- **AND** the evidence SHALL expose a deterministic reason such as `no_sparse_rows`, `insufficient_sampleable_support`, `wide_white_rows_absent`, or `low_confidence`
