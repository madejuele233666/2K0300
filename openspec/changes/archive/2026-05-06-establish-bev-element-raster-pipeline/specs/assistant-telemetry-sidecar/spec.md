## MODIFIED Requirements

### Requirement: Assistant Telemetry Exposes Element Evidence Facts
Assistant telemetry SHALL include a read-only `element_evidence` object that mirrors the public steering snapshot element-evidence facts through the shared element-evidence serializer. This object SHALL NOT add commands, ACKs, state mutations, raw media payloads, or alternate control framing to the accepted assistant JSON-line session. The existing `element_evidence.cross_exit` object SHALL remain stable, and generic extension records SHALL be appended in a form old consumers can ignore.

#### Scenario: Telemetry carries cross-exit evidence without changing control framing
- **WHEN** the runtime publishes assistant telemetry
- **THEN** the JSON payload SHALL include `element_evidence.cross_exit`
- **AND** the mirrored `cross_exit.candidate` object SHALL include `built`, `takeover_enabled`, `included_in_arbitration`, and `reason`
- **AND** the accepted assistant session SHALL continue to carry only `command`, `ack`, `state`, and `telemetry` frames using newline-delimited JSON

#### Scenario: Telemetry can carry generic element records compatibly
- **WHEN** element evidence contains generic extension records
- **THEN** assistant telemetry SHALL serialize those records at `element_evidence.records` after the stable `cross_exit` object
- **AND** each record SHALL follow the stable generic record schema defined by `bev-visual-element-evidence`
- **AND** consumers that only understand `cross_exit` SHALL still be able to parse the telemetry payload without interpreting the extension records
