## ADDED Requirements

### Requirement: Differential Steering Contract Is Explicit
The project-owned actuator contract SHALL expose only differential-drive outputs for the active three-wheel platform and SHALL NOT publish servo-specific steering fields when no independent steering servo exists.

#### Scenario: Public actuator contract matches the real platform
- **WHEN** reviewers inspect `new/code/port/*`, `new/code/legacy/*`, and `new/code/runtime/*`
- **THEN** they SHALL find that the published actuator command surface expresses left/right wheel output plus fail-safe stop semantics only, and they SHALL NOT find a public `servo_pwm` steering path preserved as a compatibility field

### Requirement: Control Gate Observability Is Project-Owned
The runtime SHALL normalize control-veto, actuator-arming, and actuator-application outcomes into project-owned observability data before publishing diagnostics or verification evidence.

#### Scenario: Dominant veto cause is distinguishable
- **WHEN** the control path suppresses actuator output because perception is stale, perception requests emergency veto, low voltage is active, IMU data is invalid, or encoder data is invalid
- **THEN** the runtime SHALL emit project-owned diagnostics or state that identify the dominant veto cause without exposing vendor bridge internals or raw device-node details

#### Scenario: Apply outcome is visible without vendor leakage
- **WHEN** the runtime requests a non-empty actuator command and the motor path either applies or rejects it
- **THEN** the runtime SHALL record whether the command was requested, whether it was applied, and whether actuator arming remained true, while keeping vendor-specific write-path details inside platform-owned diagnostics

## MODIFIED Requirements

### Requirement: Public Port Contracts Stay Project-Owned
Reused legacy logic from `old/code/` SHALL continue to depend only on project-owned port contracts and runtime state, not on true-baseline vendor headers, free functions, globals, raw device-node paths, or servo-shaped compatibility fields that misrepresent the active platform topology.

#### Scenario: Vendor leakage and false actuator topology are blocked
- **WHEN** `new/code/port/*.hpp`, `new/code/legacy/*`, or `new/code/runtime/*` are reviewed
- **THEN** they SHALL NOT include `zf_common_headfile.h`, vendor `.h` driver/device headers, vendor global variables, direct vendor free-function calls, OpenCV concrete types, raw `/dev/zf_*` paths, or public servo-output fields that imply a steering path the active vehicle does not have

### Requirement: Fail-Safe Runtime Behavior Survives The Retarget
The retarget SHALL continue to veto or clamp actuator output when perception or control-critical sensor state is invalid, and it SHALL make the project-owned reason for the in-scope control-gating decision reviewable.

#### Scenario: Invalid normalized state is both suppressed and explained
- **WHEN** perception is stale, perception requests emergency veto, low voltage is active, IMU normalization fails, encoder delta state is not yet trustworthy, or a non-empty actuator command is rejected by the motor apply path
- **THEN** the control path SHALL suppress actuator updates or force actuator arming back to fail-safe, and it SHALL record the corresponding project-owned veto or apply outcome instead of continuing with stale or partially normalized control state
