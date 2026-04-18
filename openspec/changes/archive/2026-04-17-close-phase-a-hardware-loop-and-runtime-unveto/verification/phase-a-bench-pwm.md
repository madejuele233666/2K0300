# Phase A Bench PWM Pulse

- Evidence logs:
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-bench-pwm-left.log`
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-bench-pwm-right.log`
- Board run date: `2026-04-17`
- Result: `safe_pulse_response_confirmed`

## Test Shape

This test was intentionally env-gated and isolated from the normal runtime path:

1. It only runs when `LS2K_BENCH_PWM_MS` is explicitly set.
2. It reuses the existing startup and adapter boundaries.
3. It keeps the startup low-voltage fail-safe boundary intact and refuses to pulse if low-voltage emergency is active.
4. It applies a short PWM pulse, reads encoder deltas before and after, then immediately disables motor output and exits.

## Observed Results

### Logical Left Pulse

- Command: `logical_left=1500 logical_right=0 pulse_ms=200`
- Summary:
  - `apply_ok=true`
  - `after1_left=-8`
  - `after2_left=8`
  - right side stayed at `0`

### Logical Right Pulse

- Command: `logical_left=0 logical_right=1500 pulse_ms=200`
- Summary:
  - `apply_ok=true`
  - `after1_right=4`
  - `after2_right=-4`
  - left side stayed at `0`

## Interpretation

1. The bench pulse confirms that logical left/right command routing now reaches the expected side-local encoder path.
2. This closes the earlier ambiguity where static runtime smoke could not distinguish “car is stationary” from “actuator path is dead”.
3. The test is still a short pulse, so by itself it does not replace sustained motion evidence.
4. That later sustained evidence was added in `runtime_smoke_motor_lowrisk_20260417.log` and is now reflected in `phase-a-encoder-closure.md`.
