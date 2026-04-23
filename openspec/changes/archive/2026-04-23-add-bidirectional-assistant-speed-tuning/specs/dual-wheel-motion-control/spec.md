## MODIFIED Requirements

### Requirement: Phase B Observability Exposes Left And Right Control Quantities
The runtime SHALL publish project-owned observability for left/right wheel targets, left/right measured wheel speed, and left/right commanded PWM so Phase B verification can explain wheel behavior directly. When tuning mode is enabled, that observability SHALL additionally identify runtime speed-override state and raw-versus-applied turn output so PID tuning evidence can explain why wheel targets changed.

#### Scenario: Tuning runs expose both wheel data and tuning-state context
- **WHEN** diagnostics, assistant wave channels, or structured tuning telemetry are inspected during a tuning-mode run
- **THEN** reviewers SHALL be able to identify left/right wheel targets, left/right measured speed, left/right PWM, target-speed override state, and raw-versus-applied turn output
- **AND** they SHALL NOT have to infer tuning-mode behavior only from one mixed PWM quantity or a single target-speed line

## ADDED Requirements

### Requirement: Turn Suppression Only Changes The Applied Turn Path
The accepted tuning workflow SHALL support temporary turn suppression for straight-speed PID tuning, but that suppression SHALL change only the applied turn value that enters wheel-target generation. The raw turn computation SHALL remain project-owned, reviewable, and observable.

#### Scenario: Raw turn remains observable while applied turn is suppressed
- **WHEN** tuning mode enables turn suppression during a speed-tuning run
- **THEN** the control path SHALL still compute the raw turn correction through the normal project-owned turn logic
- **AND** the wheel-target path SHALL use a suppressed applied turn value according to the tuning-mode contract

#### Scenario: Turn suppression cannot leak into the next normal run
- **WHEN** the tuning session disconnects or tuning mode is explicitly disabled after an accepted tuning run
- **THEN** the runtime SHALL clear turn-suppression state before the next normal run
- **AND** reviewers SHALL be able to identify the clear event through project-owned state or diagnostic evidence

### Requirement: Runtime Speed Override Does Not Change The Public Actuator Contract
The accepted target-speed override path SHALL remain internal to wheel-target generation and lifecycle-owned running-speed selection. It SHALL NOT change the public actuator contract exposed to the motor adapter.

#### Scenario: Live target-speed tuning remains an internal control concern
- **WHEN** reviewers inspect the runtime control path after speed-override support is added
- **THEN** they SHALL find that target-speed override influences runtime-owned target generation and observability only
- **AND** the public actuator command surface SHALL remain limited to `left_pwm`, `right_pwm`, and `emergency_stop`
