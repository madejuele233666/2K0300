# Phase A Exit Judgment

- Change: `close-phase-a-hardware-loop-and-runtime-unveto`
- Decision date: `2026-04-17`
- Phase exit: `allowed`
- Entry into `Phase B`: `allowed`

## Consolidated Stage Decision

### Baseline

The accepted `2026-04-15` direct-match baseline remains valid and is the only startup-grade baseline used for this change.

### Sensor Closure

1. IMU startup and runtime continuity remain healthy and explicitly observable.
2. Static IMU bench samples are now named and stable enough to show a stationary pose without yaw drift.
3. The live yaw-direction evidence now makes `gyro_z` sign interpretable for the current control path.
4. Encoder logical normalization is explicit, and low-risk motor-enabled runtime evidence now shows matching command/feedback sign during a sustained non-veto window.

### Actuator Closure

1. Logical left/right motor routing and fail-safe behavior are now localized to the platform-owned boundary.
2. Static runtime smoke alone was inconclusive, but follow-on env-gated bench PWM pulse tests now confirm that both logical sides produce side-local encoder response.
3. Safe bench actuator closure is therefore no longer the active blocker.

### Control Unlock

1. Runtime control unlock is no longer blocked by an unexplained permanent veto.
2. The dominant veto cause in the captured run is the expected startup `perception_stale` window.
3. The runtime then enters a sustained non-veto interval and applies non-zero logical drive commands.

### Exposure Decision

1. Direct-match exposure control beyond the default `exp_light=65` is unsupported in Phase A.
2. That policy is now explicit and bounded.
3. Exposure was not the dominant blocker in the current control-unlock evidence.

## Exit Decision

1. All Phase A exit conditions are now satisfied.
2. The next dominant problem is no longer hardware closure; it is low-speed vehicle-motion behavior in Phase B.
3. Safe diagnostics-only smoke remains the default entry, and real motor validation remains explicit opt-in.
