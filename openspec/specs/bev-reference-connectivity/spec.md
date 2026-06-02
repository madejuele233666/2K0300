# bev-reference-connectivity Specification

## Purpose
Define the central current-frame BEV visual-reference connectivity gate that prevents generated visual paths from joining disconnected white regions across black pixels.

## Requirements
### Requirement: Current-Frame Visual Reference Paths Pass A BEV Connectivity Gate
The runtime SHALL validate every current-frame visual reference candidate path before visual-reference selection. The gate SHALL apply to ordinary line candidates, cross candidates, CircleV2 candidates, and any future current-frame visual candidate that carries a `BEVReferencePath`.

The gate MUST NOT change candidate priority, candidate kind, CircleV2 state, cross evidence, ordinary reference extraction, hold-last state, reference usability, or control safety gates.

#### Scenario: Connected visual candidates may enter selection
- **WHEN** a current-frame visual candidate is present
- **AND** every adjacent leading path segment has no black pixel between its endpoints in the current frame
- **THEN** the runtime SHALL pass that candidate to `SelectVisualReference()`
- **AND** the candidate path MAY be included in public candidate-path debug output

#### Scenario: Black barrier blocks a visual candidate
- **WHEN** a current-frame visual candidate is present
- **AND** any adjacent leading path segment crosses a pixel classified as black in the current frame
- **THEN** the runtime SHALL NOT pass that candidate to `SelectVisualReference()`
- **AND** the gate SHALL NOT reset CircleV2 memory, fabricate a hold reference, or alter element evidence

#### Scenario: Hold-last is not a current-frame visual candidate
- **WHEN** current-frame visual selection produces no usable reference
- **THEN** the existing reference-continuity layer MAY still evaluate hold-last according to its own contract
- **AND** the BEV connectivity gate SHALL NOT be applied to `BuildReferenceHoldCandidate()`

### Requirement: Connectivity Uses Zero-Copy Grayscale Frame Access
The connectivity helper SHALL read the current grayscale frame through a non-owning `LegacyCameraFrameView` and SHALL project path endpoints through the existing `BEVProjector`. It SHALL NOT allocate or copy a dense BEV classification raster, SHALL NOT copy the grayscale frame, and SHALL NOT read debug-only `BEVSimpleImage.classes` as control input.

#### Scenario: Segment classification reuses sparse row pixel semantics
- **WHEN** the helper checks two adjacent BEV path points
- **THEN** it SHALL project both points to image coordinates
- **AND** it SHALL traverse the image-space line segment between them
- **AND** traversal SHALL cover every image pixel touched by that segment using a supercover or equivalent line traversal
- **AND** traversal SHALL NOT rely on sparse interpolation points that can skip a diagonal or endpoint black pixel
- **AND** it SHALL read pixels directly from `gray[row * stride + col]`
- **AND** it SHALL classify each pixel with the same threshold and `BEV_CLASSIFICATION` semantics used by sparse row scanning

#### Scenario: Endpoint and diagonal black pixels cannot be skipped
- **WHEN** a projected path segment starts on a black pixel, ends on a black pixel, or crosses a diagonal black pixel between endpoints
- **THEN** the helper SHALL report the segment blocked
- **AND** the candidate containing that segment SHALL be withheld from visual-reference selection

#### Scenario: Only black is blocking in V5
- **WHEN** a projected segment covers white, unknown, invalid, out-of-frame, or projection-failed pixels
- **THEN** V5 SHALL NOT treat those facts as a black barrier
- **AND** only a pixel classified as black SHALL block the segment

### Requirement: Connectivity Helper Has Neutral Ownership
The connectivity helper SHALL be a reusable path helper with no scene, element, arbitration, or control ownership. It SHALL consume only the current frame view, BEV projector, threshold, classification parameters, and a BEV reference path.

#### Scenario: Helper API is independent of candidate semantics
- **WHEN** ordinary, cross, CircleV2, or another generator creates a visual candidate
- **THEN** the generator SHALL NOT need to know how connectivity is checked
- **AND** the connectivity helper SHALL NOT receive circle direction, cross evidence, candidate priority, confidence policy, FSM phase, or reference-control state
