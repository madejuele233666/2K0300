## ADDED Requirements

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

## MODIFIED Requirements

### Requirement: Verification Evidence Names The Active Baseline
The retarget SHALL record which vendor baseline, build entrypoint, and runtime verification verdict were actually used for artifact and implementation verification.

#### Scenario: Verification artifacts record the true root and smoke result
- **WHEN** implementation verification reviews build or runtime logs
- **THEN** those artifacts SHALL identify the resolved true vendor root, the invoked build entrypoint, the board smoke command-resolution evidence, and whether the runtime process itself succeeded or failed, rather than relying on ambiguous script-level success
