## Why

Current visual reference paths can connect adjacent sparse samples using only geometry continuity. That can join two white regions across a black barrier, while the older lateral-jump gate can also reject legitimate bend or circle paths.

## What Changes

- Add a neutral BEV reference connectivity gate for current-frame visual reference candidates.
- The gate checks adjacent path samples against the current grayscale frame through the existing BEV projector and pixel classifier, without building or copying a dense BEV raster.
- Route ordinary line, cross, CircleV2, and future current-frame visual candidates through the same gate before visual-reference selection.
- Keep hold-last reference continuity outside this gate.
- Add `BEV_GEOMETRY.REFERENCE_LATERAL_JUMP_GATE_M` with a default disabled value of `1000.0`.
- Preserve `BEV_GEOMETRY.SPARSE_ROW_COUNT` as prefix-only sparse-row control with default `24`.

## Capabilities

### New Capabilities

- `bev-reference-connectivity`: Defines the zero-copy current-frame visual reference path connectivity gate and its ownership boundary.

### Modified Capabilities

- `ordinary-bev-reference`: Defines prefix-only sparse row count and the disabled-by-default lateral jump gate parameter.
- `steering-tuning-media-observability`: Requires steering media config snapshots to expose the BEV geometry parameters needed to interpret sparse-row count and lateral jump gate behavior.

## Risk Tier

- `STANDARD`: this changes runtime candidate admission for steering visual references in `legacy/` and `runtime/`, adds BEV geometry parameters in `port/` and `config/`, and affects selected reference behavior before control. It does not change platform drivers, motor control, safety-gate semantics, CircleV2 FSM transitions, cross recognition, or arbitration priorities.

## Impact

- `port/`: extend BEV geometry parameter contracts.
- `legacy/`: add a neutral connectivity helper and point existing lateral jump gates at an explicit parameter.
- `runtime/`: apply the connectivity gate at the current-frame visual candidate aggregation boundary before `SelectVisualReference()`.
- `platform/`: parse and publish the new BEV geometry parameter.
- `config/`: add defaults and tuning documentation.
- `new/verification/tests/`: add focused helper, parameter, media, and pipeline-facing coverage.
- Skills/workflow: use `steering-camera-debug` for the reference/camera boundary and OpenSpec `verify-sequence/default` for docs-first and source-first reviews.
