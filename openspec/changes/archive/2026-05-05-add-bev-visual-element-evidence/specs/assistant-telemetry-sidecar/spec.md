## ADDED Requirements

### Requirement: Assistant Telemetry Exposes Element Evidence Facts
Assistant telemetry SHALL include a read-only `element_evidence` object that mirrors the public steering snapshot element-evidence facts. This object SHALL NOT add commands, ACKs, state mutations, raw media payloads, or alternate control framing to the accepted assistant JSON-line session.

#### Scenario: Telemetry carries cross-exit evidence without changing control framing
- **WHEN** the runtime publishes assistant telemetry
- **THEN** the JSON payload SHALL include `element_evidence.cross_exit`
- **AND** the mirrored `cross_exit.candidate` object SHALL include `built`, `takeover_enabled`, `included_in_arbitration`, and `reason`
- **AND** the accepted assistant session SHALL continue to carry only `command`, `ack`, `state`, and `telemetry` frames using newline-delimited JSON
