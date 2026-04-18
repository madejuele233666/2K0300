# tc264-to-true-ls2k0300-adapter-layer Specification

## Purpose
Define the accepted adapter-layer contracts and fail-safe runtime behavior that map preserved TC264-era logic onto the real LS2K0300 vendor surface without leaking vendor APIs outside the platform bridge slice.
## Requirements
### Requirement: Public Port Contracts Stay Project-Owned
Reused legacy logic from `old/code/` SHALL continue to depend only on project-owned port contracts and runtime state, not on true-baseline vendor headers, free functions, globals, raw device-node paths, or servo-shaped compatibility fields that misrepresent the active platform topology.

#### Scenario: Vendor leakage and false actuator topology are blocked
- **WHEN** `new/code/port/*.hpp`, `new/code/legacy/*`, or `new/code/runtime/*` are reviewed
- **THEN** they SHALL NOT include `zf_common_headfile.h`, vendor `.h` driver/device headers, vendor global variables, direct vendor free-function calls, OpenCV concrete types, raw `/dev/zf_*` paths, or public servo-output fields that imply a steering path the active vehicle does not have

### Requirement: Differential Steering Contract Is Explicit
The project-owned actuator contract SHALL expose only differential-drive outputs for the active three-wheel platform and SHALL NOT publish servo-specific steering fields when no independent steering servo exists.

#### Scenario: Public actuator contract matches the real platform
- **WHEN** reviewers inspect `new/code/port/*`, `new/code/legacy/*`, and `new/code/runtime/*`
- **THEN** they SHALL find that the published actuator command surface expresses left/right wheel output plus fail-safe stop semantics only, and they SHALL NOT find a public `servo_pwm` steering path preserved as a compatibility field

### Requirement: True Vendor APIs Are Normalized Inside Platform-Owned Bridges
The true LS2K0300 vendor surface SHALL be normalized into project-owned snapshots before data crosses from platform code into the rest of the application.

#### Scenario: Camera and IMU globals are copied before publication
- **WHEN** platform code consumes `uvc_camera_init`, `wait_image_refresh`, `rgay_image`, `imu_type`, or device-specific IMU getter functions
- **THEN** it SHALL copy the resulting vendor-owned data into project-owned snapshot structs before publishing anything to `new/code/runtime/*` or `new/code/legacy/*`

### Requirement: Baseline-Specific Compatibility Shims Are Forbidden
The retarget SHALL not preserve wrong-baseline compatibility by using `.hpp` detection shims, stub classes, or silent fallback code.

#### Scenario: Wrong-baseline shim code is absent
- **WHEN** the platform adapter layer is reviewed
- **THEN** it SHALL NOT use `__has_include(...hpp)` checks, synthetic wrapper classes, or old-root fallback paths to pretend that the real target still exposes the superseded C++ wrapper API

### Requirement: Timer Lifecycle Ownership Is Explicit
The control-loop timer SHALL provide explicit stop and shutdown semantics even though the true vendor PIT public contract does not expose symmetric stop ownership.

#### Scenario: Timer shutdown remains fail-safe
- **WHEN** startup fails, the process exits, or runtime requests shutdown
- **THEN** the chosen timer path SHALL either expose explicit stop ownership through a project-owned bridge/executor or refuse to arm actuators, and it SHALL NOT claim successful direct-match lifecycle support without a documented stop path

### Requirement: Encoder Delta Semantics Are Declared, Not Assumed
The adapter layer SHALL define how phase-1 encoder deltas are derived on the true baseline rather than assuming the wrong-baseline `clear_count()` behavior still exists.

#### Scenario: First-sample and reset behavior are explicit
- **WHEN** the encoder adapter starts, encounters a device reset, or detects an implausible count jump
- **THEN** it SHALL treat the sample as a baseline-establishment or fault event, emit a diagnosable marker, and veto unsafe actuator updates until a valid delta baseline is restored

### Requirement: Unsupported Exposure Control Is Surfaced
If the active profile requires `exp_light` to control real camera exposure, the retarget SHALL surface whether the true vendor path supports that requirement instead of silently ignoring it.

#### Scenario: Exposure policy becomes an explicit Phase A decision
- **WHEN** direct-match camera behavior is reviewed as part of Phase A closure
- **THEN** the resulting code and evidence SHALL state whether exposure control is truly supported, unsupported, or routed through a named adaptation boundary, and they SHALL tie that statement to the observed perception freshness or emergency-veto behavior instead of leaving exposure policy as an open-ended future concern

### Requirement: True-Library Stack Equivalents Are Named Per Critical Device
Each critical device contract from the TC264-side port SHALL map to an explicit true-baseline construct, bridge file, or documented non-support decision, and each direct-match mapping SHALL name how the concrete board resource is resolved.

#### Scenario: Critical mappings are reviewable
- **WHEN** design and implementation are reviewed
- **THEN** camera, IMU, encoder, timer, PWM/GPIO motor control, ADC-based low-voltage checks, diagnostics, and parameter persistence SHALL each have a named true-baseline-side construct or implementation file, SHALL document whether the resource is fixed, enumerated, or override-driven, and unsupported contracts SHALL name the adaptation boundary instead of being silently deferred

### Requirement: Fail-Safe Runtime Behavior Survives The Retarget
The retarget SHALL continue to veto or clamp actuator output when perception or control-critical sensor state is invalid, and it SHALL make the project-owned reason for the in-scope control-gating decision reviewable.

