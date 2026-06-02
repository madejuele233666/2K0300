## ADDED Requirements

### Requirement: Ai-Enforced Artifact Gate
The change SHALL use the `ai-enforced-workflow` artifact and verification sequence before implementation begins.

#### Scenario: Artifact bundle is complete
- **WHEN** the change is ready for implementation
- **THEN** `proposal.md`, all capability delta specs, `design.md`, and `tasks.md` SHALL exist under `openspec/changes/restructure-steering-domain-architecture/`
- **AND** `openspec status --change restructure-steering-domain-architecture --json` SHALL show the `tasks` apply requirement complete or otherwise implementation-ready according to the schema

#### Scenario: Docs-first verifier gate is required
- **WHEN** the apply-required artifact set is complete
- **THEN** the caller SHALL run the docs-first artifact review through `verify-sequence/default`
- **AND** a valid active pass SHALL require complete and exhaustive review coverage in verifier execution evidence

### Requirement: Staged Migration Slices
Implementation tasks SHALL be split into independently verifiable migration slices rather than one broad path rewrite.

#### Scenario: Slice-level feedback
- **WHEN** a migration slice moves or renames a domain group
- **THEN** that slice SHALL update the related source paths, include paths, build source lists, direct test scripts, and focused documentation references before moving to the next slice
- **AND** that slice SHALL name a focused verification command or source scan that proves the moved group still builds and stays inside its new boundary

#### Scenario: No late-only verification
- **WHEN** implementation proceeds through multiple slices
- **THEN** verification SHALL run after each high-risk slice
- **AND** final broad validation SHALL not be the only evidence that intermediate safety, transport, observability, or CircleV2 ownership was migrated correctly

### Requirement: Explicit Change Surface Inventory
The implementation plan SHALL include a concrete change-surface inventory derived from active source, build, test, and documentation paths.

#### Scenario: Code surfaces are enumerated
- **WHEN** tasks are reviewed
- **THEN** the plan SHALL name affected surfaces in `new/code/legacy`, `new/code/runtime`, `new/code/platform`, `new/code/port`, and newly introduced domain directories

#### Scenario: Build and test surfaces are enumerated
- **WHEN** tasks are reviewed
- **THEN** the plan SHALL name `new/user/CMakeLists.txt`, direct verification scripts, C++ verification tests, `common_steering_test_build.sh`, and residual checks that must be updated

#### Scenario: Documentation surfaces are enumerated
- **WHEN** tasks are reviewed
- **THEN** the plan SHALL name active documentation surfaces that must be updated
- **AND** archive or superseded documentation SHALL not be rewritten as active authority unless specifically routed by current docs

### Requirement: Dependency and Residual Checks
The implementation plan SHALL include dependency-direction checks and residual path checks for removed ownership names.

#### Scenario: Forbidden include checks
- **WHEN** source migration is complete
- **THEN** checks SHALL verify that domain layers do not include forbidden `runtime/*` or stale `legacy/*` headers outside documented exceptions
- **AND** checks SHALL verify that assistant and steering-media protocol/link headers are no longer included from `platform/` paths except board bridge dependencies

#### Scenario: Stale path checks
- **WHEN** build, tests, and docs are updated
- **THEN** checks SHALL search active build files, test scripts, and active docs for stale `new/code/legacy/`, `runtime/steering_circle_v2_*`, `runtime/control_decision.*`, `runtime/low_voltage_sampler.*`, `runtime/control_debug_*`, and `platform/assistant_*` or `platform/steering_media_*` references

### Requirement: Board-Aware Verification
Because the migration touches runtime, platform-adjacent transport, safety, and build wiring, the implementation plan SHALL include board-aware verification without claiming board behavior from local-only evidence.

#### Scenario: Local verification precedes board verification
- **WHEN** implementation changes are ready for board testing
- **THEN** focused local tests and host/selftest checks SHALL run first
- **AND** board upload or smoke tasks SHALL be performed only after local build/test evidence is available

#### Scenario: Board smoke scope is explicit
- **WHEN** board smoke is run
- **THEN** the evidence SHALL distinguish upload/build success, no-motion runtime startup, assistant/steering-media transport availability, and safety-gate state
- **AND** it SHALL NOT claim powered driving behavior unless a powered drive command was explicitly requested and verified

### Requirement: Independent Implementation Verification
The implementation SHALL finish with source-first independent verification using the ai-enforced verification-cycle contracts.

#### Scenario: Verifier lifecycle follows orchestrator decisions
- **WHEN** implementation verification runs
- **THEN** the caller SHALL maintain current-state `agent-table.json`
- **AND** resume, spawn, repair, mark-non-active, and terminate decisions SHALL be taken from the verification-cycle orchestrator rather than ad hoc prompt judgment

#### Scenario: Valid pass is complete
- **WHEN** source-first verification reports pass
- **THEN** pass SHALL only be accepted if verifier evidence reports `review_coverage.coverage_status=complete` and `review_coverage.exhaustive=true`
- **AND** every skipped path or partial scope SHALL be explicitly justified in verifier evidence
