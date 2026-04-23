# runtime-speed-tuning-surface Specification

## Purpose
Define the project-owned runtime speed-tuning command, feedback, and telemetry contract used by the accepted bidirectional assistant tuning workflow.

## Requirements
### Requirement: Runtime Speed Tuning Uses A Project-Owned Command Contract
The runtime SHALL expose a project-owned bidirectional tuning command contract above the assistant TCP transport rather than letting raw inbound socket payloads mutate runtime state directly. The accepted first-release command set SHALL include `start`, `stop`, tuning-mode enable/disable, turn-suppression control, and target-speed override.

#### Scenario: Supported commands are decoded through a project-owned contract
- **WHEN** the board receives inbound tuning traffic on the accepted assistant TCP connection
- **THEN** the runtime SHALL decode that traffic through a project-owned command contract with typed command semantics
- **AND** the control path SHALL NOT consume raw socket buffers or vendor protocol payloads as its primary API

### Requirement: Inbound Commands Carry Correlation And Override-Expiry Fields
The accepted first-release tuning command contract SHALL define the fields needed for command/ACK correlation and deterministic target-speed override expiry. At minimum, accepted commands SHALL carry a project-owned `seq` identifier for request correlation, and target-speed override commands SHALL carry a TTL.

The accepted first-release contract SHALL NOT require standalone duplicate, replay, stale, reconnect-reset, or out-of-order rejection semantics beyond normal TCP delivery and project-owned parser safety. The runtime MAY retain the latest observed `seq` for diagnostics or session-local bookkeeping, but first-release host interoperability SHALL NOT depend on a frozen stale-command state machine.

#### Scenario: Expired overrides are cleared automatically
- **WHEN** the runtime holds an active target-speed override whose TTL has elapsed
- **THEN** it SHALL clear that override and resume using the startup-loaded baseline
- **AND** it SHALL publish a structured `state` frame identifying the clear event

### Requirement: First Release JSON-Line Session Contract Is Fixed And Enumerated
The accepted first-release host tuning surface SHALL use one newline-delimited JSON session contract on the accepted assistant TCP connection. That contract SHALL define the concrete frame families, required keys, field meanings, and unit rules for commands, ACK/state feedback, and structured telemetry.

The accepted first-release frame families SHALL be:

- `command`
- `ack`
- `state`
- `telemetry`

The accepted first-release command frames SHALL use these required fields:

- `type="command"`
- `cmd`
- `seq`

Additional required command fields SHALL be:

- `set_target_speed`: `value`, `ttl_ms`
- `set_turn_suppressed`: `value`
- `enable_tuning_mode`: no additional required payload fields
- `disable_tuning_mode`: no additional required payload fields
- `start`: no additional required payload fields
- `stop`: no additional required payload fields

The accepted first-release `set_target_speed.value` SHALL use the runtime's existing internal speed units and SHALL be valid only when all of the following are true:

- it is encoded as a JSON number
- it is finite
- it is greater than or equal to `0`
- it is less than or equal to the startup-loaded `Speed_base`

If `set_target_speed.value` is invalid (missing, non-numeric, non-finite, negative, or greater than the startup-loaded `Speed_base`), the runtime SHALL reject the command with `outcome="rejected"` and a human-readable `reason` string describing the violation, and it SHALL NOT alter the active tuning snapshot, active override value, or override expiry.

The accepted first-release ACK feedback SHALL identify at minimum:

- the originating `seq`
- whether the command was `accepted` or `rejected`
- a human-readable `reason` string when `outcome="rejected"`

The accepted first-release `state` frames SHALL use these required fields:

- `type="state"`
- `event`
- `reason`
- `tuning_mode_enabled`
- `turn_suppressed`
- `target_speed_override_enabled`
- `target_speed_override_value`
- `effective_speed_target`

The accepted first-release `state.event` family SHALL include at minimum:

- `input_rejected`: malformed JSON, unsupported command name, or other inbound payload outside the accepted decoded command surface was rejected before normal ACK correlation could apply
- `override_cleared`: TTL expiry cleared the active target-speed override while tuning mode may remain enabled
- `snapshot_cleared`: disconnect or explicit `disable_tuning_mode` cleared the full volatile tuning snapshot

`reason` SHALL be a human-readable string identifying the event cause such as `"malformed json"`, `"unsupported command"`, `"override TTL expired"`, `"disconnect"`, or `"tuning disabled by command"`.

`target_speed_override_value` SHALL be encoded as:

- the active override value as a JSON number when `target_speed_override_enabled=true`
- `null` when `target_speed_override_enabled=false`

`effective_speed_target` SHALL be encoded as:

- the current lifecycle-owned effective speed target in `SPINUP`, `RUNNING`, or `STOPPING`
- `0` in `DISARMED`, `START_REQUESTED`, or `FAIL_SAFE_LATCHED`
- the post-clear value under the same phase rule immediately after `override_cleared` or `snapshot_cleared`

