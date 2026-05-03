## ADDED Requirements

### Requirement: Steering Control MUST Consume BEV-Native Error Terms
The accepted steering control path MUST compute its primary camera-side steering contribution from BEV-native error terms, including near-field lateral error, far-field heading error, preview curvature, and accepted confidence/visibility context. Compatibility-only image fields MUST NOT remain the primary control contract.

#### Scenario: Ordinary steering is explained in BEV terms
- **WHEN** reviewers inspect one accepted ordinary steering cycle
- **THEN** they MUST be able to explain the camera-side steering target from BEV-native error terms
- **AND** the primary explanation MUST NOT require reconstructing legacy image-row semantics

### Requirement: ControlConstraintSet MUST Provide The Extensible Control Hook
The accepted architecture MUST provide a typed `ControlConstraintSet` or equivalent project-owned control-constraint surface so future observation modules can bias speed, steering gain, path bias, or temporary steering suppression without bypassing the accepted control boundary.

#### Scenario: A future module lowers steering aggressiveness
- **WHEN** a future observation module needs to reduce steering authority for safety or surface conditions
- **THEN** it MUST be able to do so through the accepted control-constraint surface
- **AND** implementers MUST NOT need to hardcode a new one-off branch directly into the PID implementation

### Requirement: Low-Confidence BEV MUST Degrade Through Constraints Or Fail-Safe
When BEV visible range, track confidence, calibration validity, or sampled-path continuity falls below accepted thresholds, the runtime MUST degrade through `ControlConstraintSet`, steering gain limits, speed limits, steering suppression, or fail-safe gating. It MUST NOT recover steering authority by falling back to legacy `LaneMetrics`, bottom-tracker output, `highest_line`, or compatibility projection fields as the primary control source.

#### Scenario: BEV confidence drops during an ordinary run
- **WHEN** the accepted BEV geometry output reports low confidence, short visible range, or invalid projector/calibration state
- **THEN** the control path MUST expose the degraded state and apply the accepted constraint or fail-safe behavior
- **AND** the camera-side steering target MUST remain explainable without reading legacy image-space geometry as authority

### Requirement: Control-Model Tuning Values MUST Have Explicit Parameter Ownership
The accepted BEV-native control model MUST load or derive all control-affecting tuning values through project-owned runtime parameters, accepted calibration artifacts, or explicitly documented compile-time invariants. This includes near/far sample selectors, lateral/heading/curvature weights, confidence degradation thresholds, gain scales, constraint limits, and steering suppression thresholds. Source constants MAY remain only for algorithm structure, default values, fixed array capacities, unit conversions, and hard safety clamps.

#### Scenario: Control cutover does not introduce undocumented tuning constants
- **WHEN** reviewers inspect `steering_control_error_model.*`, `pid_control.*`, `control_loop.*`, parameter loading, and preserved `config_snapshot` evidence
- **THEN** each control-affecting threshold, weight, scale, sample selector, or constraint limit MUST be traceable to a runtime parameter, accepted calibration artifact, or documented compile-time invariant
- **AND** implementers MUST NOT need to edit source code to tune BEV control behavior during board bring-up

### Requirement: Runtime-Owned State MUST Separate Track, Scene, And Controller Memory
The runtime-owned steering state MUST preserve distinct ownership for BEV track history, scene/FSM state, reference-policy carry-over, and controller memory so reset and fail-safe boundaries can clear each domain without recreating hidden coupling.

#### Scenario: Steering reset returns to a clean BEV control baseline
- **WHEN** the runtime performs a steering reset or stop-to-disarmed reset
- **THEN** BEV track carry-over, scene/FSM state, reference-policy memory, and controller memory MUST return to their accepted post-reset baseline
- **AND** the next control cycle MUST NOT inherit stale scene or controller state from before the reset

### Requirement: Ordinary And Special Scenes MUST Share One Control Entry
The accepted control loop MUST use one control-error entry surface for ordinary tracking and special-scene stages. Scene-specific behavior MAY change the reference path or control constraints, but it MUST NOT bypass the accepted control-entry contract.

#### Scenario: Circle interior still uses the standard control entry
- **WHEN** the FSM is in a circle interior or exit stage
- **THEN** the runtime MAY use a different reference path or constraint set
- **AND** the steering target MUST still be computed through the accepted control-entry model rather than a scene-specific direct motor branch

### Requirement: Legacy Gain Inputs MAY Survive Only Outside Runtime Authority
If a legacy field such as `highest_line` survives during migration, it MUST be compatibility/debug/test-only or explicitly quarantined outside the authoritative runtime control path. After BEV-native control cutover, the runtime MUST NOT use legacy image-space fields for primary gain scheduling, path authority, or control-error computation.

#### Scenario: Legacy gain fields remain non-authoritative
- **WHEN** source review finds a surviving legacy image-space field after BEV control cutover
- **THEN** design and source review MUST be able to identify it as compatibility/debug/test-only or quarantined non-authoritative code
- **AND** the accepted long-term control contract MUST remain expressed in BEV-native terms
