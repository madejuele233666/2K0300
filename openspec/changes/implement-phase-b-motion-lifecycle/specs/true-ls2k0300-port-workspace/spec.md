## ADDED Requirements

### Requirement: Phase B Uses A Lifecycle-Oriented Evidence Flow
The `new/docs/race-finish-series.zh-CN/` Phase B material and change-local verification references SHALL describe low-speed motion closure as a lifecycle-oriented flow that proves start, spinup, running, stop, and fail-safe recovery behavior rather than treating "car moved once" as sufficient evidence.

#### Scenario: Phase B evidence follows lifecycle checkpoints
- **WHEN** reviewers inspect the Phase B document set, tasks, or verification notes
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

## MODIFIED Requirements

### Requirement: Board Smoke Verdicts Reflect Runtime Outcome
The board-side smoke helper SHALL preserve the runtime process outcome as the smoke verdict while still retrieving diagnosable logs from the board.

#### Scenario: Remote runtime failure is not reported as smoke success
- **WHEN** `new/user/run_remote_smoke.sh` uploads configuration, launches the board binary, and the remote process exits with a non-zero status before the smoke budget completes
- **THEN** the script SHALL still retrieve the remote log when possible
- **AND** it SHALL return a failing exit status locally instead of masking the failure with unconditional shell success

#### Scenario: Bounded automation does not replace runtime truth
- **WHEN** the smoke helper injects bounded Phase B automation or fault-injection env for repeatable evidence collection
- **THEN** the copied log SHALL still represent the product runtime's own motion-phase and fail-safe markers
- **AND** the wrapper SHALL record the injected automation inputs as harness context rather than treating them as a different runtime contract
