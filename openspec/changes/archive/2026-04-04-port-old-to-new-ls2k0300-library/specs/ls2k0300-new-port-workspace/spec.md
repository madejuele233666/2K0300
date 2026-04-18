## ADDED Requirements

### Requirement: Dedicated New Workspace
The migration SHALL create `new/` as a copy-derived workspace that isolates the ported application from `project/` while remaining buildable against the LS2K0300 environment.

#### Scenario: Workspace structure is defined
- **WHEN** reviewers inspect the migrated workspace definition
- **THEN** they SHALL find documented ownership for copied build-entry files under `new/user/`, copied runtime/output scaffolding under `new/out/`, copied or retained model assets under `new/model/`, and migration-owned source or support files under `new/code/port/`, `new/code/platform/`, `new/code/legacy/`, `new/code/runtime/`, `new/config/`, and `new/docs/`, including hardware-profile artifacts used to describe same-hardware or different-hardware deployments

#### Scenario: Vendor files stay out of the migration boundary
- **WHEN** the first migration milestone is implemented
- **THEN** new business logic SHALL reside under `new/` rather than being spread across unrelated vendor example directories

### Requirement: Build Integration With LS2K0300
The `new/` workspace SHALL compile against the LS2K0300 library headers and link dependencies without requiring direct edits to the library driver implementations.

#### Scenario: Build boundary is reviewable
- **WHEN** a reviewer examines the build configuration
- **THEN** they SHALL find that `new/user/build.sh` and `new/user/CMakeLists.txt` are copy-derived from `project/user/` and adapted to reference the LS2K0300 `libraries/` tree while compiling migration-owned code from `new/code/` and its subfolders rather than relying on a separate top-level `new/CMakeLists.txt`

#### Scenario: Vendor driver ownership is preserved
- **WHEN** the migration needs PWM, encoder, IMU, timer, or camera support
- **THEN** it SHALL consume the existing LS2K0300 driver interfaces instead of introducing a second TC264-derived driver stack under `new/`

### Requirement: Managed Runtime Lifecycle
The migrated application SHALL define an explicit startup, periodic control, and shutdown lifecycle suitable for a Linux process.

#### Scenario: Startup sequence is documented
- **WHEN** the main program is reviewed
- **THEN** it SHALL show the order for parameter load, device initialization, control-loop start, and foreground diagnostics

#### Scenario: Shutdown is fail-safe
- **WHEN** the process exits normally or due to initialization failure
- **THEN** the design SHALL require timer stop and actuator disable behavior before resource release completes

#### Scenario: Runtime smoke execution is a separate, executable step
- **WHEN** implementation verification runs the phase-1 smoke path
- **THEN** `new/user/build.sh` SHALL remain the build-and-upload entry, and a documented board-side launch plus log-retrieval contract, such as `new/user/run_remote_smoke.sh`, SHALL execute the uploaded binary and fetch its runtime log back into the OpenSpec verification artifacts
