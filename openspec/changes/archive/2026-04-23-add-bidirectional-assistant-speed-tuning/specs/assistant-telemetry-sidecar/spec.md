## MODIFIED Requirements

### Requirement: First Release Assistant Surface Is Scoped Bidirectional
The accepted release of the assistant sidecar SHALL move from strictly read-only behavior to a scoped bidirectional surface. The accepted bidirectional surface SHALL remain limited to project-owned tuning and operator-intent commands plus project-owned ACK/state/telemetry feedback, and SHALL NOT become a general writable parameter-mutation backdoor.

#### Scenario: Supported inbound commands use the scoped sidecar surface only
- **WHEN** assistant receive parsing encounters a supported project-owned tuning or operator-intent command
- **THEN** the sidecar SHALL route it through the accepted project-owned command contract
- **AND** it SHALL NOT mutate runtime parameters, lifecycle phase, control state, or vendor globals outside that contract

#### Scenario: Unsupported inbound payloads remain isolated
- **WHEN** assistant receive parsing encounters malformed data, unsupported commands, or payloads outside the accepted tuning scope
- **THEN** the sidecar SHALL reject that payload with diagnosable project-owned evidence using the accepted standalone `state` rejection path
- **AND** startup, control-loop bring-up, and fail-safe behavior SHALL remain unaffected

### Requirement: Assistant Transport Boundary Reserves Extension While Shipping TCP Only
The assistant sidecar SHALL continue to depend on a project-owned transport abstraction that can host future transports, but the accepted implementation for this change SHALL still be TCP only for both outbound publication and inbound command reception.

#### Scenario: First release transport remains TCP only even after bidirectional support is added
- **WHEN** reviewers inspect the assistant transport integration for this change
- **THEN** they SHALL find that the accepted inbound and outbound assistant traffic both use the TCP-backed transport path
- **AND** reviewers SHALL NOT find UDP or serial assistant transports treated as implemented acceptance behavior

## ADDED Requirements

### Requirement: Assistant Command Handling Stays Outside The Control Timer
Assistant command receive parsing, ACK generation, and structured state publication SHALL remain outside the periodic control timer callback just like assistant waveform and image handling.

#### Scenario: Bidirectional assistant work remains foreground-sidecar behavior
- **WHEN** the runtime processes assistant send/receive traffic for tuning commands or ACK/state feedback
- **THEN** that work SHALL execute in the foreground loop or another non-control-timer context
- **AND** the control timer SHALL NOT own assistant JSON parsing or transport I/O

### Requirement: Assistant Sidecar Feedback Is Project-Owned
The accepted bidirectional sidecar SHALL expose project-owned ACK/state semantics suitable for host tooling rather than requiring the host to infer acceptance only from waveform changes.

#### Scenario: Host feedback does not depend on vendor-only waveform semantics
- **WHEN** the host issues a supported command over the assistant sidecar
- **THEN** the runtime SHALL publish project-owned feedback stating whether the command was accepted, rejected, or cleared
- **AND** that feedback SHALL remain available even if the host is not rendering the vendor waveform UI

### Requirement: First Release Bidirectional Sidecar Uses One Fixed JSON-Line Host Contract
The accepted first-release bidirectional sidecar SHALL freeze one project-owned host interoperability contract for commands, structured ACK/state feedback, and structured telemetry. The accepted host contract SHALL use newline-delimited JSON on the accepted assistant TCP session.

#### Scenario: Host command and feedback framing is not left unresolved
- **WHEN** reviewers inspect the accepted first-release host tuning workflow and sidecar contract
- **THEN** they SHALL find one fixed newline-delimited JSON framing model for inbound commands and project-owned outbound ACK/state/structured telemetry
- **AND** they SHALL NOT find the first-release command or feedback topology left as an implementation-time choice

#### Scenario: Standalone state events use a frozen project-owned payload
- **WHEN** the sidecar publishes a standalone `state` frame for tuning-mode transitions or tuning-snapshot clear behavior
- **THEN** that frame SHALL use the accepted project-owned required fields for event name, human-readable `reason` string, and current tuning snapshot state
- **AND** host interoperability SHALL NOT depend on sidecar-specific ad hoc state payloads

#### Scenario: Optional assistant waveform and image publication remain support evidence only
- **WHEN** the runtime also enables assistant-compatible waveform or image publication
- **THEN** that publication SHALL coexist with the accepted newline-delimited JSON host contract as optional support evidence
- **AND** reviewers SHALL NOT treat waveform or image publication as an alternate command, ACK, or structured telemetry transport for the accepted first release