The accepted first-release telemetry frames SHALL identify at minimum:

- `type="telemetry"`
- `motion_phase`
- `tuning_mode_enabled`
- `turn_suppressed`
- `target_speed_override_enabled`
- `target_speed_override_value`
- `effective_speed_target`
- `left_speed_target`
- `right_speed_target`
- `left_measured_speed`
- `right_measured_speed`
- `left_pwm_command`
- `right_pwm_command`
- `raw_turn_output`
- `applied_turn_output`

For first-release telemetry, `target_speed_override_value` and `effective_speed_target` SHALL use the same encoding rules defined above for `state` frames.

The accepted first-release unit rules SHALL be:

- target and measured speed values use the runtime's existing internal speed units rather than introducing a new physical-unit contract
- `ttl_ms` uses milliseconds
- booleans are encoded as JSON booleans

Optional assistant-compatible waveform or image publication MAY coexist as support evidence, but it SHALL NOT redefine, replace, or fragment the accepted newline-delimited JSON tuning contract.

#### Scenario: First release host tooling can implement one unambiguous wire contract
- **WHEN** implementers build the board-side command handler and the accepted host tuning tool
- **THEN** they SHALL be able to interoperate using the enumerated newline-delimited JSON command, ACK/state, and telemetry frame families and required fields
- **AND** they SHALL NOT need to choose a second framing model, alternate side channel, or undocumented field set for the accepted first release

#### Scenario: Malformed or unsupported inbound payloads use one frozen rejection path
- **WHEN** inbound bytes cannot be decoded into an accepted command frame with normal ACK correlation
- **THEN** the runtime SHALL emit a standalone `state` frame with `event="input_rejected"` and a human-readable `reason`
- **AND** it SHALL NOT silently ignore that payload or invent a second rejection transport for first-release interoperability

### Requirement: Runtime Tuning State Is Volatile And Isolated From Static Params
Live target-speed override, tuning-mode state, optional session-local request-correlation bookkeeping, and turn-suppression flags SHALL live in a runtime-owned volatile state boundary that is distinct from startup-loaded JSON parameters.

#### Scenario: Tuning commands do not rewrite persisted params
- **WHEN** a host issues target-speed overrides or tuning-mode commands during a run
- **THEN** the runtime SHALL apply them through a volatile runtime-owned tuning state
- **AND** it SHALL NOT rewrite `default_params.json`, `RuntimeParameters`, or persisted PID values as part of that live command path

### Requirement: Target-Speed Override Must Expire Cleanly
The runtime SHALL treat target-speed override as TTL-bound operator intent. The accepted baseline SHALL clear the override when its TTL expires, when the assistant session disconnects, or when tuning mode is explicitly disabled.

#### Scenario: Override expiry returns the runtime to the static baseline
- **WHEN** the active target-speed override expires or the command session disconnects
- **THEN** the runtime SHALL clear the override and resume using the startup-loaded baseline running-speed target
- **AND** it SHALL emit diagnosable state-change evidence for the clear event

### Requirement: Disconnect And Tuning-Mode Exit Clear The Full Runtime Tuning Snapshot
The runtime SHALL treat tuning mode, turn-suppression state, and target-speed override as one volatile runtime tuning snapshot. The accepted baseline SHALL clear that full snapshot when the command session disconnects or when tuning mode is explicitly disabled.

#### Scenario: Session loss clears tuning-only behavior
- **WHEN** the assistant session disconnects during or after a tuning run
- **THEN** the runtime SHALL clear target-speed override, tuning-mode enablement, and turn-suppression state together
- **AND** it SHALL emit project-owned clear-event evidence so the next normal run cannot silently inherit tuning-only behavior

#### Scenario: Tuning-mode disable returns the runtime to the normal profile
- **WHEN** tuning mode is explicitly disabled by command after the accepted tuning run has been stopped or while the runtime is otherwise non-driving
- **THEN** the runtime SHALL clear target-speed override, tuning-mode state, and turn-suppression state before the next normal run
- **AND** it SHALL publish structured state feedback identifying that clear action

#### Scenario: Stop alone does not clear the tuning snapshot
- **WHEN** the host sends `stop` during an accepted tuning run
- **THEN** the lifecycle SHALL return toward `DISARMED` using the normal stop contract without clearing tuning mode or override state by itself
- **AND** the full tuning snapshot SHALL remain present until a later `disable_tuning_mode` command or disconnect triggers `snapshot_cleared`

### Requirement: Tuning Mode Is Explicit And Scoped
The accepted baseline SHALL define an explicit tuning mode for straight-speed tuning rather than inferring tuning behavior from ordinary runtime state. Tuning-only behavior SHALL remain inactive unless that mode is explicitly enabled.

