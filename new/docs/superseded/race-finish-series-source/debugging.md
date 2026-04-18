# Debugging And Runtime Contract

## Runtime diagnostics

All diagnostics flow through `port::DiagnosticSink` and include:

- level (`INFO`, `WARN`, `ERROR`, `FAIL_SAFE`)
- stable code (for grep in smoke logs)
- monotonic timestamp

Key markers expected during smoke:

- `startup.complete`
- `params.critical.apply`
- `startup.low_voltage.raw` and optionally `startup.low_voltage.emergency`
- `startup.mode.degraded` only when `LS2K_ALLOW_DEGRADED_STARTUP=1`
- `startup.profile.timer.hook` when timer uses an adaptation hook
- `imu.detect`
- `profile.*`
- `control.start`
- `control.start.degraded.motor_disabled` only when degraded startup keeps loop alive with motor profile disabled
- `control.arm.motor_disabled` when motor profile is disabled and actuators stay disarmed
- `control.veto` (only during emergency/stale/invalid inputs)
- `encoder.baseline` on first absolute-count sample after startup or reset
- `encoder.delta.jump` when implausible absolute-count jumps force a delta-baseline reset
- `power.low_voltage.transition` when runtime low-voltage state changes after startup
- `shutdown.complete`
- `main.frame_limit` only when smoke or a bounded test explicitly sets `LS2K_MAX_FRAMES`

## Camera adaptation logs

Accepted phase-1 path:

- Marker: `kPhase1Adapted`
- Contract: `160x120` source -> destination rows `8..127`, rows `0..7` duplicated from source row `0`.
- Limitation: direct-match camera path does not apply non-default `exp_light` through the true vendor public API; use an adaptation hook or degraded diagnostics-only startup when exposure control is required.

Non-phase-1 path:

- Marker: `kNonPhase1Geometry` or `kAdaptationHookRouted`
- Expected behavior: frame is vetoed and control path stays fail-safe.

Adapter hook markers for non-camera subsystems:

- `imu.hook.read`
- `encoder.hook.read`
- `motor.hook.apply`

Phase-1 persistence note:

- persistence must remain `direct-match`
- persistence adaptation-hook is rejected before runtime startup and is not a valid smoke marker path

Manual geometry test:

```bash
LS2K_FORCE_UVC_GEOMETRY=320x240 ./new
```

## Parameter failure behavior

`ParamStore` logs which path was taken:

- valid file load
- missing file -> documented defaults
- parse failure -> documented defaults + fail-safe marker

Actuator arming is blocked if startup-critical fields are invalid:

- `P_Mode`
- `exp_light`

## Build and smoke flow

1. Build/upload:
   - `cd new/user && ./build.sh`
2. Board-side smoke + log retrieval:
   - `cd new/user && ./run_remote_smoke.sh`
3. Log location:
   - `openspec/changes/retarget-port-to-true-ls2k0300-library/verification/runtime-smoke.log`

If remote execution is unavailable:

- `run_remote_smoke.sh` records remote failure and runs a local fallback executable for diagnosability.
- When host and artifact architectures differ, local execution is skipped with an explicit architecture marker.
- Product runtime is long-running by default. `run_remote_smoke.sh` injects a smoke-only `LS2K_MAX_FRAMES` bound so verification can exit cleanly without changing product semantics.

## Runtime lifetime

- Default runtime behavior is persistent service-style execution.
- `LS2K_MAX_FRAMES` is optional and defaults to unbounded.
- `SIGINT` / `SIGTERM` request graceful shutdown of the foreground loop.

## Diagnostic cadence

- High-frequency fault markers such as `control.veto`, `camera.hook`, `imu.hook.read`, `encoder.hook.read`, and `motor.hook.apply` are rate-limited to avoid distorting runtime cadence during persistent failure or adaptation-hook states.

## Bridge ownership

- Vendor headers, globals, free-function calls, and direct vendor C++ types are confined to `new/code/platform/true_ls2k0300/`.
- Public adapters under `new/code/platform/` consume only project-owned bridge APIs and runtime types.

## Low-voltage emergency chain

Phase-1 low-voltage emergency is adapter-backed and re-sampled at runtime:

- startup/runtime raw marker: `startup.low_voltage.raw`
- startup assertion marker: `startup.low_voltage.emergency`
- runtime transition marker: `power.low_voltage.transition`
- perception path: `low_voltage_veto=true`
- control path: `control.veto` + emergency-stop actuator command

Fail-closed behavior:

- if low-voltage sample is unavailable, runtime forces emergency veto
- strict startup refuses to proceed when startup low-voltage sample is unavailable
- degraded startup (`LS2K_ALLOW_DEGRADED_STARTUP=1`) allows diagnostics-only bring-up while keeping emergency veto asserted
- when degraded startup uses `motor=disabled`, control loop may still start for diagnostics, but `actuators_armed` remains false

Override knobs for verification:

- `LS2K_FORCE_LOW_VOLTAGE=true|false`
- `LS2K_LOW_VOLTAGE_RAW_THRESHOLD=<int>`
- `LS2K_LOW_VOLTAGE_RAW_PATH=<adc raw path>`
- `LS2K_ALLOW_DEGRADED_STARTUP=true|false`

## Phase-1 operator workflow

- Operator edits:
  - `new/config/default_params.json`
  - `new/config/hardware_profile.json`
- Operator runs:
  - `build.sh` (build+upload)
  - `run_remote_smoke.sh` (runtime evidence)
- Operator checks:
  - startup/profile/parameter markers
  - perception publication freshness
  - emergency-veto behavior
