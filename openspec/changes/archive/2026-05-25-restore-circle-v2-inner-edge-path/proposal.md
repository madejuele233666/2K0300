## Why

Circle V2 `InnerTrace` currently follows the V3 fixed-slope entrance boundary override, which can produce a path by modifying ordinary row intervals instead of directly following the inner circle edge. We need to return `InnerTrace` path ownership to the circle scene: observe the locked-side inner edge and emit a scene-owned path that can run close to that edge.

## What Changes

- Replace the active V3 `P_est + fixed_slope` boundary-override `InnerTrace` path with a direct inner-edge `CircleV2ReferencePlan`.
- Keep the existing scene split: event observer decides transitions, geometry observer extracts role-specific geometry, composer emits scene-owned reference intent, and adapter only packages accepted plans.
- Move the current P-point boundary-override implementation into `new/code/archive/` as historical reference code; active runtime and tests must not include it.
- Remove CircleV2 runtime dependence on `BuildReferencePathWithBoundaryOverride()` for active `InnerTrace` candidates.
- Keep `ExitTrace`, FSM transitions, yaw-stall fallback, and CircleV2 telemetry behavior unchanged.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `sparse-circle-v2-scene`: changes `InnerTrace` reference generation from V3 fixed-slope entrance boundary override back to direct inner-edge scene path generation.

## Risk Tier

- `STANDARD`: this changes runtime perception/reference behavior for CircleV2 `InnerTrace` and removes an active reference-building path, but it does not alter hardware adapters, safety gate semantics, motor control, or external assistant command protocol.

## Impact

- `port/`: CircleV2 reference-plan types and telemetry remain scene-owned; obsolete boundary-override active types may be removed or isolated.
- `runtime/`: CircleV2 geometry/composer/adapter and frame perception pipeline stop using boundary overrides for active `InnerTrace`.
- `config/`: V3 fixed-slope parameters become inactive for CircleV2 path generation and should be removed from active defaults/docs if no active user remains.
- `new/code/archive/`: current P-point boundary-override code is retained as historical implementation context only.
- `new/verification/tests/`: CircleV2 scene and reference-orchestration tests are updated to assert inner-edge path behavior and absence of active P-point override use.
