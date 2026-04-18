## ADDED Requirements

### Requirement: Public Port Contracts Stay Project-Owned
Reused legacy logic from `old/code/` SHALL continue to depend only on project-owned port contracts and runtime state, not on true-baseline vendor headers, free functions, globals, or raw device-node paths.

#### Scenario: Vendor leakage is blocked
- **WHEN** `new/code/port/*.hpp`, `new/code/legacy/*`, or `new/code/runtime/*` are reviewed
- **THEN** they SHALL NOT include `zf_common_headfile.h`, vendor `.h` driver/device headers, vendor global variables, direct vendor free-function calls, OpenCV concrete types, or raw `/dev/zf_*` paths in their public signatures or legacy-facing code

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

#### Scenario: Startup-critical exposure requirement cannot be hidden
- **WHEN** startup applies parameters and the active hardware profile expects `exp_light` to affect camera exposure
- **THEN** the runtime SHALL either route camera setup through a named adaptation hook that honors the requirement or keep startup fail-closed/diagnostics-only with the unsupported direct path explicitly logged

### Requirement: True-Library Stack Equivalents Are Named Per Critical Device
Each critical device contract from the TC264-side port SHALL map to an explicit true-baseline construct, bridge file, or documented non-support decision.

#### Scenario: Critical mappings are reviewable
- **WHEN** design and implementation are reviewed
- **THEN** camera, IMU, encoder, timer, PWM/GPIO motor control, ADC-based low-voltage checks, diagnostics, and parameter persistence SHALL each have a named true-baseline-side construct or implementation file, and unsupported contracts SHALL name the adaptation boundary instead of being silently deferred

### Requirement: Fail-Safe Runtime Behavior Survives The Retarget
The retarget SHALL continue to veto or clamp actuator output when perception, sensor, parameter, or baseline-normalization state is invalid.

#### Scenario: Invalid normalized state suppresses actuation
- **WHEN** camera snapshots are empty, IMU normalization fails, encoder delta state is not yet trustworthy, or parameter application violates a startup-critical requirement
- **THEN** the control path SHALL suppress actuator updates and record the reason through diagnostics instead of continuing with stale, vendor-global, or partially normalized data
