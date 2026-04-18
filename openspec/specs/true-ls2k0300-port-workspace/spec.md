# true-ls2k0300-port-workspace Specification

## Purpose
Define the accepted `new/` workspace boundary, true vendor baseline selection, build integration, and verification-evidence rules for the TC264-to-LS2K0300 retarget against `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library`.
## Requirements
### Requirement: Real Vendor Baseline Is Explicit
The migrated workspace SHALL treat `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library` as the only accepted phase-1 vendor baseline.

#### Scenario: Wrong baseline paths are rejected
- **WHEN** proposal, design, tasks, build files, or verification artifacts are reviewed
- **THEN** any accepted target-root reference SHALL resolve to `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library`, and references to `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library` SHALL be treated only as superseded historical evidence

### Requirement: Superseded Change Is Preserved But Not Promoted
The earlier change `port-old-to-new-ls2k0300-library` SHALL remain historical evidence of the wrong baseline choice and SHALL NOT be used as the accepted spec source for the real retarget.

#### Scenario: Spec sync boundary is explicit
- **WHEN** reviewers assess archive and sync behavior for the superseded change
- **THEN** they SHALL find an explicit rule that its artifacts may be archived as superseded history but SHALL NOT be synced into `openspec/specs/` as the accepted contract for the true LS2K0300 retarget

### Requirement: New Workspace Remains The Migration Boundary
The migration SHALL keep `new/` as the owned workspace boundary instead of spreading project logic into vendor example directories.

#### Scenario: Workspace ownership is reviewable
- **WHEN** reviewers inspect the retargeted workspace
- **THEN** they SHALL find migration-owned code and documentation under `new/`, with vendor-owned source remaining under `true_LS2K0300_Library/...` and not duplicated into unrelated example folders

### Requirement: Build Integration Uses The True Project Skeleton
`new/user/` SHALL be derived from the true vendor `project/user` skeleton and SHALL compile only against source files that exist in the true vendor tree.

#### Scenario: Build graph references real vendor files only
- **WHEN** a reviewer inspects `new/user/CMakeLists.txt`, `new/user/build.sh`, and related build configuration
- **THEN** they SHALL find that the configured vendor root is the true LS2K0300 project tree, and the build graph SHALL NOT require superseded-only files such as `zf_driver_file_buffer.cpp`, `zf_driver_file_string.cpp`, `zf_driver_pit_fd.cpp`, or `zf_device_imu.cpp`

### Requirement: Verification Evidence Names The Active Baseline
The retarget SHALL record which vendor baseline, build entrypoint, runtime verification verdict, and accepted board evidence were actually used for artifact and implementation verification, and phase documents SHALL stay aligned with that accepted evidence.

#### Scenario: Phase documents cite the accepted direct-match baseline
- **WHEN** implementation verification accepts a newer direct-match board runtime artifact as the current baseline for Phase A
- **THEN** the roadmap/progress documents and phase-scoped evidence references SHALL align to that accepted artifact, SHALL name the exact accepted evidence path bundle, SHALL use the corresponding execution-evidence note to explain which earlier artifact was superseded, and SHALL treat that accepted baseline as the starting point for later checkpoint evidence rather than re-litigating startup-grade proof in every Phase A subtask

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

#### Scenario: Control unlock evidence stays tied to stage blockers
- **WHEN** Phase A tasks or verification references describe control-loop closure
- **THEN** they SHALL require evidence that identifies the dominant veto reason, actuator arming changes, and apply outcomes, and they SHALL connect those observations back to named Phase A blockers such as sensor trust, actuator trust, or exposure-policy uncertainty instead of treating marker presence alone as stage closure

### Requirement: Board Smoke Verdicts Reflect Runtime Outcome
The board-side smoke helper SHALL preserve the runtime process outcome as the smoke verdict while still retrieving diagnosable logs from the board.

#### Scenario: Remote runtime failure is not reported as smoke success
- **WHEN** `new/user/run_remote_smoke.sh` uploads configuration, launches the board binary, and the remote process exits with a non-zero status before the smoke budget completes
- **THEN** the script SHALL still retrieve the remote log when possible, but it SHALL return a failing exit status locally instead of masking the failure with unconditional shell success

#### Scenario: Log retrieval remains best-effort evidence collection
- **WHEN** the remote runtime fails after producing a partial log
- **THEN** the smoke helper SHALL attempt to copy the remote log back into the change verification directory before returning failure so the startup/runtime problem remains diagnosable

### Requirement: Verification Evidence Records Resolved Hardware Inputs
Board-runtime verification evidence SHALL name the resolved hardware resources or discovery results that were used during direct-match startup for critical subsystems.

#### Scenario: Board logs show resolved direct-match evidence
- **WHEN** implementation verification reviews board smoke output for a direct-match startup run
- **THEN** the resulting logs SHALL identify the resolved IMU node or discovery failure, the camera path, and any encoder/motor/ADC resource evidence needed to explain startup success or vetoed failure

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