#### Scenario: Normal runs remain unaffected when tuning mode is off
- **WHEN** the runtime is operating with tuning mode disabled
- **THEN** target-speed override and tuning-only turn suppression SHALL be inactive
- **AND** normal lifecycle, turn computation, and wheel-target generation SHALL follow the existing non-tuning path

### Requirement: Tuning-Only Commands Have Explicit Disabled-Mode Semantics
The accepted first-release runtime SHALL define explicit command semantics for tuning-only commands while tuning mode is disabled. At minimum, `set_target_speed` and `set_turn_suppressed` SHALL NOT silently activate tuning behavior when tuning mode is off.

#### Scenario: Target-speed override is rejected while tuning mode is off
- **WHEN** the host sends `set_target_speed` while `tuning_mode_enabled=false`
- **THEN** the runtime SHALL reject that command without changing the active speed target or tuning snapshot
- **AND** it SHALL emit structured ACK or state feedback identifying that tuning mode was disabled

#### Scenario: Invalid target-speed override is rejected without side effects
- **WHEN** the host sends `set_target_speed` with an invalid `value`
- **THEN** the runtime SHALL reject that command with `outcome="rejected"` and a reason string describing the violation
- **AND** it SHALL leave the active tuning snapshot, active override value, and override TTL unchanged

#### Scenario: Turn suppression is rejected while tuning mode is off
- **WHEN** the host sends `set_turn_suppressed` while `tuning_mode_enabled=false`
- **THEN** the runtime SHALL reject that command without changing the applied turn behavior
- **AND** it SHALL emit structured ACK or state feedback identifying that tuning mode was disabled

### Requirement: Host Workflow Supports Minimal Live Plotting And CSV Capture
The accepted baseline SHALL include a project-owned host tuning workflow that can connect to the runtime, send supported commands, plot left/right target-versus-measured speed curves with a minimal visualization, and save CSV evidence for repeated PID tuning runs.

#### Scenario: Host tool installs plotting support when absent
- **WHEN** the accepted host tuning tool starts on a machine without the required plotting package
- **THEN** it SHALL attempt to install the required plotting dependency before enabling live plotting
- **AND** the workflow SHALL still preserve project-owned CSV capture even if plotting remains unavailable

#### Scenario: End-to-end tuning workflow exercises explicit tuning-mode boundaries
- **WHEN** the accepted host tuning workflow performs an end-to-end tuning run
- **THEN** that required workflow SHALL include explicit tuning-mode enablement before `set_target_speed`
- **AND** it SHALL include `stop` followed by explicit `disable_tuning_mode` after the run completes, with no alternate first-release return-to-normal shortcut

The accepted baseline SHALL provide structured ACK or state feedback suitable for project-owned host tooling so the host can tell whether commands were accepted or rejected.

#### Scenario: Host can distinguish accepted and rejected commands
- **WHEN** the host issues a tuning or motion command
- **THEN** the runtime SHALL publish project-owned ACK feedback that identifies whether the command was accepted or rejected, with a reason string on rejection
- **AND** the host workflow SHALL NOT rely solely on inferred behavior from wheel-speed curves to determine command acceptance

#### Scenario: Host can parse standalone state transitions without telemetry inference
- **WHEN** the runtime emits a standalone `state` frame for `input_rejected`, `override_cleared`, or `snapshot_cleared`
- **THEN** the host SHALL be able to identify the event type, reason string, and current tuning snapshot fields from that state frame alone
- **AND** the accepted workflow SHALL NOT require the host to infer those state transitions only by polling telemetry

#### Scenario: Host can parse non-driving field values without guessing
- **WHEN** the runtime publishes `state` or `telemetry` while no override is active or the lifecycle is not actively driving
- **THEN** `target_speed_override_value` SHALL use `null` when no override is active
- **AND** `effective_speed_target` SHALL follow the frozen phase-based encoding rather than leaving host behavior to implementation-time choice

### Requirement: First Release Host Interoperability Uses One Fixed JSON-Line Session Contract
The accepted first-release host tuning workflow SHALL use a single project-owned newline-delimited JSON session contract on the accepted assistant TCP connection for commands, structured ACK/state feedback, and structured telemetry. Assistant-compatible waveform or image publication MAY remain optional support evidence, but the host tuning workflow SHALL NOT depend on a second unresolved command or telemetry topology.

#### Scenario: Host tuning interop is frozen for the first release
- **WHEN** a host tuning session is established against the accepted runtime
- **THEN** the runtime and host tool SHALL interoperate through one fixed newline-delimited JSON command/feedback/telemetry contract on the accepted TCP session
- **AND** the accepted first-release workflow SHALL NOT leave command, ACK, or structured telemetry framing as an implementation-time choice
