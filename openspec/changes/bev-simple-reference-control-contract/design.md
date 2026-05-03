# BEV Simple Reference/Control Contract Design

## Active Authority

The active runtime visual authority is the virtual BEV sparse sample LUT produced from the calibrated projector. Dense BEV and classification images are debug artifacts only. The path layer consumes row-level white intervals from sparse samples and does not inspect archived algorithms or derived scene state.

## Runtime Contract

- `ReferenceHoldState` stores only the last present white-point reference, hold cycle count, and BEV geometry identity.
- `PerceptionFrontend` explicitly composes simple perception, reference continuity, usability, curvature, reference-control readiness, and debug transport.
- `PerceptionResult` is a runtime transport snapshot; it does not own layer decisions and does not publish yaw targets, turn targets, or PWM targets.
- Reference-control readiness only decides whether selected reference facts and curvature are ready for control.
- The safety gate is the only owner of low voltage, perception health, stale perception, IMU, and encoder veto.
- `ControlLoop` calls the yaw controller only after the safety gate and motion supervisor allow drive, then computes `yaw_rate_target` from `curvature_command`, the configured yaw-rate gain, and the current effective speed target.
- Debug snapshots, assistant telemetry, and steering-media headers expose the same minimal steering contract:
  `perception_health.*`, `reference.*`, `eligibility.*`, `curvature.*`, `reference_control.*`, `safety_gate.*`, `degraded.*`, `yaw_control.*`, and `actuator.*`.
- Assistant no longer publishes image frames; images use steering media only.
- `BEV_GEOMETRY` contains only `FORWARD_SAMPLE_*`, `SEARCH_LATERAL_LIMIT_M`, and `LATERAL_STEP_M`.
- Runtime parameters use `RUNNING_SPEED_TARGET` and `YAW_RATE_PID`; old speed and gyro-camera PID names are not aliases.

## Archive Boundary

Historical algorithms remain under `new/code/archive/bev_topology_pre_simple_rewrite` and historical tests remain under `new/verification/archive/bev_topology_pre_simple_rewrite`. Active CMake targets, runtime code, active tests, scripts, and docs must not include or depend on files from those archive directories.

## Verification

The implementation is accepted when the minimal BEV tests, control model test, steering media selftest, host build, diff check, and active residual keyword scan all pass.
