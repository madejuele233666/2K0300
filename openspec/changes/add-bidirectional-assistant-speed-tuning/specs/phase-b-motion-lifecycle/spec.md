## MODIFIED Requirements

### Requirement: Motion Lifecycle Is Project-Owned And Explicit
The Phase B runtime SHALL define a project-owned motion lifecycle that is evaluated separately from process startup/shutdown and separately from the safety gate. The lifecycle MUST at minimum expose `DISARMED`, `START_REQUESTED`, `SPINUP`, `RUNNING`, `STOPPING`, and `FAIL_SAFE_LATCHED` phases. Accepted operator intent for this lifecycle MAY come from local product entrypoints or from the accepted project-owned remote command surface, but all such intent SHALL still pass through the same motion-intent boundary.

#### Scenario: Remote operator intent does not bypass lifecycle ownership
- **WHEN** a host issues an accepted remote `start` or `stop` command during runtime
- **THEN** the runtime SHALL translate that request into the project-owned motion-intent boundary
- **AND** reviewers SHALL NOT find the remote path writing lifecycle phase or actuator output directly

## ADDED Requirements

### Requirement: Remote Start And Stop Respect Existing Phase Guards
Accepted remote `start` and `stop` commands SHALL respect the same lifecycle transition guards already used by the local product runtime and harness paths.

#### Scenario: Remote start still waits for gate-clear confirmation
- **WHEN** a host requests `start` while the runtime is `DISARMED` or `START_REQUESTED`
- **THEN** the runtime SHALL continue to enforce the documented gate-clear and spinup rules before entering `RUNNING`
- **AND** a remote start request SHALL NOT grant unconstrained drive output by itself

#### Scenario: Remote stop still flows through shaped stopping
- **WHEN** a host requests `stop` during `SPINUP` or `RUNNING`
- **THEN** the runtime SHALL enter `STOPPING` and follow the accepted stop-shaping rules before returning to `DISARMED`
- **AND** the remote stop path SHALL NOT replace the lifecycle-owned stop completion contract with an immediate hard-cut shortcut

#### Scenario: Latched fail-safe blocks remote lifecycle commands
- **WHEN** a host sends remote `start` or `stop` while the runtime is `FAIL_SAFE_LATCHED`
- **THEN** the runtime SHALL reject that command without clearing the latched fault or changing lifecycle phase
- **AND** the host SHALL receive project-owned rejection evidence stating that the fault latch remains active and the documented reset boundary is still authoritative

### Requirement: Tuning Mode Does Not Create A Parallel Lifecycle
The accepted tuning mode SHALL affect only the documented tuning-specific control profile and runtime override behavior. It SHALL NOT define a second motion lifecycle or new actuator-arming contract.

#### Scenario: Tuning mode reuses the normal lifecycle
- **WHEN** tuning mode is enabled for a speed-tuning run
- **THEN** the runtime SHALL continue to publish and honor the same Phase B lifecycle phases and fail-safe behavior
- **AND** tuning mode SHALL remain a scoped control-profile modifier rather than a parallel lifecycle owner
