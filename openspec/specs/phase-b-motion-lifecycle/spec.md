# phase-b-motion-lifecycle Specification

## Purpose
Define the project-owned Phase B motion lifecycle, re-arm rules, and bounded fault-injection/runtime-entry semantics for the accepted `new/` runtime.

## Requirements
### Requirement: Motion Lifecycle Is Project-Owned And Explicit
The Phase B runtime SHALL define a project-owned motion lifecycle that is evaluated separately from process startup/shutdown and separately from the safety gate. The lifecycle MUST at minimum expose `DISARMED`, `START_REQUESTED`, `SPINUP`, `RUNNING`, `STOPPING`, and `FAIL_SAFE_LATCHED` phases. Accepted operator intent for this lifecycle MAY come from local product entrypoints or from the accepted project-owned remote command surface, but all such intent SHALL still pass through the same motion-intent boundary.

#### Scenario: Clean startup does not imply immediate drive permission
- **WHEN** startup reaches `startup.complete` and the control gate is clear
- **THEN** the runtime SHALL still require an explicit motion-lifecycle transition before it applies drive output
- **AND** reviewers SHALL be able to identify the published current motion phase without inferring it from raw motor commands

#### Scenario: Operator intent is separated from process exit
- **WHEN** the runtime receives a start or stop request
- **THEN** it SHALL publish that request through a project-owned motion-intent boundary
- **AND** it SHALL NOT treat process lifetime alone as the motion-lifecycle contract

#### Scenario: Remote operator intent does not bypass lifecycle ownership
- **WHEN** a host issues an accepted remote `start` or `stop` command during runtime
- **THEN** the runtime SHALL translate that request into the project-owned motion-intent boundary
- **AND** reviewers SHALL NOT find the remote path writing lifecycle phase or actuator output directly

### Requirement: Hold-Disarmed And Fail-Safe Are Distinct Runtime Outcomes
The Phase B runtime SHALL distinguish "gate is clear but motion is intentionally held disarmed" from "gate or fault state requires fail-safe actuator suppression". The system MUST preserve `control.veto.*` for true safety blockers and MUST publish a separate apply/diagnostic outcome for hold-disarmed states.

#### Scenario: Not-yet-started vehicle does not masquerade as a fail-safe fault
- **WHEN** the safety gate is clear but the motion phase is `DISARMED` or `START_REQUESTED`
- **THEN** the runtime SHALL suppress actuator output through a hold-disarmed path
- **AND** it SHALL NOT report that state as the dominant `control.veto.*` reason

#### Scenario: Runtime fault still forces fail-safe suppression
- **WHEN** the vehicle is in `SPINUP`, `RUNNING`, or `STOPPING` and the safety gate becomes blocked by low voltage, stale perception, invalid perception health, reference-control-not-ready, invalid IMU, or invalid encoder
- **THEN** the runtime SHALL force actuator output back to fail-safe
- **AND** it SHALL publish the corresponding `control.veto.*` reason and motion-fault state

### Requirement: Phase-Aware Start And Stop Shaping Are Mandatory
The Phase B runtime SHALL shape startup and stop behavior through lifecycle-aware runtime policy rather than step-jumping directly from zero to unconstrained control output. The policy MUST support ramped effective speed target, limited turn authority during spinup, and bounded command deltas before motor application.

#### Scenario: Spinup is shaped before full running authority
- **WHEN** the motion lifecycle transitions from `START_REQUESTED` to `SPINUP`
- **THEN** the runtime SHALL ramp the effective speed target toward the configured running target
- **AND** it SHALL limit turn authority and/or command deltas until spinup completes

#### Scenario: Controlled stop converges back to disarmed
- **WHEN** the runtime receives a stop request during `RUNNING` or `SPINUP`
- **THEN** it SHALL enter `STOPPING`
- **AND** it SHALL reduce output toward zero under explicit stop policy before returning to `DISARMED`

### Requirement: Transition Guards And Parameter Semantics Are Fixed
The Phase B motion lifecycle SHALL define transition guards for motion start, motion stop completion, and post-fault re-arm as normative contract behavior rather than leaving them to implementation-local interpretation.

