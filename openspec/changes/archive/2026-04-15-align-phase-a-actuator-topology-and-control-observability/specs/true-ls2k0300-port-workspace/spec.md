## ADDED Requirements

### Requirement: Phase A Documents Separate Startup Proof From Closure Proof
The `new/docs/race-finish-series.zh-CN/` Phase A roadmap/progress documents SHALL distinguish direct-match startup evidence from the remaining closure work for sensor quality, actuator proof, and control unlock.

#### Scenario: Direct-match startup is not described as unresolved
- **WHEN** reviewers inspect the current roadmap, Phase A task list, and progress snapshot after the accepted direct-match board evidence bundle at `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.log`, `runtime-smoke-retry-2026-04-15.exit`, and `hardware-discovery-retry-2026-04-15.log` exists
- **THEN** those documents SHALL state that startup-grade proof has already reached markers such as `imu.init`, `imu.detect`, `startup.complete`, or `control.start`, they SHALL use `runtime-smoke-execution-evidence.md` as the supersession note for earlier direct-match artifacts, and they SHALL NOT continue describing default direct-match startup as wholly unresolved

#### Scenario: Remaining Phase A gaps stay explicit
- **WHEN** the same documents describe open work after startup-grade proof exists
- **THEN** they SHALL explicitly name the remaining gaps as IMU sample quality, encoder trustworthiness, motor-output proof, control unlock, or related evidence gaps rather than collapsing them into one ambiguous “IMU not closed” status

### Requirement: Phase A Evidence Names Differential Steering And Control Unlock
Phase A documentation and verification references SHALL describe the platform as pure differential steering and SHALL require evidence that can explain why control remains vetoed or becomes armed.

#### Scenario: Actuator topology language matches the platform
- **WHEN** reviewers inspect Phase A and nearby roadmap documents
- **THEN** they SHALL find pure differential-drive wording, they SHALL find `turn_output` described as a differential steering adjustment, and they SHALL NOT find the steering path described as depending on an independent servo

#### Scenario: Control unlock evidence is reviewable
- **WHEN** Phase A tasks or verification references describe control-loop closure
- **THEN** they SHALL require evidence or diagnostics that distinguish veto cause, actuator arming state, and applied-output outcome rather than relying on the presence of a single coarse `control.veto` marker alone

## MODIFIED Requirements

### Requirement: Verification Evidence Names The Active Baseline
The retarget SHALL record which vendor baseline, build entrypoint, runtime verification verdict, and accepted board evidence were actually used for artifact and implementation verification, and phase documents SHALL stay aligned with that accepted evidence.

#### Scenario: Phase documents cite the accepted direct-match baseline
- **WHEN** implementation verification accepts a newer direct-match board runtime artifact as the current baseline for Phase A
- **THEN** the roadmap/progress documents and phase-scoped evidence references SHALL align to that accepted artifact, SHALL name the exact accepted evidence path bundle, and SHALL use the corresponding execution-evidence note to explain which earlier artifact was superseded instead of continuing to describe an older pre-fix or degraded-path state as the current truth
