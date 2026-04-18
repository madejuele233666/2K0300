## ADDED Requirements

### Requirement: Phase A Closure Uses A Single Evidence-Oriented Checkpoint Flow
The `new/docs/race-finish-series.zh-CN/` Phase A material and change-local verification references SHALL describe the remaining Phase A work as one closure flow with ordered checkpoints rather than as unrelated, parallel “nice to have” tasks.

#### Scenario: Checkpoints follow the control-critical dependency chain
- **WHEN** reviewers inspect the Phase A task list, current progress snapshot, or a Phase A closure change
- **THEN** they SHALL find the remaining work ordered as accepted baseline freeze, sensor trust closure, actuator trust closure, and final control-unlock/exit judgment, with exposure-policy decision either remaining as the default downstream closure step or being explicitly promoted earlier when perception or exposure is the dominant blocker, and downstream checkpoints SHALL NOT be described as complete while upstream trust prerequisites remain open

### Requirement: Phase A Evidence Bundles Are Phase-Scoped And Decision-Oriented
Phase A closure work SHALL emit phase-scoped evidence bundles that state what each checkpoint proves, what it does not prove, and whether the checkpoint blocks stage exit.

#### Scenario: Evidence files distinguish checkpoint proof from stage exit
- **WHEN** reviewers inspect change-local verification notes or Phase A evidence references
- **THEN** each checkpoint evidence bundle SHALL identify its target question, SHALL reference the accepted baseline it builds on when relevant, and SHALL state whether the result closes a Phase A blocker, leaves the blocker open, or escalates to a follow-on change

### Requirement: Phase A Exit Judgment Is Explicit
Phase A closure work SHALL end with an explicit stage-exit judgment rather than an implied conclusion spread across logs and roadmap prose.

#### Scenario: Phase A pass or fail is written directly
- **WHEN** a Phase A closure change reaches implementation verification or final document sync
- **THEN** the resulting progress and phase documents SHALL explicitly state whether Phase A exit conditions are met, which blockers remain if they are not met, and whether entry into `Phase B` is allowed

## MODIFIED Requirements

### Requirement: Verification Evidence Names The Active Baseline
The retarget SHALL record which vendor baseline, build entrypoint, runtime verification verdict, and accepted board evidence were actually used for artifact and implementation verification, and phase documents SHALL stay aligned with that accepted evidence.

#### Scenario: Phase documents cite the accepted direct-match baseline
- **WHEN** implementation verification accepts a newer direct-match board runtime artifact as the current baseline for Phase A
- **THEN** the roadmap/progress documents and phase-scoped evidence references SHALL align to that accepted artifact, SHALL name the exact accepted evidence path bundle, SHALL use the corresponding execution-evidence note to explain which earlier artifact was superseded, and SHALL treat that accepted baseline as the starting point for later checkpoint evidence rather than re-litigating startup-grade proof in every Phase A subtask

### Requirement: Phase A Evidence Names Differential Steering And Control Unlock
Phase A documentation and verification references SHALL describe the platform as pure differential steering and SHALL require evidence that can explain why control remains vetoed or becomes armed.

#### Scenario: Control unlock evidence stays tied to stage blockers
- **WHEN** Phase A tasks or verification references describe control-loop closure
- **THEN** they SHALL require evidence that identifies the dominant veto reason, actuator arming changes, and apply outcomes, and they SHALL connect those observations back to named Phase A blockers such as sensor trust, actuator trust, or exposure-policy uncertainty instead of treating marker presence alone as stage closure
