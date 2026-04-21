## ADDED Requirements

### Requirement: Wheel Targets Are Generated Before Wheel PWM
The Phase B control path SHALL derive logical `left_speed_target` and `right_speed_target` from the lifecycle-owned effective speed target plus bounded turn correction before it computes wheel PWM outputs.

#### Scenario: Wheel targets are explicit in the control path
- **WHEN** the gate is clear and motion lifecycle allows drive output
- **THEN** the control path SHALL compute logical left/right wheel targets before running wheel-speed PID updates
- **AND** the implementation SHALL NOT treat `turn_output` only as a final PWM bias added after a shared mean-speed controller

### Requirement: Left And Right Wheel PID State Is Independent
The runtime SHALL maintain separate PID state for the left and right wheels, including per-wheel error history, integral accumulation, derivative state, and reset behavior.

#### Scenario: One wheel controller does not implicitly share state with the other
- **WHEN** reviewers inspect the wheel-speed control implementation
- **THEN** they SHALL find distinct left and right controller state objects or equivalent project-owned state
- **AND** one wheel's accumulated error, saturation, or reset event SHALL NOT silently reuse the other wheel's PID state

### Requirement: Left And Right Wheel PID Parameters Exist Independently
The runtime parameter surface SHALL define independent left-wheel and right-wheel PID parameters in the first release, even if both sides start from copied baseline values.

#### Scenario: Per-wheel parameter schema is explicit from day one
- **WHEN** reviewers inspect the runtime parameter contract and defaults for the dual-wheel controller
- **THEN** they SHALL find explicit left and right parameter fields for the wheel PID surface
- **AND** the first release SHALL NOT depend on a single shared PID block with implicit per-wheel overrides

### Requirement: Public Actuator Contract Remains Differential Output Only
The dual-wheel control refactor SHALL preserve the existing public actuator contract so the runtime still publishes only `left_pwm`, `right_pwm`, and `emergency_stop` to the motor adapter.

#### Scenario: Wheel-target refactor does not change the platform-facing actuator API
- **WHEN** reviewers inspect `new/code/port/*`, `new/code/runtime/*`, and `new/code/platform/*`
- **THEN** they SHALL find that internal wheel targets remain runtime-owned control data
- **AND** the public actuator command surface SHALL remain limited to differential wheel output plus fail-safe stop semantics

### Requirement: Phase B Observability Exposes Left And Right Control Quantities
The runtime SHALL publish project-owned observability for left/right wheel targets, left/right measured wheel speed, and left/right commanded PWM so Phase B verification can explain wheel behavior directly.

#### Scenario: Phase B evidence can cite left and right wheel behavior explicitly
- **WHEN** diagnostics, assistant wave channels, or verification bundles are inspected for a dual-wheel run
- **THEN** they SHALL provide enough project-owned evidence to distinguish left/right target generation, left/right feedback, and left/right output application
- **AND** reviewers SHALL NOT have to infer wheel-level behavior only from mean encoder speed or a single mixed PWM quantity

### Requirement: Non-Assistant Evidence Surface Exists For Wheel-Level Verification
The runtime SHALL expose a project-owned non-assistant evidence surface for wheel-level control data so reviewers can validate dual-wheel behavior during assistant-disabled runs.

#### Scenario: Assistant-disabled verification still has wheel-level evidence
- **WHEN** Phase B verification runs with the assistant sidecar disabled or unavailable
- **THEN** reviewers SHALL still be able to inspect structured diagnostics markers, structured logs, or harness-visible control snapshot export for left/right targets, left/right measured speed, and left/right PWM
- **AND** assistant waveforms SHALL remain optional support evidence rather than the only wheel-level observability path
