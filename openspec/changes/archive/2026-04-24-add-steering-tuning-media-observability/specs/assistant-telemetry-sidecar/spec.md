## MODIFIED Requirements

### Requirement: Assistant Sidecar Can Publish Image Frames
The optional assistant integration MAY continue to publish assistant-compatible image frames as support evidence, but the accepted project-owned steering-tuning image and metadata surface SHALL use a separate media sidecar rather than the accepted assistant JSON control session.

#### Scenario: Assistant-compatible image publication does not redefine the accepted project-owned media path
- **WHEN** the runtime also enables assistant-compatible waveform or image publication for convenience
- **THEN** that publication SHALL remain optional support evidence only
- **AND** reviewers SHALL treat the separate project-owned steering media sidecar as the accepted image/metadata contract for steering tuning

### Requirement: First Release Bidirectional Sidecar Uses One Fixed JSON-Line Host Contract
The accepted first-release bidirectional assistant sidecar SHALL keep one project-owned newline-delimited JSON host interoperability contract for commands, structured ACK/state feedback, and structured telemetry on the accepted assistant TCP session. Steering media traffic SHALL NOT change that framing or multiplex raw image payloads onto the control connection.

#### Scenario: Control-plane framing remains frozen when steering media is enabled
- **WHEN** the steering tuning media sidecar is enabled alongside the assistant control session
- **THEN** the accepted assistant TCP session SHALL continue to carry only `command`, `ack`, `state`, and `telemetry` frames using newline-delimited JSON
- **AND** the runtime SHALL NOT place raw image bytes, binary envelopes, or media-only metadata on that accepted control session

## ADDED Requirements

### Requirement: Accepted Control And Steering Media Sessions Are Separated
The accepted steering tuning workflow SHALL separate the existing assistant JSON control plane from the new project-owned steering media plane. Both sessions MAY target the same host, but they SHALL use distinct ports, and the steering media session SHALL remain read-only push traffic from board to host.

#### Scenario: Board-owned dual connections keep command traffic off the media socket
- **WHEN** the board establishes the accepted steering tuning sidecar topology
- **THEN** it SHALL actively connect to the host once for the accepted JSON control session and once for the accepted steering media session
- **AND** the steering media session SHALL NOT accept commands or redefine the accepted control contract

### Requirement: Dual-Channel Sidecar Wiring Uses One Project-Owned Setup Surface
The accepted control/media sidecar topology SHALL use one project-owned setup surface on the board. For the accepted first release, the control host SHALL remain `assistant_tcp.host`, the control port SHALL remain `assistant_tcp.port`, and steering media wiring SHALL be expressed through `steering_media_enabled`, `steering_media_port`, and `steering_media_publish_interval_ms`.

#### Scenario: Accepted setup does not leave media endpoint wiring to hardcoded values
- **WHEN** reviewers inspect the accepted board-side setup path for steering tuning
- **THEN** they SHALL find that the steering media endpoint is loaded from the project-owned startup parameter surface and remains aligned to `assistant_tcp.host`
- **AND** they SHALL NOT find the accepted media endpoint left to hardcoded bridge constants or undocumented manual setup steps