The lifecycle parameters SHALL mean:

- `motion_unveto_confirm_cycles`: consecutive clear-gate control cycles required before `START_REQUESTED` may enter `SPINUP`
- `motion_spinup_ms`: elapsed spinup time before `SPINUP` may enter `RUNNING`
- `motion_turn_limit_spinup`: maximum spinup-phase turn authority before full running authority is released
- `motion_pwm_step_limit`: maximum absolute left/right PWM change allowed per control cycle while lifecycle-owned command shaping is active
- `motion_stop_ms`: minimum time the runtime remains in shaped stop behavior before `STOPPING` completion may be declared
- `motion_stop_encoder_threshold`: maximum absolute mean encoder magnitude allowed when declaring `STOPPING` complete
- `motion_fault_rearm_hold_ms`: minimum hold time after entering `FAIL_SAFE_LATCHED` before re-arm may be considered

#### Scenario: Start stays blocked until clean gate confirmation is satisfied
- **WHEN** the runtime is in `START_REQUESTED`
- **THEN** it SHALL remain there until `motion_intent.start_requested=true` and the safety gate stays clear for `motion_unveto_confirm_cycles` consecutive control cycles
- **AND** if the safety gate blocks before that streak completes, the runtime SHALL remain in `START_REQUESTED` and emit blocked-start evidence instead of entering `SPINUP`

#### Scenario: Spinup completes only after the shaped-start window finishes
- **WHEN** the runtime is in `SPINUP`
- **THEN** it SHALL remain in `SPINUP` until at least `motion_spinup_ms` has elapsed
- **AND** it SHALL enforce `motion_turn_limit_spinup` and `motion_pwm_step_limit` as the spinup-phase shaping bounds during that window
- **AND** it SHALL enter `RUNNING` only after the spinup timer completes and the runtime is allowed to release spinup-specific shaping limits in favor of running authority

#### Scenario: Stop completion requires both shaped output quiescence and settled encoder evidence
- **WHEN** the runtime is in `STOPPING`
- **THEN** it SHALL return to `DISARMED` only after at least `motion_stop_ms` has elapsed, the shaped drive command has returned to zero, and the absolute mean encoder magnitude is at or below `motion_stop_encoder_threshold`

#### Scenario: Baseline re-arm after fault requires explicit reset intent
- **WHEN** the runtime is in `FAIL_SAFE_LATCHED` after a safety fault
- **THEN** it SHALL remain latched until the safety gate returns clear, `motion_fault_rearm_hold_ms` has elapsed, and `reset_fault_requested=true`
- **AND** the default product lifecycle SHALL source `reset_fault_requested` from a product-owned operator reset request on the accepted runtime entrypoint, with the Phase B baseline binding owned by `new/user/main.cpp`
- **AND** the default product lifecycle SHALL NOT silently re-arm itself without that reset intent

### Requirement: Restart Requires Explicit Reset Boundaries
The Phase B runtime SHALL reset controller carry-over state on lifecycle transitions where stale integral, smoothing, or attitude state would pollute the next motion attempt. At minimum, PID integrators, last-error smoothing state, and project-owned turn target carry-over MUST be reset on re-entry boundaries documented by the change.

#### Scenario: Post-stop restart begins from clean controller state
- **WHEN** a completed stop returns the motion lifecycle to `DISARMED`
- **THEN** the next start sequence SHALL begin with reset controller carry-over state
- **AND** the first spinup cycle SHALL NOT reuse stale integral or attitude accumulation from the previous run

#### Scenario: Post-fault restart does not inherit stale control memory
- **WHEN** a runtime fault drives the lifecycle into `FAIL_SAFE_LATCHED`
- **THEN** a later re-arm SHALL require the documented reset boundary before the runtime may drive again
- **AND** verification evidence SHALL be able to tie that reset to the restart path

### Requirement: Fail-Safe Latch And Re-Arm Rules Are Reviewable
Phase B fail-safe behavior SHALL not end at transient veto suppression alone. The lifecycle MUST define when a runtime fault becomes latched, what conditions are required before re-arm is allowed, and whether test-only automation is participating in that re-arm path.

