## MODIFIED Requirements

### Requirement: Circle Records Preserve Raw Facts And Pipeline-Suppressed Effective Facts
The visual element pipeline SHALL continue to publish four circle generic records in this order after `cross_exit`: `circle_left_raw`, `circle_right_raw`, `circle_left`, and `circle_right`. Raw records SHALL preserve detector output. Effective records SHALL be the only circle records eligible for Phase 2 circle entry candidate construction.

Cross suppression SHALL occur only in `steering_visual_element_pipeline.*`. The cross detector MUST NOT know about circle evidence, and the circle detector MUST NOT know about cross evidence. Suppression SHALL NOT delete raw circle detector facts. After suppression, a raw record MAY remain present, but the corresponding effective record SHALL be absent and MUST NOT produce a circle entry candidate.

#### Scenario: Cross present suppresses effective circle records and candidate build
- **WHEN** `element_evidence.cross_exit.present` is true in the visual element pipeline
- **THEN** `circle_left_raw` and `circle_right_raw` SHALL preserve the circle detector's raw present/confidence/bounds/support/reason values
- **AND** `circle_left.present` and `circle_right.present` SHALL be false
- **AND** the effective circle record reason SHALL be `suppressed_by_cross_exit`
- **AND** no `kCircleLeft` or `kCircleRight` candidate SHALL be built or pushed

#### Scenario: Cross absent allows effective circle candidate summary
- **WHEN** `element_evidence.cross_exit.present` is false
- **THEN** `circle_left` SHALL mirror `circle_left_raw`
- **AND** `circle_right` SHALL mirror `circle_right_raw`
- **AND** a present effective circle record MAY report a built circle entry candidate summary
- **AND** arbitration inclusion SHALL still depend on `BEV_ELEMENT.CIRCLE_ENTRY_TAKEOVER_ENABLED`

#### Scenario: Raw circle records remain evidence-only
- **WHEN** `circle_left_raw` or `circle_right_raw` is present
- **THEN** the raw record candidate summary SHALL report `built=false`
- **AND** it SHALL report `takeover_enabled=false`
- **AND** it SHALL report `included_in_arbitration=false`
- **AND** it SHALL report `reason=evidence_only`

### Requirement: Circle Evidence Parameters Are Append-Only And Default-Off For Takeover
The runtime SHALL add append-only `BEV_ELEMENT` parameters for circle entry candidate construction without changing existing cross takeover or Phase 1 circle evidence field meanings. The accepted Phase 2 circle entry parameters SHALL include:

- `CIRCLE_ENTRY_TAKEOVER_ENABLED`, default `false`
- `CIRCLE_ENTRY_MIN_FRONTIER_POINTS`, default `4`
- `CIRCLE_ENTRY_DIRECTION_MIN_LATERAL_M`, default `0.08`
- `CIRCLE_ENTRY_MAX_INTERPOLATION_GAP_M`, default `0.12`
- `CIRCLE_ENTRY_MAX_JOIN_JUMP_M`, default `0.12`

Missing circle entry parameter fields SHALL use `RuntimeParameters{}` defaults. Malformed or out-of-range circle entry parameters SHALL follow the existing parameter parse-failure behavior rather than silently changing actuator behavior.

#### Scenario: Missing entry parameter fields use runtime defaults
- **WHEN** `BEV_ELEMENT` omits one or more circle entry parameter fields
- **THEN** the runtime SHALL use the corresponding `RuntimeParameters{}` entry defaults
- **AND** existing cross and Phase 1 circle evidence parameters SHALL keep their parsed or default values

#### Scenario: Default entry takeover is disabled
- **WHEN** runtime parameters are default-constructed or loaded from `default_params.json`
- **THEN** `BEV_ELEMENT.CIRCLE_ENTRY_TAKEOVER_ENABLED` SHALL be false
- **AND** a built circle entry candidate SHALL not enter arbitration unless the parameter is explicitly enabled

#### Scenario: Invalid entry parameters follow parameter parse-failure fallback
- **WHEN** a circle entry parameter has an invalid type or invalid range
- **THEN** runtime parameter loading SHALL mark the parameter input malformed according to the existing optional-parameter failure path
- **AND** the runtime SHALL fall back to documented runtime defaults instead of applying a partially invalid entry threshold set

## ADDED Requirements

### Requirement: Circle Entry Typed Facts Do Not Change Generic Record Wire Shape
The runtime MAY extend internal circle typed facts with near-connected component, frontier, half-width, centerline, direction, and reason fields. These typed facts SHALL NOT add required keys to the public `VisualElementEvidenceRecord` JSON shape. Assistant telemetry, steering media, and text/debug surfaces that serialize generic records SHALL preserve the existing required keys: `id`, `present`, `confidence`, `reason`, `bounds`, `support`, and `candidate`.

#### Scenario: Generic circle records remain backward-compatible
- **WHEN** circle entry typed facts are present internally
- **THEN** serialized generic records SHALL still contain the existing record keys
- **AND** old consumers that ignore unknown element ids or read only generic record summaries SHALL remain parseable without understanding typed entry facts

#### Scenario: Probe may expose entry facts as debug-only output
- **WHEN** `scene_overlay_probe` prints circle entry frontier, half-width, or candidate path diagnostics
- **THEN** those fields SHALL be observational debug output
- **AND** detector, builder, selected-reference, hold, safety, yaw, and actuator code SHALL NOT read probe-rendered output as input authority

### Requirement: Offline Probe Observes Circle Entry Candidate Behavior
The authority-baseline offline probe path SHALL expose circle entry candidate summary and selected-reference behavior for both default takeover-disabled and explicitly takeover-enabled runs. Probe output MAY serialize or print circle entry diagnostics, but it SHALL NOT become a runtime control authority.

#### Scenario: Authority-baseline circle samples build default-excluded candidates
- **WHEN** the authority-baseline probe suite evaluates `circle-1.raw`, `circle-2.raw`, or `circle-3.raw` with default parameters
- **THEN** it SHALL expect `cross_exit.present=false`
- **AND** effective `circle_left.present=true`
- **AND** effective `circle_left.candidate.built=true`
- **AND** effective `circle_left.candidate.included_in_arbitration=false`
- **AND** the selected visual reference SHALL remain the ordinary line source

#### Scenario: Takeover-enabled authority-baseline circle samples select circle reference
- **WHEN** the authority-baseline probe suite evaluates `circle-1.raw`, `circle-2.raw`, or `circle-3.raw`
- **AND** `BEV_ELEMENT.CIRCLE_ENTRY_TAKEOVER_ENABLED=true`
- **THEN** a valid `kCircleLeft` candidate SHALL enter arbitration
- **AND** `visual_reference.source` SHALL be `circle_left`

#### Scenario: Cross and bend authority-baseline samples do not build circle candidates
- **WHEN** the authority-baseline probe suite evaluates `cross-1.raw`, `cross-2.raw`, or `cross-3.raw`
- **THEN** it SHALL expect cross evidence and no built circle entry candidate
- **WHEN** the authority-baseline probe suite evaluates `bend-1.raw`, `bend-2.raw`, or `bend-3.raw`
- **THEN** it SHALL expect absent effective circle records and no built circle entry candidate
