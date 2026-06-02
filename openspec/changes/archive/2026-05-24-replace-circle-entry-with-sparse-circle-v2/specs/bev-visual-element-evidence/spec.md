## MODIFIED Requirements

### Requirement: Element Detection Is Isolated From Control Owners
The visual element evidence pipeline SHALL derive non-circle visual element evidence from current-frame BEV visual facts without coupling detectors to control owners. `cross_exit` behavior and candidate takeover semantics SHALL remain governed by the existing BEV visual element evidence requirements.

Circle semantics SHALL NOT be owned by the visual element evidence pipeline after this change. Phase1 circle cue semantics SHALL move to `CircleV2Scene` private event observation and SHALL NOT be published as visual element evidence records.

#### Scenario: Runtime visual element pipeline emits no circle records
- **WHEN** `RunVisualElementPipeline()` evaluates a runtime frame
- **THEN** it SHALL continue to evaluate cross / non-circle visual element evidence
- **AND** it SHALL NOT publish `circle_left_raw`, `circle_right_raw`, `circle_left`, or `circle_right` records
- **AND** it SHALL NOT include circle candidate summaries in `element_evidence.records`

#### Scenario: Cross remains isolated from circle v2
- **WHEN** `cross_exit` evidence is present or absent
- **THEN** the cross detector SHALL remain unaware of `CircleV2Scene`
- **AND** `CircleV2Scene` SHALL not read cross detector internals to decide its FSM transitions

## REMOVED Requirements

### Requirement: Circle Evidence Is A Raster-Backed Current-Frame Visual Fact
This requirement is removed from `bev-visual-element-evidence`. Runtime circle detection is no longer specified as public element evidence records. The preserved Phase1 circle direction cue is specified by `sparse-circle-v2-scene` and is private to `CircleV2EventObserver`.

#### Scenario: Old circle evidence ids are absent from visual element output
- **WHEN** the runtime serializes `VisualElementEvidenceFrame`
- **THEN** the old circle evidence ids `circle_left_raw`, `circle_right_raw`, `circle_left`, and `circle_right` SHALL be absent

### Requirement: Circle Records Preserve Raw Facts And Pipeline-Suppressed Effective Facts
This requirement is removed. Cross suppression of effective circle records is obsolete because public visual element circle records are no longer emitted by `RunVisualElementPipeline()`.

#### Scenario: Cross suppression no longer creates effective circle records
- **WHEN** `cross_exit.present` is true
- **THEN** the visual element pipeline SHALL NOT create suppressed effective circle records with reason `suppressed_by_cross_exit`

### Requirement: Circle Evidence Parameters Are Append-Only And Default-Off For Takeover
This requirement is removed for runtime circle ownership. Old circle evidence and circle_entry parameters SHALL be replaced by the V2 parameter surface specified in `sparse-circle-v2-scene`.

#### Scenario: Old circle evidence and entry parameters are not runtime authority
- **WHEN** runtime parameters are loaded for the V2 circle implementation
- **THEN** old circle evidence / circle entry keys SHALL NOT control whether circle candidates are produced
- **AND** circle candidate production SHALL be controlled by `CircleV2Scene` lifecycle, V2 FSM phase, and `CIRCLE_V2_*` parameters

### Requirement: Offline Probe Uses Runtime Raster Input For Circle Evidence Observation
This requirement is removed from `bev-visual-element-evidence` for circle observation. Probe visibility for circle SHALL use CircleV2 telemetry and CircleV2 candidate/reference output, not public visual element circle records.

#### Scenario: Probe expectations move to CircleV2 telemetry
- **WHEN** an offline probe exercises a circle frame
- **THEN** it SHALL not require old element-record output such as `element_record id=circle_left_raw`
- **AND** it MAY assert CircleV2 telemetry, phase, direction, reference role, and adapted candidate source instead
