## ADDED Requirements

### Requirement: Read-Only Target Runtime
The simulation capability SHALL launch the existing target runtime as a read-only test subject and MUST NOT require tracked edits under `new/` to run a simulation.

#### Scenario: Launches Existing Target Without Mutating Source
- **WHEN** a simulation run is started for the target runtime
- **THEN** the run orchestration SHALL launch the existing main runtime entrypoint as an opaque process and SHALL leave tracked files under `new/` unchanged

#### Scenario: Uses Generated Effective Parameters
- **WHEN** the simulator needs runtime parameters for a run
- **THEN** it SHALL write an `effective_params.json` inside that run directory and SHALL pass it to the target process through `LS2K_PARAMS_PATH`

### Requirement: Embedded Target Shadow Contract
The simulation capability SHALL carry a self-contained shadow contract for the target runtime boundary so implementation can proceed from `true_LS2K0300_Library/` plus these OpenSpec artifacts without rereading `new/code`.

#### Scenario: Shadow Contract Documents Runtime Entry And Environment
- **WHEN** an implementer reads the change artifacts
- **THEN** the artifacts SHALL state that the opaque target entry is `new/user/main.cpp` and that the target process reads `LS2K_PROFILE_PATH`, `LS2K_PARAMS_PATH`, `LS2K_AUTO_START`, `LS2K_AUTO_STOP_AFTER_MS`, and `LS2K_AUTO_RESET_FAULT`

#### Scenario: Shadow Contract Documents Bridge Surface
- **WHEN** the C++ simulation bridge is implemented
- **THEN** the artifacts SHALL identify `ls2k::platform::true_ls2k0300` as the replacement namespace and SHALL include camera, IMU, encoder, motor, battery ADC, timer, and disabled-first sidecar bridge groups as required behavior groups

#### Scenario: Shadow Contract Restricts Future Hardware Reservations
- **WHEN** future hardware reservations are documented
- **THEN** only TFLite, Flash/file persistence, and TCP/UDP/wireless assistant SHALL be reserved; unrelated display, button, buzzer, servo, or extra GPIO hardware SHALL NOT be included in this change

### Requirement: Local gRPC Simulator Transport
The simulation capability SHALL use generated gRPC/Protobuf over a local Unix domain socket as the default simulator transport and MUST NOT maintain a hand-written socket protocol.

#### Scenario: Server Socket Is Run-Scoped
- **WHEN** `simcar run` creates a run directory
- **THEN** the Python simulator server SHALL listen on `simcar/runs/<run_id>/sim.sock` or an equivalent run-scoped Unix domain socket path recorded in the replay manifest

#### Scenario: Wire Schema Is Generated
- **WHEN** C++ and Python simulator clients are built
- **THEN** Protobuf/gRPC bindings SHALL be generated from a `.proto` service definition rather than manually duplicated message parsing code

### Requirement: 3D Camera Frame Simulation
The simulation capability SHALL provide a live 3D camera simulation that returns `320x240` grayscale camera frames of exactly `76800` bytes to the target bridge.

#### Scenario: Captures Target-Compatible Gray Frame
- **WHEN** the target bridge requests a camera frame
- **THEN** the simulator SHALL return a valid grayscale frame with width `320`, height `240`, and `320 * 240` bytes

#### Scenario: Prioritizes Image Algorithm Stress
- **WHEN** scenario image conditions are configured
- **THEN** the simulator SHALL be able to produce bend, cross, circle, straight, lost-line, overexposure, and shadow cases through the live 3D backend or documented first-stage approximations

### Requirement: Simulated LS2K0300 Device Behaviors
The simulation capability SHALL provide sufficient simulated LS2K0300 device behavior for target startup and closed-loop running.

#### Scenario: Startup Sensors Are Valid
- **WHEN** the target runtime initializes in simulation
- **THEN** camera, IMU, encoder, motor, battery ADC, timer, and persistence behavior SHALL be available enough for startup to complete unless a scenario explicitly injects a startup failure

#### Scenario: Battery Defaults Non-Emergency
- **WHEN** no low-voltage fault is configured
- **THEN** the simulated battery raw ADC sample SHALL be valid and above the configured emergency threshold so low voltage does not block startup

#### Scenario: Motor Commands Affect Future Sensors
- **WHEN** the target applies logical left and right motor PWM commands
- **THEN** the simulator SHALL record the command and use it to update subsequent vehicle state, camera pose, encoder counts, and IMU samples

### Requirement: Run Artifact Contract
The simulation capability SHALL write complete run artifacts for debugging, replay, and regression checks.

#### Scenario: Writes Required Run Files
- **WHEN** a simulation run ends
- **THEN** the run directory SHALL include `summary.json`, `scenario.yaml`, `effective_params.json`, `replay_manifest.json`, `timeline.jsonl`, `sensors.jsonl`, `actuators.jsonl`, `control_debug_snapshot.jsonl`, `failures.jsonl`, raw frame files, PNG frame files, and `video.mp4`

#### Scenario: Failure Is Reproducible
- **WHEN** a run fails
- **THEN** `summary.json` and `replay_manifest.json` SHALL record the scenario, seed, first failure frame when available, and artifact paths needed to replay or inspect the failure

### Requirement: First-Stage Runtime Mode Documentation
The simulation capability SHALL document first-stage runtime-mode limits and MUST NOT claim bit-for-bit deterministic simulation in the MVP.

#### Scenario: Documents Real-Time Limitation
- **WHEN** runtime-mode documentation is generated
- **THEN** it SHALL state that the first stage uses wall-clock real-time execution, is seed-stable for scenario inputs, and is not bit-for-bit deterministic because the target time source uses `std::chrono::steady_clock`

#### Scenario: Defers Deterministic Time Hook
- **WHEN** deterministic stepping is discussed
- **THEN** it SHALL be marked as a later change that may require a minimal target time-source hook
