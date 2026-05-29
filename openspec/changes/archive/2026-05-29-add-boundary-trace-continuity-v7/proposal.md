## Why

Current ordinary BEV reference generation can associate different physical edges across adjacent sparse rows before single-boundary offset. When that boundary fact jumps, the normal-offset helper amplifies the false local slope into meter-scale path samples that are already present in `visual_reference.path_candidates`.

## What Changes

- Add an explicit `BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M` parameter as the sole distance source for boundary-trace continuity clipping.
- Add a neutral boundary-trace continuity helper that consumes raw BEV boundary points and returns the kept point prefix/order after deleting outlier points one at a time.
- Apply the clipped boundary facts before ordinary midpoint and single-boundary candidate generation so one clipped side naturally degrades to single-edge semantics and two clipped sides produce no row candidate.
- Publish the explicit boundary-trace distance in the steering media config snapshot so captured evidence explains the loaded boundary association rule.
- Keep `BuildSingleBoundaryOffsetReference()` as a pure geometry helper and prevent it from receiving discontinuous ordinary boundary traces.
- Preserve current screen-edge semantics, visual reference arbitration, connectivity gate, hold-last ownership, CircleV2/cross recognition, and control-chain behavior.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `ordinary-bev-reference`: Adds explicit boundary-trace continuity clipping before ordinary midpoint and single-boundary offset candidate generation.
- `steering-tuning-media-observability`: Extends steering media config snapshots with the V7 boundary-trace distance needed to interpret ordinary reference candidate behavior.

## Risk Tier

- `STANDARD`: this changes current-frame ordinary visual reference candidate generation in `legacy/`, adds a BEV geometry parameter in `port/`, `platform/`, and `config/`, and can change selected visual reference facts before steering control. It does not change platform drivers, motor control, safety-gate semantics, visual reference arbitration priority, CircleV2 FSM transitions, cross recognition, or the single-boundary offset helper's geometry contract.

## Impact

- `port/`: extend `BEVGeometryParameters` with the boundary-trace continuity distance.
- `platform/`: parse, validate, and publish the new BEV geometry parameter in config snapshots.
- `legacy/`: add a neutral boundary-trace clipping helper and apply clipped edge facts in ordinary BEV reference candidate generation.
- `config/`: add default JSON and human-readable parameter documentation.
- `new/docs/`: keep `path-evaluation-boundary-continuity-v7.zh-CN.md` as the design discussion record.
- `new/verification/tests/`: add helper, ordinary candidate degradation, parameter parsing/default, and media/config snapshot coverage.
- Skills/workflow: use `steering-camera-debug` for the reference/camera evidence boundary and OpenSpec `verify-sequence/default` for docs-first and source-first reviews.
