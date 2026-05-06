## MODIFIED Requirements

### Requirement: Cross-Exit Evidence Is A BEV Metric Visual Fact
The runtime SHALL derive `cross_exit` element evidence only from current-frame BEV metric visual facts. A present cross fact SHALL require both left-side and right-side opening evidence plus enough contiguous strict wide-white rows. A strict wide-white row SHALL meet the configured `BEV_ELEMENT.CROSS_WIDE_ROW_WHITE_RATIO_MIN` threshold in addition to existing width, reach, balance, sampleability, unknown-ratio, and confidence gates.

#### Scenario: Strict wide-white rows with bilateral openings report cross present
- **WHEN** sparse BEV row scans contain enough contiguous sampleable rows with wide white support
- **AND** each accepted wide row's `white_count / sampleable_count` meets `BEV_ELEMENT.CROSS_WIDE_ROW_WHITE_RATIO_MIN`
- **AND** the current-frame boundary facts indicate left-side opening and right-side opening
- **THEN** `element_evidence.cross_exit.present` SHALL be true
- **AND** the evidence SHALL include finite forward and lateral metric bounds, non-zero support counts, confidence, and reason `present`

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

## ADDED Requirements

### Requirement: Circle Evidence Is A Raster-Backed Current-Frame Visual Fact
The runtime SHALL derive Phase 1 circle element evidence only from the current-frame `BEVElementRasterFrame`. The circle detector MUST NOT read cross evidence, sparse line candidates, reference hold memory, safety state, IMU, encoder, low-voltage state, actuator state, yaw memory, `PerceptionResult`, or control phase.

The detector SHALL evaluate the minimal visual rule:

- `circle_left` is left-side opening plus right-side straight support
- `circle_right` is right-side opening plus left-side straight support
- strict wide-white bilateral opening is not circle evidence and remains cross-like
- bilateral opening with non-straight side support is not circle evidence and remains bend/non-circle
- neither side opening is not circle evidence and remains ordinary straight or curve evidence

Each circle evidence fact SHALL publish a present flag, confidence, BEV metric bounds, support counters, and deterministic reason. Support counters SHALL distinguish sampleable raster support, supporting white support, supporting black support, and unknown support.

#### Scenario: Left opening with right straight support reports left circle raw evidence
- **WHEN** the current-frame raster has enough sampleable support, left-side opening evidence, and right-side straight support
- **THEN** `element_evidence.records` SHALL include `circle_left_raw`
- **AND** `circle_left_raw.present` SHALL be true
- **AND** `circle_left_raw.reason` SHALL be `present`
- **AND** `circle_left_raw.confidence` SHALL meet or exceed the configured circle present threshold
- **AND** `circle_left_raw.bounds` and `circle_left_raw.support` SHALL describe finite BEV metric support for the observed fact

#### Scenario: Right opening with left straight support reports right circle raw evidence
- **WHEN** the current-frame raster has enough sampleable support, right-side opening evidence, and left-side straight support
- **THEN** `element_evidence.records` SHALL include `circle_right_raw`
- **AND** `circle_right_raw.present` SHALL be true
- **AND** `circle_right_raw.reason` SHALL be `present`
- **AND** `circle_right_raw.confidence` SHALL meet or exceed the configured circle present threshold
- **AND** `circle_right_raw.bounds` and `circle_right_raw.support` SHALL describe finite BEV metric support for the observed fact

#### Scenario: Both sides opening does not become raw circle evidence
- **WHEN** the current-frame raster indicates openings on both sides
- **THEN** `circle_left_raw.present` SHALL be false
- **AND** `circle_right_raw.present` SHALL be false
- **AND** the raw circle absence reason SHALL distinguish strict both-open, bend-like double-opening, or saturated wide-white-row cases from ordinary unsupported evidence

#### Scenario: Opposite shrink does not become raw circle evidence
- **WHEN** the current-frame raster indicates one side opening
- **AND** the opposite boundary fit indicates significant shrink rather than straight support
- **THEN** the corresponding raw circle record SHALL be false
- **AND** the raw circle absence reason SHALL identify bend/non-circle geometry rather than reporting circle present

#### Scenario: Neither side opening does not become raw circle evidence
- **WHEN** the current-frame raster indicates neither side opening
- **THEN** `circle_left_raw.present` SHALL be false
- **AND** `circle_right_raw.present` SHALL be false
- **AND** the raw circle absence reason SHALL distinguish the no-opening case from projection or sampleability failure

#### Scenario: Raster unavailable fails closed
- **WHEN** the element raster is absent, disabled, invalid, or has insufficient sampleable support
- **THEN** `circle_left_raw.present` and `circle_right_raw.present` SHALL be false
- **AND** each raw record SHALL expose a deterministic absent reason such as `raster_unavailable`, `insufficient_sampleable_support`, or `low_confidence`

### Requirement: Circle Records Preserve Raw Facts And Pipeline-Suppressed Effective Facts
The visual element pipeline SHALL always publish four circle generic records in this order after `cross_exit`: `circle_left_raw`, `circle_right_raw`, `circle_left`, and `circle_right`. Raw records SHALL preserve detector output. Effective records SHALL be the records consumed by future Phase 2 line-following work.

Cross suppression SHALL occur only in `steering_visual_element_pipeline.*`. The cross detector MUST NOT know about circle evidence, and the circle detector MUST NOT know about cross evidence. Suppression SHALL NOT delete raw circle detector facts.

