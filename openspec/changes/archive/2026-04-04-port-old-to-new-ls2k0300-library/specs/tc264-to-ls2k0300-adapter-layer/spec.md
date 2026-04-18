## ADDED Requirements

### Requirement: Adapter-Gated Legacy Logic
Reused logic from `old/code/` SHALL depend on adapter contracts or runtime state objects rather than direct TC264 peripheral APIs.

#### Scenario: Forbidden platform leakage
- **WHEN** a migrated legacy source file is reviewed
- **THEN** it SHALL NOT include TC264-only headers, interrupt macros, pin macros, `zf_common_headfile.hpp`, direct `zf_driver_*` / `zf_device_*` headers, or direct calls such as `pwm_set_duty`, `encoder_get_count`, `mt9v03x_*`, or `mpu6050_*`

#### Scenario: Inputs and outputs are explicit
- **WHEN** the control loop calls migrated logic
- **THEN** camera data, IMU data, encoder deltas, parameters, and actuator commands SHALL cross the boundary as named adapter inputs or outputs rather than hidden hardware globals

#### Scenario: Port contracts do not re-export platform or OpenCV types
- **WHEN** `new/code/port/*.hpp` is reviewed
- **THEN** it SHALL NOT include or expose `zf_common_headfile.hpp`, direct `zf_driver_*` / `zf_device_*` types, OpenCV headers, `cv::Mat`, `cv::VideoCapture`, or other LS2K0300 platform-owned concrete types through public fields, function signatures, or aliases

#### Scenario: Platform-facing types stop at the platform layer
- **WHEN** the adapter boundary is implemented
- **THEN** `new/code/platform/*.cpp` MAY translate between LS2K0300 or OpenCV types and project-owned port structs, but the public contract seen by `new/code/legacy/` and `new/code/runtime/` SHALL remain project-owned and hardware-agnostic

#### Scenario: Main acts only as a restricted composition root
- **WHEN** `new/user/main.cpp` is reviewed
- **THEN** it MAY instantiate concrete platform bootstrap or adapter owners as the application composition root, but it SHALL hand off to startup, runtime, and shutdown flows through project-owned contracts rather than propagating LS2K0300 or OpenCV concrete types beyond that entry boundary

### Requirement: LS2K0300 Stack Equivalents For Devices
The migration SHALL map each critical TC264 hardware dependency to an LS2K0300 stack equivalent or to an explicit adaptation hook in phase 1.

#### Scenario: Critical devices are mapped
- **WHEN** the design and implementation are reviewed
- **THEN** camera, IMU, encoder, timer, PWM/GPIO motor control, diagnostics, and parameter persistence SHALL each have a named LS2K0300-side construct or implementation file

#### Scenario: Unsupported contracts are surfaced
- **WHEN** a TC264 behavior cannot be reproduced exactly, such as nested interrupts or dual-core execution
- **THEN** the change SHALL document the adaptation boundary and the reason it is not replicated

### Requirement: Retained Control Dependencies Stay In Scope
Phase 1 SHALL carry or explicitly replace any legacy helper module that the retained control path still invokes.

#### Scenario: FUZZY steering dependency is not deferred away
- **WHEN** `old/code/PID.c`, `old/code/All_init.c`, or `old/code/key.c` is retained in phase 1
- **THEN** the required `FUZZY_PID_UCAS` contract, including `InitMH`, `DuoJi_GetP`, and `P_Mode`-selected steering tables, SHALL be migrated or re-expressed in a named replacement module rather than deferred

### Requirement: Hardware Adaptation Profile
The adapter layer SHALL define the active target hardware as an explicit adaptation profile instead of assuming the source and target hardware are identical.

#### Scenario: Same-hardware deployment is declared explicitly
- **WHEN** the target deployment uses hardware that is effectively equivalent to the source system for a given subsystem
- **THEN** the adaptation profile SHALL mark that subsystem as direct-match and the adapter SHALL use the documented pass-through path

#### Scenario: Different-hardware deployment is surfaced through an explicit extension path
- **WHEN** the target deployment differs in camera geometry, IMU traits, encoder polarity, actuator range, or another critical subsystem
- **THEN** the adaptation profile SHALL mark that subsystem with a named adaptation hook or marker, and the adapter SHALL route that subsystem through the documented extension path rather than silently rejecting or degrading it

### Requirement: Legacy Camera Frame Compatibility
The camera adapter SHALL present a legacy-compatible grayscale frame contract to migrated vision logic while enforcing the LS2K0300 phase-1 UVC geometry boundary.

