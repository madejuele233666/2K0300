## ADDED Requirements

### Requirement: Host Live Viewer Uses The Accepted Steering Media Stream
The host tooling SHALL provide an optional live browser viewer that consumes the accepted steering media envelope stream without changing the board-side media protocol, media port configuration, or read-only session semantics.

#### Scenario: Live viewer renders the same decoded image frames as the evidence recorder
- **WHEN** host capture is started with live viewing enabled and the board or a test peer sends accepted `config_snapshot` and `image_frame` envelopes
- **THEN** the host SHALL decode those envelopes through the same media ingest path used for evidence capture
- **AND** the browser viewer SHALL receive the image frame dimensions, source dimensions, downsample value, steering snapshot metadata, and raw `gray8` payload needed to render the frame
- **AND** the evidence bundle SHALL still preserve the accepted `config_snapshot`, `frame_metadata.jsonl`, raw frame files, and summary files.

#### Scenario: Live viewer remains a host-side adapter
- **WHEN** implementers add or modify the live viewing surface
- **THEN** board runtime code SHALL still only know how to connect to the configured host media TCP endpoint and send accepted steering media envelopes
- **AND** browser, HTTP, WebSocket, canvas, and live UI concepts SHALL NOT appear in board runtime, platform, port, legacy, config, or steering media protocol code.

### Requirement: Live Viewer Fan-Out Is Read-Only And Drop-Tolerant
The live viewer fan-out SHALL be lower priority than media ingest and evidence recording. Slow, disconnected, or absent browser clients SHALL NOT block TCP receive, evidence persistence, control telemetry capture, or board media reconnect behavior.

#### Scenario: Slow browser clients do not backpressure media ingest
- **WHEN** a browser client cannot keep up with incoming image frames
- **THEN** the live fan-out SHALL be allowed to drop stale live-view frames or keep only the latest frame for that client
- **AND** the host evidence recorder SHALL continue to validate and persist accepted media envelopes independently of the browser client state.

#### Scenario: Live viewer does not become a command channel
- **WHEN** a browser connects to the live viewer
- **THEN** the viewer SHALL expose only read-only media status and image display behavior
- **AND** it SHALL NOT accept assistant commands, motion commands, runtime parameter writes, ACKs, or state mutations.
