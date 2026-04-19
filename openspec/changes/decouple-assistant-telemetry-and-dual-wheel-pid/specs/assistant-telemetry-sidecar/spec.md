## ADDED Requirements

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

### Requirement: First Release Assistant Surface Is Read-Only
The first release of the assistant sidecar SHALL expose read-only waveform and image publication only, and SHALL NOT introduce a writable tuning overlay or assistant-driven parameter mutation path.

#### Scenario: Assistant receive traffic cannot change runtime parameters in the first release
- **WHEN** assistant receive parsing encounters malformed data, unsupported commands, or any candidate tuning payload
- **THEN** the sidecar SHALL ignore the payload and emit a diagnosable warning or info marker
- **AND** it SHALL NOT mutate runtime parameters, lifecycle state, control state, or vendor globals

### Requirement: Assistant Transport Boundary Reserves Extension While Shipping TCP Only
The assistant sidecar SHALL depend on a project-owned transport abstraction that can host future transports, but this change SHALL implement only the TCP-backed transport path.

#### Scenario: First release transport implementation remains TCP only
- **WHEN** reviewers inspect the sidecar transport integration for this change
- **THEN** they SHALL find a project-owned transport boundary or equivalent owning interface in `new/`
- **AND** the concrete enabled implementation for this change SHALL be TCP only
- **AND** reviewers SHALL NOT find UDP or serial assistant transports treated as implemented behavior in the acceptance surface

### Requirement: Assistant Sidecar Can Publish Image Frames
The optional assistant sidecar SHALL be able to publish E07_04-style image-transfer data through the same project-owned sidecar boundary without making image publication startup-critical.

#### Scenario: Optional image publication shares the sidecar isolation rules
- **WHEN** the runtime enables assistant image publication
- **THEN** waveform and image publication SHALL both remain optional foreground-sidecar behavior
- **AND** assistant image publication SHALL consume project-owned image/frame handoff data rather than injecting vendor protocol APIs into `runtime` or `legacy`
