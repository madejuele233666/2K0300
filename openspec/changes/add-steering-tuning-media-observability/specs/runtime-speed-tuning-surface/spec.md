## MODIFIED Requirements

### Requirement: Host Workflow Supports Minimal Live Plotting And CSV Capture
The accepted baseline SHALL continue to use `new/user/debug.sh tuning ...` as the project-owned host tuning workflow. For steering tuning, that same accepted workflow MAY optionally listen on a separate steering media port, record image frames plus metadata, and align those artifacts with control-plane CSV and command logs.

#### Scenario: The accepted host entrypoint remains `debug.sh tuning`
- **WHEN** reviewers inspect the first-release steering tuning workflow
- **THEN** they SHALL find `new/user/debug.sh tuning ...` as the accepted host entrypoint for command/control plus optional steering media capture
- **AND** they SHALL NOT find a separate ad hoc socket script treated as the primary accepted steering tuning workflow

#### Scenario: Host evidence can align control CSV and steering media artifacts
- **WHEN** the accepted host workflow runs with steering media enabled
- **THEN** it SHALL be able to save control CSV, media metadata, and raw image captures with timestamps that support one aligned evidence bundle
- **AND** control CSV capture SHALL remain available even when media capture or live plotting is disabled

### Requirement: First Release Host Interoperability Uses One Fixed JSON-Line Session Contract
The accepted first-release host tuning workflow SHALL continue to use a single project-owned newline-delimited JSON assistant control session for commands, structured ACK/state feedback, and structured telemetry. Enabling steering media SHALL not alter the accepted control schema or require a second command-bearing contract.

#### Scenario: Steering media enablement does not change the accepted control command set
- **WHEN** the host enables steering media capture during a tuning session
- **THEN** the runtime and host SHALL still interoperate for commands, ACK/state feedback, and structured telemetry through the frozen assistant JSON-line control session
- **AND** steering media capture SHALL remain a separate read-only observability path rather than a second command surface

## ADDED Requirements

### Requirement: Steering Observability Remains Read-Only In The First Release
The accepted steering tuning workflow for this change SHALL expose steering-related parameter and controller context as read-only observability only. It SHALL NOT introduce host commands that mutate steering PID values, overwrite startup-loaded parameters, or write parameter files during a run.

#### Scenario: Steering parameter context is observed but not mutated
- **WHEN** the host workflow captures steering tuning evidence for a run
- **THEN** it MAY record the current startup-loaded steering parameter snapshot as part of steering media observability
- **AND** the accepted first-release command set SHALL NOT include any steering-parameter write or override command