#### Scenario: Faulted run stays latched until re-arm conditions are met
- **WHEN** a runtime fault interrupts `SPINUP`, `RUNNING`, or `STOPPING`
- **THEN** the lifecycle SHALL enter `FAIL_SAFE_LATCHED`
- **AND** it SHALL remain there until the documented re-arm conditions are satisfied

#### Scenario: Test-only auto-reset remains distinguishable
- **WHEN** a verification run enables automatic post-fault reset
- **THEN** the logs and runtime contract SHALL identify that automation as test harness behavior
- **AND** the automation SHALL be allowed to synthesize reset intent only after the baseline latch-clear and hold-time conditions are satisfied
- **AND** the default product lifecycle SHALL still define a non-automatic baseline re-arm path

#### Scenario: Phase B baseline re-arm is invoked explicitly and does not exit the process
- **WHEN** the product runtime is latched in `FAIL_SAFE_LATCHED` and an operator requests a baseline reset
- **THEN** the accepted Phase B baseline SHALL treat that request as a dedicated re-arm trigger handled by `new/user/main.cpp` rather than as stop/exit automation
- **AND** the baseline binding for that trigger SHALL be `SIGUSR1` until a richer project-owned operator interface replaces it

### Requirement: Fault Injection Uses The Product Runtime Entry Point
Phase B verification SHALL exercise drop-frame, IMU-invalid, encoder-invalid, and low-voltage fault paths through bounded hooks on the existing runtime entrypoint rather than through a separate product binary or alternate runtime flow.

#### Scenario: Injected faults still use the main runtime boundary
- **WHEN** a verifier enables bounded Phase B fault injection
- **THEN** the runtime SHALL continue using the normal startup, control-loop, perception, and shutdown flow
- **AND** the injected fault SHALL surface through the same project-owned diagnostics and lifecycle markers used by real failures

#### Scenario: Unsupported or absent fault hooks do not redefine the runtime
- **WHEN** no fault-injection env is set, or an unsupported env value is supplied
- **THEN** the runtime SHALL stay on the normal production path
- **AND** the verification helper SHALL not create a second long-lived runtime contract

### Requirement: Remote Start And Stop Respect Existing Phase Guards
Accepted remote `start` and `stop` commands SHALL respect the same lifecycle transition guards already used by the local product runtime and harness paths.

#### Scenario: Remote start still waits for gate-clear confirmation
- **WHEN** a host requests `start` while the runtime is `DISARMED` or `START_REQUESTED`
- **THEN** the runtime SHALL continue to enforce the documented gate-clear and spinup rules before entering `RUNNING`
- **AND** a remote start request SHALL NOT grant unconstrained drive output by itself

#### Scenario: Remote stop still flows through shaped stopping
- **WHEN** a host requests `stop` during `SPINUP` or `RUNNING`
- **THEN** the runtime SHALL enter `STOPPING` and follow the accepted stop-shaping rules before returning to `DISARMED`
- **AND** the remote stop path SHALL NOT replace the lifecycle-owned stop completion contract with an immediate hard-cut shortcut

#### Scenario: Latched fail-safe blocks remote lifecycle commands
- **WHEN** a host sends remote `start` or `stop` while the runtime is `FAIL_SAFE_LATCHED`
- **THEN** the runtime SHALL reject that command without clearing the latched fault or changing lifecycle phase
- **AND** the host SHALL receive project-owned rejection evidence stating that the fault latch remains active and the documented reset boundary is still authoritative

### Requirement: Tuning Mode Does Not Create A Parallel Lifecycle
The accepted tuning mode SHALL affect only the documented tuning-specific control profile and runtime override behavior. It SHALL NOT define a second motion lifecycle or new actuator-arming contract.

#### Scenario: Tuning mode reuses the normal lifecycle
- **WHEN** tuning mode is enabled for a speed-tuning run
- **THEN** the runtime SHALL continue to publish and honor the same Phase B lifecycle phases and fail-safe behavior
- **AND** tuning mode SHALL remain a scoped control-profile modifier rather than a parallel lifecycle owner
