# BEV Simple Reference/Control Contract

## Why

The active steering path has been simplified to image facts and white-point control, but several public names, JSON fields, parameter keys, and planning artifacts still described the removed complex BEV stack. Those leftovers can mislead future implementation work and make archived code look active.

## What Changes

- Keep the active runtime contract as:

  ```text
  frame -> sparse BEV reference facts -> reference usability -> reference curvature
  -> reference-control readiness -> safety gate -> yaw target -> actuator
  ```

- Rename multi-frame reference history to hold-memory terminology.
- Remove obsolete telemetry labels from active protocol outputs.
- Keep reference-control readiness separate from safety vetoes.
- Keep the safety gate as the only owner of low voltage, perception health, stale perception, IMU, and encoder veto.
- Remove public legacy angular-target naming; perception publishes curvature and the control loop computes `yaw_rate_target`.
- Rename the speed and yaw PID parameter surface to `RUNNING_SPEED_TARGET` and `YAW_RATE_PID`.
- Delete the roadblock debug placeholder; future roadblock behavior must return as a BEV road element evidence layer.
- Keep dense BEV and classification images as debug artifacts only.
- Reduce `BEV_GEOMETRY` to forward samples, BEV lateral scan range, and lateral step.
- Keep the archived historical implementation isolated under archive directories only.
- Rewrite docs, tests, and tooling so they describe the simple pipeline and no inactive control surfaces.

## Impact

- Assistant and steering-media JSON consumers must stop expecting the removed telemetry labels.
- Runtime behavior remains the same simple BEV perception/control behavior; this change is cleanup and contract alignment, not a new path-finding algorithm.
