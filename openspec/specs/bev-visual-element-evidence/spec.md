# bev-visual-element-evidence Specification

## Purpose
Define BEV visual element evidence as metric visual facts that can be observed and optionally converted into reference candidates without coupling the detector to control owners.

## Requirements
### Requirement: Cross-Exit Evidence Is A BEV Metric Visual Fact
The runtime SHALL derive `cross_exit` element evidence only from current-frame BEV metric visual facts produced by sparse BEV row scans. A present cross fact SHALL require both left-side and right-side opening evidence plus enough contiguous strict wide-white rows. A strict wide-white row SHALL meet the configured `BEV_ELEMENT.CROSS_WIDE_ROW_WHITE_RATIO_MIN` threshold in addition to existing width, reach, balance, sampleability, unknown-ratio, and confidence gates. The detector MUST publish whether the evidence is present, its confidence, BEV metric bounds, sample support counts, and an explanatory reason.

#### Scenario: Cross evidence reports present from supported wide rows
- **WHEN** sparse BEV row scans contain enough contiguous sampleable rows with wide white support
- **AND** each accepted wide row's `white_count / sampleable_count` meets `BEV_ELEMENT.CROSS_WIDE_ROW_WHITE_RATIO_MIN`
- **AND** the current-frame boundary facts indicate left-side opening and right-side opening
- **THEN** `element_evidence.cross_exit.present` SHALL be true
- **AND** the evidence SHALL include finite forward and lateral metric bounds, non-zero support counts, confidence, and reason `present`

#### Scenario: Cross evidence fails closed when support is insufficient
- **WHEN** sparse BEV row scans are absent, have insufficient sampleable support, have no supported wide rows, or only low-confidence support
- **THEN** `element_evidence.cross_exit.present` SHALL be false
- **AND** the evidence SHALL expose a deterministic reason such as `no_sparse_rows`, `insufficient_sampleable_support`, `wide_white_rows_absent`, or `low_confidence`

#### Scenario: Wide support below strict white ratio fails closed
- **WHEN** sparse BEV row scans are wide enough geometrically
- **AND** the accepted rows' `white_count / sampleable_count` is below `BEV_ELEMENT.CROSS_WIDE_ROW_WHITE_RATIO_MIN`
- **THEN** `element_evidence.cross_exit.present` SHALL be false
- **AND** the evidence SHALL expose a deterministic absent reason such as `wide_white_rows_absent`

#### Scenario: Missing bilateral opening fails closed
- **WHEN** sparse BEV row scans contain wide white support
- **AND** the current-frame boundary facts do not indicate both left-side and right-side opening
- **THEN** `element_evidence.cross_exit.present` SHALL be false
- **AND** the evidence SHALL expose a deterministic absent reason rather than treating one-sided circle or bend geometry as cross

### Requirement: Element Detection Is Isolated From Control Owners
The visual element evidence pipeline SHALL derive non-circle visual element evidence from current-frame BEV visual facts without coupling detectors to control owners. `cross_exit` behavior and candidate takeover semantics SHALL remain governed by the existing BEV visual element evidence requirements.

Circle semantics SHALL NOT be owned by the visual element evidence pipeline. Phase1 circle cue semantics SHALL be private to `CircleV2Scene` event observation and SHALL NOT be published as visual element evidence records.

#### Scenario: Runtime control remains line-only by default
- **WHEN** `cross_exit` evidence is present and the takeover parameter remains at its default false value
- **THEN** the runtime SHALL continue selecting visual references from the line candidate only
- **AND** the evidence MAY report a built candidate whose `included_in_arbitration` flag is false

#### Scenario: Runtime visual element pipeline emits no circle records
- **WHEN** `RunVisualElementPipeline()` evaluates a runtime frame
- **THEN** it SHALL continue to evaluate cross / non-circle visual element evidence
- **AND** it SHALL NOT publish `circle_left_raw`, `circle_right_raw`, `circle_left`, or `circle_right` records
- **AND** it SHALL NOT include circle candidate summaries in `element_evidence.records`

#### Scenario: Cross remains isolated from circle v2
- **WHEN** `cross_exit` evidence is present or absent
- **THEN** the cross detector SHALL remain unaware of `CircleV2Scene`
- **AND** `CircleV2Scene` SHALL not read cross detector internals to decide its FSM transitions

### Requirement: Cross Candidate Takeover Is Explicitly Disabled By Default
The runtime SHALL include a runtime parameter controlling whether a built `cross_exit` candidate may enter visual-reference arbitration. The default SHALL be disabled in both `RuntimeParameters{}` and `default_params.json`.

#### Scenario: Candidate summary distinguishes builder outcome from arbitration inclusion
- **WHEN** the runtime evaluates a `cross_exit` candidate
- **THEN** the public evidence SHALL include candidate summary fields for `built`, `takeover_enabled`, `included_in_arbitration`, and `reason`
- **AND** `built=false` SHALL distinguish unsupported evidence or missing line geometry from a built candidate excluded only because takeover remains disabled

#### Scenario: Enabling takeover is an explicit parameter choice
- **WHEN** the takeover parameter is false
- **THEN** a built `cross_exit` candidate SHALL NOT be passed to visual-reference orchestration
- **WHEN** the takeover parameter is true
- **THEN** a built `cross_exit` candidate MAY be passed to visual-reference orchestration, but it still MUST pass existing candidate validation and downstream reference-control gates

### Requirement: Element Evidence Supports Backward-Compatible Extension Records
`VisualElementEvidenceFrame` SHALL preserve the existing typed `cross_exit` evidence and SHALL support a generic extension record list for future visual elements. Generic records SHALL be append-only evidence facts with an element id, present/confidence status, metric bounds, support counters, reason string, and candidate summary.

The public JSON shape for generic records SHALL be `element_evidence.records`, an array serialized after `element_evidence.cross_exit`. Each record SHALL use these keys:

- `id`: non-empty element identifier string such as `roadblock` or `ml_track`
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
- **THEN** `DetectCrossExitEvidence` SHALL report the same present/absent facts, confidence semantics, metric bounds, support counts, and reason strings, subject to later explicit cross-recognition requirements such as strict wide-white-row support
- **AND** `BuildCrossExitVisualReferenceCandidate` SHALL preserve existing built/takeover/included/reason semantics

#### Scenario: Cross remains default-off for arbitration
- **WHEN** `BEV_ELEMENT.CROSS_EXIT_TAKEOVER_ENABLED=false`
- **THEN** a built cross candidate SHALL continue to report `included_in_arbitration=false`
- **AND** the visual reference orchestration input set SHALL not include that cross candidate
