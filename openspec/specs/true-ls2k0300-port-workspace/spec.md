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

### Requirement: Build Integration Treats Assistant As A New-Owned Sidecar
`new/user/` build integration SHALL compile any assistant sidecar implementation from the `new/` workspace while sourcing vendor assistant libraries only from the accepted true baseline, and assistant support SHALL remain optional at runtime.

#### Scenario: Assistant build graph stays inside the owned workspace boundary
- **WHEN** reviewers inspect `new/user/CMakeLists.txt` and related build files after this change
- **THEN** they SHALL find assistant sidecar source files owned under `new/`
- **AND** any vendor assistant source references SHALL resolve only to the accepted `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library` tree
- **AND** the accepted sidecar release surface for this change SHALL be TCP-only waveform publication plus E07_04-style image publication
- **AND** the build SHALL NOT require moving business logic into vendor example directories

### Requirement: Verification Evidence Names The Active Baseline
The retarget SHALL record which vendor baseline, build entrypoint, runtime verification verdict, and accepted board evidence were actually used for artifact and implementation verification, and phase documents SHALL stay aligned with that accepted evidence.

#### Scenario: Phase documents cite the accepted direct-match baseline
- **WHEN** implementation verification accepts a newer direct-match board runtime artifact as the current baseline for Phase A
- **THEN** the roadmap/progress documents and phase-scoped evidence references SHALL align to that accepted artifact, SHALL name the exact accepted evidence path bundle, SHALL use the corresponding execution-evidence note to explain which earlier artifact was superseded, and SHALL treat that accepted baseline as the starting point for later checkpoint evidence rather than re-litigating startup-grade proof in every Phase A subtask

### Requirement: Historical Phase A Documents Are Superseded
The historical `new/docs/superseded/race-finish-series.zh-CN/` Phase A roadmap/progress documents SHALL remain archived background. Current runtime, parameter, media, and verification authority SHALL come from root `README.md`, `new/config/default_params.md`, `new/code/port/README.md`, `new/user/README.md`, and active tests.

#### Scenario: Direct-match startup is not described as unresolved
- **WHEN** reviewers inspect the active documentation surface
- **THEN** they SHALL NOT treat the historical Phase A roadmap as current authority for runtime, parameter, media, overlay, or verification decisions

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

#### Scenario: Bounded automation does not replace runtime truth
- **WHEN** the smoke helper injects bounded Phase B automation or fault-injection env for repeatable evidence collection
- **THEN** the copied log SHALL still represent the product runtime's own motion-phase and fail-safe markers
- **AND** the wrapper SHALL record the injected automation inputs as harness context rather than treating them as a different runtime contract

### Requirement: Verification Evidence Records Resolved Hardware Inputs
Board-runtime verification evidence SHALL name the resolved hardware resources or discovery results that were used during direct-match startup for critical subsystems.

#### Scenario: Board logs show resolved direct-match evidence
- **WHEN** implementation verification reviews board smoke output for a direct-match startup run
- **THEN** the resulting logs SHALL identify the resolved IMU node or discovery failure, the camera path, and any encoder/motor/ADC resource evidence needed to explain startup success or vetoed failure

### Requirement: Phase A Closure History Remains Archived
The historical Phase A material under `new/docs/superseded/race-finish-series.zh-CN/` SHALL remain available only as background for older evidence-oriented checkpoint decisions.

#### Scenario: Checkpoints follow the control-critical dependency chain
- **WHEN** reviewers inspect current active docs
- **THEN** they SHALL find the current simple reference/control contract rather than the old Phase A closure roadmap as active authority

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

### Requirement: Phase B Lifecycle Contract Is Not Owned By The Old Roadmap
The runtime lifecycle contract SHALL be documented by active specs, code, tests, and current operator docs. The old `new/docs/superseded/race-finish-series.zh-CN/` Phase B material SHALL NOT be used as active authority.

#### Scenario: Phase B evidence follows lifecycle checkpoints
- **WHEN** reviewers inspect active lifecycle specs, tasks, or verification notes
- **THEN** they SHALL find explicit evidence targets for startup safety, shaped start behavior, low-speed running, controlled stop, and fail-safe recovery
- **AND** the phase SHALL NOT be treated as complete based only on marker presence or a single non-zero motor command

### Requirement: Test Automation Stays Separate From Product Motion Semantics
Phase B verification MAY use bounded automation for repeatable board tests, but the accepted workspace contract SHALL keep that automation separate from the long-lived product runtime semantics.

#### Scenario: Bounded automation is documented as test harness behavior
- **WHEN** Phase B verification references auto-start, auto-stop, bounded frame limits, or post-fault auto-reset
- **THEN** those behaviors SHALL be described as smoke/bench/test harness policy
- **AND** the product runtime contract SHALL remain "controlled stop before exit" rather than "always auto-run"

### Requirement: Phase B Verification Notes Are Concrete And Bounded
The accepted Phase B verification bundle SHALL include implementation-grade notes for `B-1` through `B-5` that document the exact command or env entrypoint used, the expected markers, and what each artifact proves versus what it does not prove.

#### Scenario: Phase B note deliverables are explicit
- **WHEN** reviewers inspect Phase B verification notes or change-local verification bundles
- **THEN** they SHALL find notes covering static safety/start-stop, half-load output behavior, straight-run behavior, turn/run behavior, and fault injection
- **AND** each note SHALL record the concrete command or env contract used to produce the artifact
- **AND** each note SHALL explicitly state the evidentiary limits of the corresponding log, video, or tuning artifact

### Requirement: Phase B Verification Uses Wheel-Level Evidence
The `new/` workspace documentation and verification flow SHALL describe dual-wheel control with wheel-level targets, wheel-level feedback, and wheel-level outputs, and SHALL treat assistant evidence as optional support evidence rather than as the acceptance gate itself.

#### Scenario: Phase B docs and evidence distinguish wheel-level behavior from sidecar convenience
- **WHEN** Phase B task docs, progress notes, or verification bundles are updated for this change
- **THEN** they SHALL name left/right target, left/right feedback, and left/right PWM observability as accepted evidence for the refactored control path
- **AND** they SHALL name at least one project-owned non-assistant evidence surface, such as structured diagnostics or harness-visible snapshot export, for assistant-disabled verification
- **AND** they SHALL describe assistant waveforms and image publication as optional convenience evidence that supports diagnosis but does not replace the primary runtime or board-test verdict
