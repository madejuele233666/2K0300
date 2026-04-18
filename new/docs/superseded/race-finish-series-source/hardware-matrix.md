# Hardware Matrix (Phase 1)

The active hardware contract is declared in `new/config/hardware_profile.json`.

## Direct-match profile (default)

| Subsystem | Mode | Hook | Runtime behavior |
|---|---|---|---|
| camera | `direct-match` | `phase1-160x120-to-160x128-no-exp-light-control` | Accept `160x120`, enforce deterministic adaptation to legacy `160x128`, and fail closed for non-default `exp_light` because the true public API does not expose direct exposure control. |
| imu | `direct-match` | `imu-core-device-detect` | Detect `imu_type` through the true IMU core path and normalize device-specific globals into one project-owned sample. |
| encoder | `direct-match` | `absolute-count-delta-baseline` | Read absolute counts, establish a baseline on first sample, then derive deltas without assuming vendor-side clear/reset primitives. |
| motor | `direct-match` | `pwm-gpio-free-function` | Apply signed PWM with explicit direction GPIO through vendor free functions hidden behind the bridge slice. |
| timer | `direct-match` | `pit-timer-bridge` | Periodic callback through the true LS2K0300 PIT bridge with project-owned stop/shutdown control. |
| persistence | `direct-match` | `json-file-store` | File-backed parameters and startup-critical checks. `adaptation-hook` is not implemented in phase 1 and is fail-safe rejected. |
| display | `disabled` | `phase1-deferred` | Out of phase-1 runtime path. |

## Adaptation-hook example

When a subsystem differs from source hardware, switch to `adaptation-hook` and provide a named hook.

Example:

```json
"camera": { "mode": "adaptation-hook", "hook": "camera-geometry-320x240" }
```

Expected behavior:

- adapter emits explicit adaptation marker
- IMU/encoder hook reads are marked unavailable so control loop enters veto
- motor hook apply path suppresses direct actuator output and reports fail-safe marker

Silent fallback is forbidden.

Phase-1 startup policy:

- default startup is strict fail-closed and requires direct-match for camera/imu/encoder/motor
- adaptation-hook for those critical subsystems is only allowed when `LS2K_ALLOW_DEGRADED_STARTUP=1`
- degraded startup is diagnostics-only and is not considered a normal control-ready deployment
- in degraded mode with `motor=disabled`, control loop may run for observability but actuator arming remains false

## Fail-Closed Profile Contract

- `hardware_profile.json` is mandatory in phase 1.
- Missing file, invalid JSON, unknown mode, or missing subsystem block now fails closed before runtime startup.
- Critical profile loading no longer falls back to implicit direct-match assumptions.
- Parameter loading is gated by profile: persistence must be `direct-match`.
- Low-voltage sensing is routed through `IPowerMonitorAdapter` in the platform layer; runtime no longer reads vendor ADC directly.

## Compatibility evidence

During verification, capture at least:

- one direct-match run
- one adaptation-hook run
- low-voltage startup marker (`startup.low_voltage.raw`) and emergency assertion behavior when forced
- runtime low-voltage transition behavior (`power.low_voltage.transition`)
- resulting markers and compatibility decisions in:
  - `openspec/changes/retarget-port-to-true-ls2k0300-library/verification/hardware-profile.log`
