## ADDED Requirements

### Requirement: Motion Lifecycle Boundary Stays Above Vendor Adapters
The Phase B start/stop/fail-safe lifecycle SHALL remain a project-owned runtime contract and SHALL NOT be delegated to vendor-facing adapters, raw bridge state, or preserved `old_2` hardware UI assumptions.

#### Scenario: Motion semantics stay in runtime-owned modules
- **WHEN** reviewers inspect the Phase B implementation
- **THEN** they SHALL find motion phases, motion intent, and lifecycle transitions in project-owned runtime code
- **AND** they SHALL NOT find vendor adapters or bridges acting as the primary owner of Phase B start/stop state

## MODIFIED Requirements

### Requirement: Fail-Safe Runtime Behavior Survives The Retarget
The retarget SHALL continue to veto or clamp actuator output when perception or control-critical sensor state is invalid, and it SHALL make the project-owned reason for the in-scope control-gating decision reviewable.

#### Scenario: Invalid normalized state is both suppressed and explained
- **WHEN** perception is stale, perception requests emergency veto, low voltage is active, IMU normalization fails, encoder delta state is not yet trustworthy, or a non-empty actuator command is rejected by the motor apply path
- **THEN** the control path SHALL suppress actuator updates or force actuator arming back to fail-safe
- **AND** it SHALL record the corresponding project-owned veto or apply outcome instead of continuing with stale or partially normalized control state
- **AND** it SHALL distinguish true fail-safe suppression from Phase B hold-disarmed lifecycle suppression so reviewers can tell whether the blocker is safety- or lifecycle-owned

### Requirement: Control Gate Observability Is Project-Owned
The runtime SHALL normalize control-veto, actuator-arming, and actuator-application outcomes into project-owned observability data before publishing diagnostics or verification evidence.

#### Scenario: Dominant veto cause is distinguishable
- **WHEN** the control path suppresses actuator output because perception is stale, perception requests emergency veto, low voltage is active, IMU data is invalid, or encoder data is invalid
- **THEN** the runtime SHALL emit project-owned diagnostics or state that identify the dominant veto cause without exposing vendor bridge internals or raw device-node details

#### Scenario: Hold-disarmed is distinguishable from fail-safe
- **WHEN** the safety gate is clear but the Phase B motion lifecycle has not yet allowed drive output
- **THEN** the runtime SHALL publish a project-owned hold-disarmed outcome that is distinguishable from `control.veto.*`
- **AND** verification evidence SHALL be able to tell whether the car was idle by policy or blocked by fault

#### Scenario: Apply outcome is visible without vendor leakage
- **WHEN** the runtime requests a non-empty actuator command and the motor path either applies or rejects it
- **THEN** the runtime SHALL record whether the command was requested, whether it was applied, whether actuator arming remained true, and whether the lifecycle was running, stopping, or held disarmed
- **AND** vendor-specific write-path details SHALL remain inside platform-owned diagnostics
