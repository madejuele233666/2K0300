## Why

Circle V2 currently derives `InnerTrace` reference geometry from the visible locked-direction inner edge. Entrance frames can expose too little inner-edge geometry, causing missing or insufficient circle reference plans before the car has naturally entered the roundabout.

## What Changes

- Add a V3 entrance guide inside `CircleV2Scene`: estimate the locked-direction outer entrance corner `P_est`, construct a virtual opposite boundary through `P_est` using a direction-specific fixed slope, and offset that virtual boundary by road half width to produce the `InnerTrace` reference.
- Reuse one internal locked-side expansion observation for Phase1 circle cue, Approach entry gate, and `P_est` estimation.
- Add runtime parameters for left/right fixed entrance slopes in BEV `dx/dy = lateral_m / forward_m` coordinates.
- Keep `CircleV2Reducer`, visual-reference adapter, ordinary road model builder, and visual-reference arbitration unaware of `P_est`, fixed slope, and virtual boundary details.
- Preserve existing `ExitTrace` outer-edge behavior and Circle V2 lifecycle semantics.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `sparse-circle-v2-scene`: `InnerTrace` reference construction changes from visible inner-edge following to a V3 fixed-slope entrance guide, and Circle V2 gains explicit entry-guide slope parameters.

## Risk Tier

- `STANDARD`: This changes runtime steering reference generation and runtime parameter parsing in `port`, `platform`, `runtime`, `config`, and verification tests. It does not add a new platform driver or external dependency, but it affects on-track vehicle behavior and therefore needs spec, design, host tests, verifier review, and board smoke-test planning.

## Impact

- Affected layers:
  - `port`: Circle V2 and BEV element parameter structs.
  - `platform`: runtime parameter parsing, validation, and media parameter snapshots.
  - `runtime`: CircleV2 event/geometry/composer internals and perception pipeline parameter mapping.
  - `config`: default runtime JSON and parameter documentation.
  - `verification`: CircleV2 scene tests and parameter tests.
- OpenSpec skills used: propose/apply/verify/sync/archive lifecycle with source-first implementation verification.