#### Scenario: Invalid normalized state is both suppressed and explained
- **WHEN** perception is stale, perception requests emergency veto, low voltage is active, IMU normalization fails, encoder delta state is not yet trustworthy, or a non-empty actuator command is rejected by the motor apply path
- **THEN** the control path SHALL suppress actuator updates or force actuator arming back to fail-safe, SHALL record the corresponding project-owned veto or apply outcome instead of continuing with stale or partially normalized control state, and SHALL support later Phase A evidence that distinguishes transient startup suppression from sustained runtime blockers

### Requirement: Control Gate Observability Is Project-Owned
The runtime SHALL normalize control-veto, actuator-arming, and actuator-application outcomes into project-owned observability data before publishing diagnostics or verification evidence.

#### Scenario: Dominant veto cause is distinguishable
- **WHEN** the control path suppresses actuator output because perception is stale, perception requests emergency veto, low voltage is active, IMU data is invalid, or encoder data is invalid
- **THEN** the runtime SHALL emit project-owned diagnostics or state that identify the dominant veto cause without exposing vendor bridge internals or raw device-node details

#### Scenario: Apply outcome is visible without vendor leakage
- **WHEN** the runtime requests a non-empty actuator command and the motor path either applies or rejects it
- **THEN** the runtime SHALL record whether the command was requested, whether it was applied, and whether actuator arming remained true, while keeping vendor-specific write-path details inside platform-owned diagnostics

### Requirement: Direct-Match Hardware Discovery Is Truthful
Direct-match bridge and adapter code SHALL verify the concrete board resource it intends to use before it reports a subsystem as ready.

#### Scenario: IMU discovery resolves a real board node
- **WHEN** the direct-match IMU path initializes on a board where `/sys/bus/iio/devices/` numbering differs from previous assumptions
- **THEN** the platform-owned bridge SHALL enumerate or otherwise resolve the active IMU node from real board-visible evidence, and it SHALL NOT hard-code `iio:device1` as an unverified invariant

#### Scenario: Missing direct-match resource blocks readiness
- **WHEN** a direct-match device node or sysfs resource required by IMU, encoder, motor, or ADC logic cannot be resolved
- **THEN** the owning adapter SHALL report not-ready/fail-safe with diagnosable evidence naming the unresolved resource, rather than claiming successful initialization

### Requirement: Bridge I/O Failures Are Propagated
Platform-owned bridges SHALL surface read and write failures to adapters instead of fabricating valid samples, zero values, or successful actuator writes.

#### Scenario: Encoder read failure is not reported as valid data
- **WHEN** an encoder count read fails because the device node is missing, unreadable, or returns malformed data
- **THEN** the bridge SHALL return an invalid result, and the adapter/runtime SHALL preserve fail-safe veto behavior instead of publishing a valid delta sample

#### Scenario: Actuator write failure is not reported as success
- **WHEN** PWM or GPIO writes fail during motor initialization, command application, or shutdown
- **THEN** the motor path SHALL surface that failure through diagnostics and readiness/apply status so actuator state is not falsely reported as available

#### Scenario: ADC acquisition failure stays distinguishable from low voltage
- **WHEN** the battery ADC path cannot be read
- **THEN** the power-monitor path SHALL report the sample as unavailable rather than converting the failure into a synthetic raw value that looks like a real low-voltage reading

### Requirement: IMU Startup Proof And Sample-Quality Proof Stay Separate
The adapter layer and Phase A verification flow SHALL distinguish direct-match IMU startup readiness from the later proof that IMU samples are continuously valid, directionally correct, and usable for control.

#### Scenario: IMU can start without being declared fully closed
- **WHEN** reviewers inspect Phase A logs, code, or evidence after direct-match startup has already reached `imu.init`, `imu.detect`, and `startup.complete`
- **THEN** they SHALL treat that as startup-grade proof only, and later closure evidence SHALL explicitly address runtime `imu.valid` continuity, static-sample interpretability, and axis/direction correctness rather than collapsing all IMU questions into one status

### Requirement: Encoder Trust Must Be Closed Before Speed Feedback Is Declared Reliable
The adapter/runtime contract SHALL require Phase A evidence that encoder delta direction, baseline behavior, and magnitude interpretation are trustworthy before speed feedback is treated as a closed control input.

#### Scenario: Encoder delta semantics are tied to trust evidence
- **WHEN** reviewers inspect encoder closure work or control-loop evidence that relies on encoder-derived speed
- **THEN** they SHALL find explicit evidence for baseline establishment behavior, jump-reset interpretation, forward/reverse sign correctness, and a human-explainable delta-to-speed conclusion before the encoder path is described as control-trustworthy

### Requirement: Motor Closure Distinguishes Write Success From Physical Output Success
The motor path SHALL not treat successful PWM/GPIO writes as sufficient evidence that the actuator path is closed for Phase A.

#### Scenario: Physical-output proof is required for actuator closure
- **WHEN** reviewers inspect motor closure evidence or control-unlock claims that depend on the actuator path
- **THEN** they SHALL find evidence that non-zero commands produce real output in a safe test setup, that differential direction is interpretable, and that emergency-stop or write-failure handling returns the system to fail-safe rather than relying on software write success alone