#### Scenario: Cross present suppresses effective circle records only
- **WHEN** `element_evidence.cross_exit.present` is true in the visual element pipeline
- **THEN** `circle_left_raw` and `circle_right_raw` SHALL preserve the circle detector's raw present/confidence/bounds/support/reason values
- **AND** `circle_left.present` and `circle_right.present` SHALL be false
- **AND** the effective circle record reason SHALL be `suppressed_by_cross_exit`

#### Scenario: Cross absent copies raw circle facts to effective records
- **WHEN** `element_evidence.cross_exit.present` is false
- **THEN** `circle_left` SHALL mirror `circle_left_raw`
- **AND** `circle_right` SHALL mirror `circle_right_raw`
- **AND** absent raw records SHALL remain absent in their effective records with the detector-provided reason

#### Scenario: Phase 1 circle evidence does not enter visual-reference arbitration
- **WHEN** any circle raw or effective record is present
- **THEN** the circle record candidate summary SHALL report `built=false`
- **AND** it SHALL report `takeover_enabled=false`
- **AND** it SHALL report `included_in_arbitration=false`
- **AND** it SHALL report `reason=evidence_only`
- **AND** the visual element pipeline SHALL NOT push a `VisualReferenceCandidate` with kind `kCircleLeft` or `kCircleRight`

### Requirement: Circle Evidence Parameters Are Append-Only And Default-Off For Takeover
The runtime SHALL add append-only `BEV_ELEMENT` parameters for circle evidence without changing the existing `CROSS_EXIT_TAKEOVER_ENABLED` field or its default disabled semantics. The accepted circle evidence parameters SHALL include:

- `CROSS_WIDE_ROW_WHITE_RATIO_MIN`, default `0.95`
- `CIRCLE_EVIDENCE_ENABLED`, default `true`
- `CIRCLE_MIN_SUPPORT_ROWS`, default `4`
- `CIRCLE_MIN_SAMPLEABLE_PER_ROW`, default `16`
- `CIRCLE_OPEN_EXPANSION_MIN_M`, default `0.05`
- `CIRCLE_OPENING_EXPANSION_RATIO_MIN`, default `0.10`
- `CIRCLE_OPPOSITE_STRAIGHT_DRIFT_MAX_M`, default `0.06`
- `CIRCLE_OPPOSITE_SHRINK_RATIO_MIN`, default `0.10`
- `CIRCLE_PRESENT_CONFIDENCE_MIN`, default `0.65`

Missing circle/cross evidence parameter fields SHALL use `RuntimeParameters{}` defaults. Malformed or out-of-range circle/cross evidence parameters SHALL follow the existing parameter parse-failure behavior rather than silently changing actuator behavior.

#### Scenario: Missing evidence parameter fields use runtime defaults
- **WHEN** `BEV_ELEMENT` omits one or more circle or cross evidence parameter fields
- **THEN** the runtime SHALL use the corresponding `RuntimeParameters{}` evidence defaults
- **AND** `CROSS_EXIT_TAKEOVER_ENABLED` SHALL keep its existing parsed or default value

#### Scenario: Explicit circle disable produces not-evaluated circle records
- **WHEN** `BEV_ELEMENT.CIRCLE_EVIDENCE_ENABLED` is false
- **THEN** the visual element pipeline SHALL still publish all four circle records
- **AND** those circle records SHALL be absent with a deterministic disabled/not-evaluated reason
- **AND** no circle visual-reference candidate SHALL be pushed

#### Scenario: Invalid evidence parameters follow parameter parse-failure fallback
- **WHEN** a circle or cross evidence parameter has an invalid type or invalid range
- **THEN** runtime parameter loading SHALL mark the parameter input malformed according to the existing optional-parameter failure path
- **AND** the runtime SHALL fall back to documented runtime defaults instead of applying a partially invalid evidence threshold set

### Requirement: Offline Probe Uses Runtime Raster Input For Circle Evidence Observation
The authority-baseline offline probe path SHALL construct and pass `BEVElementRasterFrame` into `RunVisualElementPipeline` when evaluating visual element evidence. Probe output MAY serialize and print circle evidence records for observation, but it SHALL NOT become a runtime control authority.

#### Scenario: Authority-baseline circle sample can be evaluated through the same raster input path
- **WHEN** `scene_overlay_probe` evaluates an authority-baseline raw frame such as `circle-2.raw`
- **THEN** the probe SHALL build the same kind of element raster input that the runtime frame pipeline passes to the visual element pipeline
- **AND** it SHALL expose `element_record id=circle_left_raw` or `element_record id=circle_right_raw` output when the detector reports raw circle evidence

#### Scenario: Authority-baseline fixtures have deterministic circle expectations
- **WHEN** the authority-baseline probe suite evaluates `circle-1.raw`, `circle-2.raw`, or `circle-3.raw`
- **THEN** it SHALL expect `cross_exit.present=false`, `circle_left_raw.present=true`, and effective `circle_left.present=true`
- **WHEN** the authority-baseline probe suite evaluates `cross-1.raw`, `cross-2.raw`, or `cross-3.raw`
- **THEN** it SHALL expect cross evidence and absent raw/effective circle records because both sides are open
- **WHEN** the authority-baseline probe suite evaluates `bend-1.raw`, `bend-2.raw`, or `bend-3.raw`
- **THEN** it SHALL expect absent raw/effective circle records

#### Scenario: Probe changes do not alter runtime control behavior
- **WHEN** offline probe output includes circle evidence records
- **THEN** those records SHALL remain observational evidence only
- **AND** runtime selected-reference, hold, safety, yaw, and actuator behavior SHALL remain outside the probe path
