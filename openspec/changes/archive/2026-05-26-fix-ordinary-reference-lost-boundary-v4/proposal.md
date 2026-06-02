## Why

Ordinary BEV reference generation still treats every selected white interval midpoint as the road center. That is only valid when both interval endpoints are real visible boundaries; when one side disappears, the midpoint can be pulled toward a sampleable-frame or opening boundary and produce a wrong reference before CircleV2 or arbitration ever gets involved.

## What Changes

- Interpret ordinary row intervals by boundary visibility before creating current-frame center samples.
- Add a reusable single-boundary normal-offset helper that consumes only a BEV boundary trace, target forward samples, and a signed normal offset.
- Use the helper for ordinary one-side-lost reference construction while preserving strict leading behavior and existing hold-last ownership.
- Reuse the same helper for CircleV2 role-specific single-boundary path composition, including InnerTrace offset semantics.
- Preserve the composition-layer rule that cross evidence suppresses CircleV2 stepping even when V4 makes the ordinary line candidate unavailable.
- Keep CircleV2 FSM, cross evidence, visual element recognition, candidate arbitration, and safety/control gates out of this change.

## Capabilities

### New Capabilities
- `ordinary-bev-reference`: Defines ordinary sparse BEV reference behavior when interval endpoints are or are not real visible boundaries, plus the reusable single-boundary normal-offset helper contract.

### Modified Capabilities
- `sparse-circle-v2-scene`: CircleV2 single-boundary reference composition reuses the neutral helper instead of carrying a scene-private fixed lateral offset formula.

## Risk Tier

- `STANDARD`: this changes runtime visual-reference geometry for ordinary BEV line following and CircleV2 reference-path composition. It does not change platform adapters, motor control, safety-gate semantics, assistant protocol, or element-recognition FSM transitions, but wrong reference output can directly affect steering.

## Impact

- `port/`: existing BEV point/path and runtime parameter contracts remain unchanged; no new public arbitration type is introduced.
- `legacy/`: ordinary BEV simple perception changes from raw interval-midpoint selection to interpreted boundary/center-candidate selection.
- `runtime/`: CircleV2 geometry/composer code reuses the neutral helper for single-boundary path offsets without changing reducer/event logic.
- `config/`: no new business parameter; existing `BEV_GEOMETRY.nominal_road_half_width_m`, `BEV_GEOMETRY.lateral_step_m`, and CircleV2 path-offset parameters continue to provide caller-owned distances.
- `new/verification/tests/`: add focused helper, ordinary reference, and CircleV2 composition coverage.
- Skills/workflow: use `steering-camera-debug` for the perception/reference boundary and OpenSpec `verify-sequence/default` for docs-first and source-first review.
