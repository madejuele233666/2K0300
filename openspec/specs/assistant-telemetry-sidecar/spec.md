# assistant-telemetry-sidecar Specification

## Purpose
Define the optional read-only Seekfree Assistant sidecar boundary for `new/`, including transport isolation, waveform publication, and image publication constraints.

## Requirements
### Requirement: Assistant Sidecar Is Optional And Non-Blocking
The `new/` workspace SHALL treat Seekfree Assistant integration as an optional sidecar service rather than as a startup-critical subsystem. Assistant initialization, reconnect failure, or polling failure MUST NOT block `startup.complete`, `control.start`, or the Phase B motion lifecycle.

#### Scenario: Assistant disabled or unavailable does not block runtime startup
- **WHEN** the runtime starts with assistant sidecar disabled, misconfigured, or temporarily unavailable
- **THEN** startup and control-loop bring-up SHALL continue through the accepted Phase B path
- **AND** the runtime SHALL emit diagnosable warning/info markers for the sidecar state instead of converting the failure into a new control veto or startup failure

### Requirement: Assistant Publication Uses Project-Owned Control Snapshot
Assistant waveform publication SHALL consume a project-owned control debug snapshot rather than directly reading timer-callback locals, vendor globals, or raw protocol buffers.

#### Scenario: Wave channels are sourced from a project-owned snapshot
- **WHEN** reviewers inspect the assistant publication path
- **THEN** they SHALL find a project-owned snapshot or equivalent runtime-owned structure carrying fields such as motion phase, left/right wheel targets, left/right measured speed, turn correction, and left/right PWM command
- **AND** they SHALL NOT find assistant-side code reading raw vendor camera/encoder/motor symbols or unstructured control-loop locals as its primary API

### Requirement: Assistant Polling Stays Outside The Control Timer
Seekfree Assistant protocol polling, receive parsing, waveform publication, and image publication SHALL execute outside the periodic control timer callback.

#### Scenario: Assistant processing remains in the foreground loop
- **WHEN** the runtime processes assistant send/receive traffic
- **THEN** that work SHALL be performed in the foreground `main` loop or another non-control-timer context
- **AND** the 5 ms control callback SHALL NOT own protocol parsing or transport I/O

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
The assistant sidecar SHALL depend on a project-owned transport abstraction that can host future transports, but this change SHALL implement only the TCP-backed transport path.

#### Scenario: First release transport remains TCP only even after bidirectional support is added
- **WHEN** reviewers inspect the sidecar transport integration for this change
- **THEN** they SHALL find a project-owned transport boundary or equivalent owning interface in `new/`
- **AND** the accepted inbound and outbound assistant traffic both use the TCP-backed transport path
- **AND** reviewers SHALL NOT find UDP or serial assistant transports treated as implemented behavior in the acceptance surface

### Requirement: Assistant Sidecar Can Publish Image Frames
The optional assistant sidecar SHALL be able to publish E07_04-style image-transfer data through the same project-owned sidecar boundary without making image publication startup-critical.

#### Scenario: Optional image publication shares the sidecar isolation rules
- **WHEN** the runtime enables assistant image publication
- **THEN** waveform and image publication SHALL both remain optional foreground-sidecar behavior
- **AND** assistant image publication SHALL consume project-owned image/frame handoff data rather than injecting vendor protocol APIs into `runtime` or `legacy`

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
