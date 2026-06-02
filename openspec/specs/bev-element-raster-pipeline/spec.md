# bev-element-raster-pipeline Specification

## Purpose
Define the runtime BEV element raster and single-frame steering perception pipeline boundary used by visual element detectors without making debug imagery or control owners part of detector authority.

## Requirements
### Requirement: BEV Element Raster Parameters Have Deterministic Runtime Semantics
The runtime SHALL define a `BEV_ELEMENT_RASTER` parameter group with at least `ENABLED` and `WIDTH`. Missing `BEV_ELEMENT_RASTER` fields SHALL use the `RuntimeParameters{}` fallback values. `ENABLED` SHALL default to true for this base change. `WIDTH` SHALL default to the current effective dense BEV width of `320` cells. `WIDTH` SHALL be valid only when it is an integer greater than or equal to `2`; malformed or out-of-range `BEV_ELEMENT_RASTER` fields SHALL be treated as parameter parse failure and SHALL fall back to `RuntimeParameters{}` defaults through the existing parameter fallback path.

#### Scenario: Missing raster group uses defaults
- **WHEN** runtime parameters are loaded from JSON that omits `BEV_ELEMENT_RASTER`
- **THEN** parsing SHALL succeed
- **AND** `BEV_ELEMENT_RASTER.ENABLED` SHALL use the `RuntimeParameters{}` default true value
- **AND** `BEV_ELEMENT_RASTER.WIDTH` SHALL use the `RuntimeParameters{}` default `320` value

#### Scenario: Invalid raster width fails closed through parameter fallback
- **WHEN** `BEV_ELEMENT_RASTER.WIDTH` is malformed or less than `2`
- **THEN** the parameter load SHALL be marked malformed according to the existing parameter fallback semantics
- **AND** the runtime SHALL use `RuntimeParameters{}` fallback values rather than silently clamping the invalid width

#### Scenario: Disabled raster is unavailable, not sampled
- **WHEN** `BEV_ELEMENT_RASTER.ENABLED=false`
- **THEN** the raster builder SHALL return an unavailable raster with no sampleable cells
- **AND** it SHALL NOT build or consume a raster projection LUT for that frame
- **AND** sparse line perception and sparse-derived cross evidence SHALL continue through their existing inputs

### Requirement: Runtime BEV Element Raster Is A Visual Fact Input
The runtime SHALL provide a `BEVElementRaster` visual fact surface derived from the current camera frame, active threshold, BEV projector, and `BEV_ELEMENT_RASTER` parameters. The raster SHALL expose metric/cell conversion, per-cell classification, and projection/sample availability so element detectors can consume BEV facts without reading debug images or recomputing projection rules.

#### Scenario: Raster cells carry sampleability and classification
- **WHEN** the raster is built for a camera frame with valid projector configuration
- **THEN** each raster cell SHALL expose a cell classification such as white, black, unknown, or invalid
- **AND** each cell SHALL expose whether its source projection was sampleable, outside the source frame, failed projection, or otherwise unavailable
- **AND** cells that are not sampleable SHALL NOT be converted into white, black, path, opening, or boundary facts

#### Scenario: Runtime raster is not debug authority
- **WHEN** debug, overlay, assistant, or media code needs to observe BEV raster-related facts
- **THEN** it MAY serialize or render facts produced by the runtime raster path
- **AND** it SHALL NOT feed a debug dense image back into detector, reference, hold, safety, yaw, or actuator decisions

### Requirement: Runtime Raster Uses Precomputed Sampling Work
The runtime raster implementation SHALL avoid per-frame projection recomputation for every cell under stable projector and raster parameters. It SHALL use precomputed sampling facts and reusable buffers so frame-time work is limited to threshold classification and image sampling.

#### Scenario: LUT-backed raster build reuses frame-independent work
- **WHEN** the raster grid and projector parameters remain unchanged across frames
- **THEN** the runtime SHALL reuse a precomputed raster projection/sample LUT
- **AND** per-frame raster build SHALL reuse owned output buffers rather than allocating a new raster cell buffer for every frame

#### Scenario: Raster dimensions remain metric-consistent
- **WHEN** `BEV_ELEMENT_RASTER.WIDTH` is configured
- **THEN** the raster height SHALL be derived from the configured BEV metric width/forward aspect ratio
- **AND** metric-to-cell and cell-to-metric conversions SHALL map representative cells consistently within the configured raster frame

### Requirement: Runtime Raster Publishes Performance Evidence
The runtime SHALL expose raster build cost through the existing performance-counter diagnostics when performance counters are enabled. The raster build stage SHALL be named `perception.element_raster` in `perf.window` diagnostics and SHALL be backed by a dedicated `PerfStage::kPerceptionElementRaster` enum value or equivalent project-owned stage.

#### Scenario: Raster build stage is observable on board
- **WHEN** performance counters are enabled and runtime raster build executes
- **THEN** `perf.window` diagnostics SHALL be able to emit a `stage=perception.element_raster` entry
- **AND** that entry SHALL include the existing count, average, maximum, and last-duration fields used by other performance stages

### Requirement: Steering Frame Perception Pipeline Owns One-Frame Perception
The runtime SHALL separate frontend lifecycle ownership from single-frame perception ownership. `PerceptionFrontend` SHALL own camera capture, fault injection, empty-frame fallback, state publication, memory reset, and diagnostics. A single-frame steering perception pipeline SHALL own thresholding, sparse BEV rows, element raster, line candidate construction, visual element aggregation, visual-reference orchestration, hold continuity, reference usability, lateral error, and reference-control readiness for the current frame.

#### Scenario: Frontend does not call element detectors directly
- **WHEN** reviewers inspect `PerceptionFrontend`
- **THEN** they SHALL find that element evidence is produced through the single-frame perception pipeline
- **AND** `PerceptionFrontend` SHALL NOT directly call cross, circle, roadblock, or ML element detector functions

#### Scenario: Existing control chain semantics remain unchanged
- **WHEN** the single-frame pipeline processes the same sparse line facts with `BEV_ELEMENT.CROSS_EXIT_TAKEOVER_ENABLED=false`
- **THEN** selected-reference, hold, reference-usability, lateral-error, and readiness behavior SHALL remain equivalent to the prior line-only default control behavior
- **AND** raster creation SHALL NOT by itself cause an element candidate to enter arbitration

### Requirement: Visual Element Pipeline Aggregates Element Evidence Only
The runtime SHALL provide a visual element pipeline entry point that aggregates element detector evidence and optional visual-reference candidates. The element pipeline SHALL NOT own selected-reference choice, hold continuity, reference usability, lateral error, reference-control readiness, safety, yaw, or actuator behavior.

#### Scenario: Cross is registered through the element pipeline
- **WHEN** the current base change runs visual element aggregation
- **THEN** cross-exit SHALL be the only implemented element registered in the pipeline
- **AND** cross candidate inclusion SHALL continue to depend on the existing explicit takeover parameter

#### Scenario: Future elements can append evidence without touching frontend orchestration
- **WHEN** a future element detector is added
- **THEN** it SHALL be added behind the visual element pipeline boundary
- **AND** the runtime frontend SHALL NOT need new direct detector calls for that element
