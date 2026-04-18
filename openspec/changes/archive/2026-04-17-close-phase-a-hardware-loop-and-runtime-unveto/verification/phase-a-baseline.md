# Phase A Baseline

- Change: `close-phase-a-hardware-loop-and-runtime-unveto`
- Baseline freeze date: `2026-04-17`
- Accepted startup-grade baseline being inherited:
  - `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.log`
  - `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.exit`
  - `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/hardware-discovery-retry-2026-04-15.log`
  - `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/runtime-smoke-2026-04-15.md`

## Accepted Facts

The accepted `2026-04-15` bundle already proves startup-grade facts:

1. Default `direct-match` startup reaches `imu.init`, `imu.detect`, `startup.complete`, and `control.start`.
2. Camera, ADC, IMU, encoder, motor, and timer direct-match resources are named in board evidence.
3. The runtime can emit project-owned control markers such as `control.veto.*`, `control.apply.*`, `control.command.requested_nonzero`, and `control.arm.transition`.
4. Public actuator contract is already reduced to `left_pwm + right_pwm + emergency_stop`.

## Facts Not Accepted By The Baseline

The accepted startup-grade bundle does not prove:

1. IMU static-sample magnitude, bias interpretation, or axis-direction closure.
2. Encoder direction and speed magnitude trust for control.
3. Physical actuator-path closure under non-zero drive command.
4. Stable control unlock beyond startup freshness transients.
5. Whether non-default camera exposure is truly supported on the direct-match path.

## Phase A Blockers At Entry

At the start of this change, the remaining Phase A blockers were:

1. IMU sample closure.
2. Encoder trust closure.
3. Motor-path physical closure.
4. Runtime unveto evidence.
5. Camera exposure-policy decision.
