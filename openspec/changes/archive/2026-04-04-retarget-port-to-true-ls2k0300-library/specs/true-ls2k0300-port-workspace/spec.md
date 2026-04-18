## ADDED Requirements

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
The retarget SHALL record which vendor baseline and build entrypoint were actually used for artifact and implementation verification.

#### Scenario: Verification artifacts record the true root
- **WHEN** implementation verification reviews build or runtime logs
- **THEN** those artifacts SHALL identify the resolved true vendor root, the invoked build entrypoint, and any command-resolution evidence used to run verification, rather than relying on ambiguous or inherited paths from the superseded change
