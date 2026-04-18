## ADDED Requirements

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

## MODIFIED Requirements

### Requirement: Fail-Safe Runtime Behavior Survives The Retarget
The retarget SHALL continue to veto or clamp actuator output when perception or control-critical sensor state is invalid, and it SHALL make the project-owned reason for the in-scope control-gating decision reviewable.

#### Scenario: Invalid normalized state is both suppressed and explained
- **WHEN** perception is stale, perception requests emergency veto, low voltage is active, IMU normalization fails, encoder delta state is not yet trustworthy, or a non-empty actuator command is rejected by the motor apply path
- **THEN** the control path SHALL suppress actuator updates or force actuator arming back to fail-safe, SHALL record the corresponding project-owned veto or apply outcome instead of continuing with stale or partially normalized control state, and SHALL support later Phase A evidence that distinguishes transient startup suppression from sustained runtime blockers

### Requirement: Unsupported Exposure Control Is Surfaced
If the active profile requires `exp_light` to control real camera exposure, the retarget SHALL surface whether the true vendor path supports that requirement instead of silently ignoring it.

#### Scenario: Exposure policy becomes an explicit Phase A decision
- **WHEN** direct-match camera behavior is reviewed as part of Phase A closure
- **THEN** the resulting code and evidence SHALL state whether exposure control is truly supported, unsupported, or routed through a named adaptation boundary, and they SHALL tie that statement to the observed perception freshness or emergency-veto behavior instead of leaving exposure policy as an open-ended future concern
