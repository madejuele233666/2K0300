# Phase A Encoder Closure

- Evidence logs:
  - `openspec/changes/archive/2026-04-17-close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-encoder-closure.log`
  - `openspec/changes/archive/2026-04-17-close-phase-a-hardware-loop-and-runtime-unveto/verification/runtime_smoke_motor_lowrisk_20260417.log`
- Supporting bench evidence:
  - `openspec/changes/archive/2026-04-17-close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-bench-pwm-left.log`
  - `openspec/changes/archive/2026-04-17-close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-bench-pwm-right.log`
- Board run date: `2026-04-17`
- Result: `closed`

## What Changed In Code

1. The bridge now keeps full-width encoder counts instead of narrowing them before the adapter sees them.
2. The adapter normalizes logical wheel direction at the owning boundary:
   - logical left uses raw left sign
   - logical right uses inverted raw right sign, matching the inherited legacy speed-loop expectation
3. Runtime-facing evidence now uses `encoder.delta.summary` rather than raw device-node interpretation.

## What The Run Proves

1. Encoder direct-match startup remains healthy.
2. Baseline establishment is explicit through `encoder.baseline`.
3. No implausible jump-reset storm was observed in this run.
4. Follow-on bench PWM pulse tests confirm side-local routing:
   - logical left pulse produced `after1_left=-8`, `after2_left=8`
   - logical right pulse produced `after1_right=4`, `after2_right=-4`
5. Low-risk motor-enabled runtime evidence closes the remaining control-trust gap for Phase A:
   - `runtime_smoke_motor_lowrisk_20260417.log` applies `logical drive command left_pwm=2500 right_pwm=-2500`
   - the next logical encoder sample reports `left=435 right=-418 mean=8 diff=-853`
   - the encoder sign therefore matches the commanded logical motion, and the left/right magnitudes stay in the same order rather than collapsing to zero or flipping unpredictably

## Scope Note

1. This closes encoder trust at Phase A scope: sign, reachability, and runtime visibility are now interpretable at the control boundary.
2. It does not claim that full speed calibration or final vehicle-motion tuning is done; those belong to Phase B and later.

## Stage Impact

- “Encoder can read” is still true.
- “Encoder is side-responsive and no longer opaque” is now true.
- “Encoder is trusted for Phase A control-unlock evidence” is now true.
- Encoder is no longer a Phase A exit blocker.
