## ADDED Requirements

### Requirement: Assistant Vendor Integration Stays Inside Owning Bridges
Seekfree Assistant protocol types, vendor component headers, and transport free functions SHALL remain confined to assistant-owning bridge files and SHALL NOT leak into `legacy`, `runtime`, or public port contracts.

#### Scenario: Assistant vendor APIs are isolated from runtime and legacy code
- **WHEN** reviewers inspect the assistant integration path for this change
- **THEN** any direct use of `seekfree_assistant_*`, `tcp_client_*`, or equivalent vendor protocol/transport APIs SHALL appear only in assistant-owning bridge or link files
- **AND** any E07_04-style image-transfer glue SHALL follow the same owning-bridge confinement rule
- **AND** `new/code/legacy/*`, `new/code/runtime/*`, and `new/code/port/*` SHALL continue to depend only on project-owned types and interfaces

### Requirement: Dual-Wheel Control Consumes Normalized Logical Wheel Data
The dual-wheel controller SHALL continue to consume normalized logical left/right encoder semantics and SHALL continue to produce logical left/right actuator commands without embedding vendor mapping or device-path assumptions in controller code.

#### Scenario: Wheel controller does not own vendor polarity or side mapping
- **WHEN** reviewers inspect the wheel-target and wheel-PID implementation
- **THEN** they SHALL find that encoder sign normalization and motor-side mapping remain owned by platform adapters or bridges
- **AND** controller code SHALL operate on project-owned logical left/right values rather than raw vendor ordering, raw device-node paths, or ad hoc sign fixes
