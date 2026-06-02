## ADDED Requirements

### Requirement: Domain Layer Ownership
The active steering implementation SHALL organize source ownership under `new/code/` into the following domain layers: `port/`, `platform/`, `transport/`, `vision/`, `reference/`, `control/`, `safety/`, `observability/`, and `runtime/`.

#### Scenario: Active source directories exist
- **WHEN** the architecture migration is complete
- **THEN** the active source tree SHALL contain domain directories for `transport`, `vision`, `reference`, `control`, `safety`, and `observability`
- **AND** `legacy/` SHALL no longer be used as the active owner for migrated steering algorithms

#### Scenario: Domain ownership is documented
- **WHEN** a developer reads the active architecture documentation
- **THEN** the documentation SHALL describe each new domain directory and the responsibilities it owns
- **AND** it SHALL distinguish active current paths from archive or superseded paths

### Requirement: Dependency Direction
Domain layers SHALL depend on `port/` contracts and MUST NOT depend on `runtime/` for domain logic.

#### Scenario: Forbidden runtime dependency scan
- **WHEN** dependency checks scan `new/code/vision`, `new/code/reference`, `new/code/control`, `new/code/safety`, `new/code/observability`, and `new/code/transport`
- **THEN** `vision`, `reference`, `control`, `safety`, `observability`, and `transport` SHALL NOT include `runtime/*` headers
- **AND** any temporary exception SHALL be documented as an explicit migration compromise with a follow-up task and local verification gate

#### Scenario: Platform remains hardware adapter only
- **WHEN** dependency checks scan `new/code/platform`
- **THEN** active host assistant protocol, steering-media protocol, and host link ownership SHALL NOT remain under `platform/`
- **AND** `platform/true_ls2k0300` SHALL remain the allowed owner for concrete board bridge implementations

### Requirement: Behavior Preservation
The migration SHALL preserve steering runtime behavior, algorithm formulas, parameter semantics, JSON wire formats, and board startup semantics.

#### Scenario: No behavior-changing work is introduced
- **WHEN** implementation tasks move source files, update includes, update namespaces, update CMake, update tests, or update active docs
- **THEN** those tasks SHALL NOT change control formulas, BEV sampling math, CircleV2 state transitions, safety-gate priority, low-voltage threshold semantics, assistant command fields, steering-media envelope formats, or parameter names

#### Scenario: Wire-format compatibility is verified
- **WHEN** assistant and steering-media code has moved into `transport/`
- **THEN** assistant telemetry and steering-media selftests SHALL verify the existing encoded payload behavior still works
- **AND** the verification evidence SHALL not rely only on successful compilation

### Requirement: Vision Ownership
Vision-owned code SHALL include image thresholding, BEV projection/classification helpers, BEV row and raster facts, visual element evidence, visual element pipeline, CircleV2 scene code, and vision-backed reference connectivity.

#### Scenario: CircleV2 scene migration
- **WHEN** `runtime/steering_circle_v2_scene.*`, `runtime/steering_scene_frame_view.hpp`, `runtime/steering_circle_v2_reference_adapter.*`, and `runtime/detail/steering_circle_v2_*` are migrated
- **THEN** they SHALL land under `vision/elements/circle_v2/`
- **AND** their tests SHALL continue to exercise CircleV2 scene, reducer, observer, composer, and reference adapter behavior

#### Scenario: Vision-backed connectivity remains vision-owned
- **WHEN** reference connectivity checks consume frame pixels, projector state, and classification details
- **THEN** that connectivity code SHALL remain in `vision/` rather than `reference/`
- **AND** reference orchestration SHALL consume accepted candidates rather than raw image sampling details

### Requirement: Reference Ownership
Reference-owned code SHALL include visual reference orchestration, reference continuity/hold, usability, lateral error debug facts, tracking geometry, reference-control readiness, and reference time alignment.

#### Scenario: Reference continuity extraction
- **WHEN** hold-related functions currently coupled to BEV simple perception are migrated
- **THEN** `MakeReferenceHoldState`, `BuildReferenceHoldCandidate`, and reference-hold reset semantics SHALL be represented by `reference/reference_continuity.*` or an equivalent reference-owned file
- **AND** BEV simple perception SHALL no longer own hold selection

#### Scenario: Reference time alignment without runtime dependency
- **WHEN** `steering_reference_time_alignment.*` is migrated into `reference/`
- **THEN** its motion-history input contract SHALL be available without including `runtime/runtime_state.hpp`
- **AND** `reference/` SHALL not depend on `runtime/`

### Requirement: Control Ownership
Control-owned code SHALL include motion types, motion supervisor, runtime tuning state, yaw controller, wheel PID, wheel target mixer, and actuator command builder.

#### Scenario: Control loop dependencies are domain named
- **WHEN** `control_loop.*` is updated after migration
- **THEN** it SHALL include control-owned headers from `control/` for controller, mixer, PID, motion, tuning, and actuator command builder responsibilities
- **AND** it SHALL continue to own runtime scheduling and state interaction rather than becoming a control-domain implementation file

### Requirement: Safety Ownership
Safety-owned code SHALL include safety gate evaluation, control apply observation, and low-voltage safety input ownership.

#### Scenario: Control gate split
- **WHEN** `runtime/control_decision.*` is migrated
- **THEN** safety gate input, decision, veto reason, diagnostic string, and priority behavior SHALL be owned by `safety/control_gate.*`
- **AND** control apply observation and apply-outcome string mapping SHALL be owned by `safety/control_apply_observation.*` or an equivalently named safety file

#### Scenario: Low-voltage sampler boundary
- **WHEN** `low_voltage_sampler.*` is migrated
- **THEN** the sampler SHALL preserve startup sample reuse, invalid-sample fail-safe, state transition diagnostics, and sample interval behavior
- **AND** any direct `RuntimeState` write dependency SHALL be handled by a documented runtime-facing boundary rather than silently making safety a runtime implementation layer

### Requirement: Observability Ownership
Observability-owned code SHALL include control debug snapshots, debug reporting, and assistant telemetry view mapping.

#### Scenario: Debug remains non-authoritative
- **WHEN** observability files are migrated
- **THEN** they SHALL serialize, format, or map runtime facts only
- **AND** they SHALL NOT recompute reference selection, safety state, speed target, element state, or actuator intent

### Requirement: Runtime Ownership
Runtime-owned code SHALL remain responsible for startup/shutdown, `RuntimeState`, camera capture and frame store ownership, perception/control loop scheduling, services, and pipeline orchestration.

#### Scenario: Runtime subdirectory organization
- **WHEN** runtime files are organized
- **THEN** lifecycle, capture, pipelines, loops, and services SHALL be grouped under runtime subdirectories or equivalently clear runtime ownership paths
- **AND** the migration SHALL not alter shared mutex ownership, atomics, frame-store lifetime, or service tick semantics

#### Scenario: Pipeline name reflects its scope
- **WHEN** `steering_frame_perception_pipeline.*` is renamed or moved
- **THEN** its active name SHALL reflect that it orchestrates a steering frame pipeline across vision, reference, and readiness steps rather than only raw perception
- **AND** `perception_frontend.*` SHALL remain the runtime scheduler that invokes it