#### Scenario: Accepted phase-1 frame is adapted deterministically
- **WHEN** the adapter receives a valid `160x120` camera frame from the LS2K0300 UVC path
- **THEN** it SHALL expose a `160x128` grayscale legacy buffer by preserving all 160 columns, copying source rows `0..119` into destination rows `8..127`, and filling destination rows `0..7` from source row `0`

#### Scenario: Non-phase-1 frame geometry is surfaced through a marker
- **WHEN** the adapter receives an empty frame or any frame whose dimensions are not exactly `160x120`
- **THEN** it SHALL emit a diagnosable geometry marker and SHALL route the frame through a named extension interface instead of silently consuming it

### Requirement: Fail-Safe Adapter Behavior
The adapter layer SHALL reject unsafe runtime states and prevent actuator updates when critical inputs are missing, stale, or invalid.

#### Scenario: Startup device failure
- **WHEN** camera, IMU, encoder, timer, or motor adapter initialization fails
- **THEN** the application SHALL refuse to arm actuators and SHALL surface a diagnosable error

#### Scenario: Runtime input becomes invalid
- **WHEN** the control loop detects stale sensor data, malformed parameters, or empty camera input
- **THEN** it SHALL suppress or clamp actuator output according to a documented safe behavior instead of continuing silently

### Requirement: Foreground Perception And Emergency Chain Is Preserved Or Explicitly Replaced
Phase 1 SHALL define the old `cpu0_main.c` frame-processing and emergency-veto path as an explicit runtime contract instead of silently collapsing it into the PIT callback.

#### Scenario: Frame ownership and perception location are explicit
- **WHEN** the runtime model is reviewed
- **THEN** it SHALL document which foreground or helper-owned path consumes camera frame-ready events, where thresholding and `eight_neighbor`-equivalent perception runs, and how the resulting analysis is published to the periodic control loop

#### Scenario: Freshness and veto semantics are explicit
- **WHEN** the periodic control loop consumes perception output
- **THEN** it SHALL evaluate documented freshness, empty-frame, and emergency-veto markers before actuator updates, rather than recomputing heavy perception ad hoc inside the PIT callback or silently proceeding on stale results

#### Scenario: Threshold and low-voltage emergency behavior is not silently dropped
- **WHEN** the migration handles the `cpu0_main.c` threshold-based `Emergency_Stop` and low-voltage emergency path
- **THEN** the change SHALL either retain those fail-safe semantics behind LS2K0300 adapters or document a named replacement path and verification hook; silent deferral is forbidden

### Requirement: Phase-1 Parameter Replacement
Phase 1 SHALL replace flash-backed menu tuning with file-backed parameters and explicit defaults.

#### Scenario: Parameter file exists
- **WHEN** the application starts with a valid parameter file
- **THEN** the runtime SHALL load the documented values into the control loop before enabling periodic control

#### Scenario: Persisted parameter surface is explicit
- **WHEN** the phase-1 parameter schema and loader are reviewed
- **THEN** they SHALL document named fields for `Speed_base`, `JWJC`, `circle_k`, `circle_b`, `road_k`, `road_b`, `see_max`, `PID_TURN_CAMERA.D`, `PID_TURN_GYRO_CAMERA.D`, `Straight_permit`, `island_point`, `island_delay`, `circle_k_err`, `P_Mode`, and `exp_light`, including any intentional renames

#### Scenario: Startup-critical parameters are applied before bring-up
- **WHEN** the runtime applies loaded parameters
- **THEN** startup-critical fields such as `P_Mode` and `exp_light` SHALL be applied before fuzzy-table initialization, camera-exposure setup, or actuator arming proceeds

#### Scenario: Parameter file is missing or malformed
- **WHEN** the parameter source cannot be parsed
- **THEN** the application SHALL either load documented safe defaults or stop before arming actuators, and it SHALL record which path was taken

### Requirement: Migrated Runtime State Is Explicitly Initialized
Phase 1 SHALL repair carry-over undefined behavior and hidden initialization dependencies instead of preserving them.

#### Scenario: Read-before-init state becomes owned runtime state
- **WHEN** a legacy control-path value depends on prior ISR execution order or cross-module globals, such as `W_Target_last`, `bcount`, `circle_find`, `zebra_flag`, or `cross_flag`
- **THEN** the LS2K0300 port SHALL move that value into an explicitly initialized runtime or adapter-owned state object before first use
