# Temporary Wheel PID From Scratch

## Purpose

This note defines a temporary from-scratch wheel PID workflow for this repository.
It is not the default tuning policy. Use it only when the user explicitly asks to restart tuning from scratch or to discard the previous search path.

Reasons to propose this workflow to the user:

- the previous tuning state is untrusted
- battery / wheel / suspension / filter conditions changed
- repeated scans suggest the search got trapped around an over-aggressive `P`

Those bullets are not automatic triggers. Even when they apply, this document remains an opt-in recovery workflow and requires explicit user approval before use.

## Fixed Defaults

Unless the user explicitly overrides them, start with these runtime conditions:

- `Speed_base = 150`
- tuning sequence: `75,105,135`
- tuning runs: `2`
- `ttl-ms = 2500`
- `step-dwell-ms = 1200`
- `startup-delay-ms = 600`
- `command-gap-ms = 500`
- `terminal_steady_error_band = 20`
- `terminal_pwm_jump_band = 800`
- `prohibit_reverse_pwm = 0`
- `LEFT_WHEEL_PID.I = 0`
- `LEFT_WHEEL_PID.D = 0`
- `RIGHT_WHEEL_PID.I = 0`
- `RIGHT_WHEEL_PID.D = 0`
- `LEFT_WHEEL_PID.INTEGRAL_LIMIT = 2200`
- `RIGHT_WHEEL_PID.INTEGRAL_LIMIT = 2200`
- initial `LEFT_WHEEL_PID.MEASUREMENT_FILTER_ALPHA = 0.4`
- initial `RIGHT_WHEEL_PID.MEASUREMENT_FILTER_ALPHA = 0.4`

If later evidence shows one wheel needs a different filter alpha, record the split explicitly and continue.

## Ground Rules

1. Stop any managed runtime before bench or tuning:

```bash
(cd new/user && ./debug.sh remote stop)
```

2. Keep transport stable before tuning:

```bash
bash new/verification/tests/run_assistant_service_publish_policy_test.sh
```

3. Do not overwrite `new/config/default_params.json` during exploration.
4. Write all working params under `new/verification/params/`.
5. Preserve every run's CSV, host log, board log, and round summary.

## Phase 0: Create A Working Baseline

Start from `new/config/default_params.json`.
Do not seed the temporary from-scratch workflow from a previous accepted-best file or a working snapshot unless the user explicitly asks for that.

Copy `new/config/default_params.json` into a working file and set the fixed defaults above.

Recommended path:

```text
new/verification/params/auto-wheel-pid-working-<timestamp>.json
```

From this point onward:

- auto-search only `LEFT_WHEEL_PID.{P,I,D}`
- auto-search only `RIGHT_WHEEL_PID.{P,I,D}`
- if filter changes are needed, adjust `LEFT_WHEEL_PID.MEASUREMENT_FILTER_ALPHA` and `RIGHT_WHEEL_PID.MEASUREMENT_FILTER_ALPHA` manually between phases and record the reason
- leave `Speed_base`, `wheel_turn_target_scale`, and turn PID alone unless the user explicitly asks

## Phase 1: Open-Loop Wheel Bench

Before closed-loop PID search, map each wheel's rough PWM-to-encoder response.

Use the project-owned bench entrypoint:

```bash
(cd new/user && ./debug.sh bench calibrate \
  --side both \
  --sequence 600,900,1200,1500 \
  --repeats 3 \
  --pulse-ms 180 \
  --settle-ms 80 \
  --cooldown-ms 250 \
  --csv ../verification/manual-probes/<timestamp>-open-loop-both.csv)
```

What to extract:

- the first PWM value where each wheel produces a reliable encoder delta
- whether one wheel needs materially more PWM than the other
- whether encoder response is noisy or non-monotonic

If one wheel has a larger dead zone or weaker low-PWM response, remember that later static error is not purely a PID issue.

## Phase 2: Initial Filter Check

Do one short closed-loop validation with:

- both wheels at the current low-risk baseline `P`
- `I = 0`
- `D = 0`
- both wheels at `MEASUREMENT_FILTER_ALPHA = 0.4`

Use the accepted board-assisted flow:

```bash
python3 .codex/skills/auto-wheel-pid-tuning/scripts/auto_tune_wheel_pid.py \
  --motor-enabled \
  --params-in new/verification/params/auto-wheel-pid-working-<timestamp>.json \
  --term-groups p \
  --max-rounds 1 \
  --bo-suggestions-per-dimension 0 \
  --fixed-search-step 1 \
  --sequence 75,105,135 \
  --screen-sequence 75,105,135 \
  --sweep-values-left-p <current_left_p> \
  --sweep-values-right-p <current_right_p> \
  --terminal-steady-error-band 20 \
  --terminal-pwm-jump-band 800
```

If the wheel fails mainly by per-segment `pwm_jump` while static error is otherwise moderate, adjust `MEASUREMENT_FILTER_ALPHA` before pushing `P` higher.

## Phase 3: P-Only Stable-Zone Search

This phase finds the stable `P` region. Do **not** use final static-error strictness to pick `P`.

### Important

The current `auto_tune_wheel_pid.py` still treats steady-state error as a hard terminal gate.
That is too strict for the `P-only` phase because `I = 0`.

Therefore, in this phase:

- use the tool for evidence capture
- ignore its `accepted` flag when failure is only due to static error
- inspect `round-summary.json` and, when needed, per-segment breakdown manually

### P-Only acceptance rule

For the `P-only` phase, reject a candidate if repeated runs show any of:

- per-segment `tail_pwm_jump_abs > 800`
- repeated low `attainment_ratio` below about `0.82`
- obvious overshoot / rail-hit / chatter growth compared with lower `P`

Treat static error as advisory here, not final.

### Recommended order

1. coarse scan `P`
2. identify the stable zone before output oscillation starts
3. do one narrow integer scan around the best-looking region
4. repeat-validate the top two or three `P` values

Example coarse sweep:

```bash
python3 .codex/skills/auto-wheel-pid-tuning/scripts/auto_tune_wheel_pid.py \
  --motor-enabled \
  --params-in new/verification/params/auto-wheel-pid-working-<timestamp>.json \
  --params-working new/verification/params/auto-wheel-pid-p-working-<timestamp>.json \
  --params-best new/verification/params/auto-wheel-pid-p-best-<timestamp>.json \
  --term-groups p \
  --max-rounds 1 \
  --bo-suggestions-per-dimension 0 \
  --fixed-search-step 1 \
  --sequence 75,105,135 \
  --screen-sequence 75,105,135 \
  --sweep-values-left-p 40,60,80,100,120,140 \
  --sweep-values-right-p 40,60,80,100,120,140 \
  --terminal-steady-error-band 20 \
  --terminal-pwm-jump-band 800
```

## Phase 4: Add Small D Only To Reduce Tail PWM Jump

Once a wheel has a reasonable `P` zone:

- keep `I = 0`
- scan a small `D` window, for example `0,0.25,0.5,0.75,1.0`

Goal:

- reduce per-segment `tail_pwm_jump_abs`
- reduce overshoot
- do not materially reduce `attainment_ratio`

Reject `D` values that:

- worsen `105` / `135` static error
- reduce `attainment_ratio`
- create new high-speed lag

## Phase 5: Add I Last

Only after `P` and `D` are stable should `I` be introduced.

This phase exists to remove the remaining steady-state bias caused by:

- wheel dead zone
- static friction
- filter lag

Start small and keep the integral limit enabled.

Recommended small scan:

- `0`
- `0.05`
- `0.1`
- `0.2`
- `0.3`

In this phase, static error becomes a hard requirement.

## Phase 6: Final Repeat Validation

For any candidate proposed as "usable", run at least three full repeated validations.

A candidate is only usable if all repeated runs are transport-clean and all target-speed segments pass:

- `tail_mean_abs_error <= 20`
- `tail_pwm_jump_abs <= 800`

Do not accept a wheel based on one good screen round.

## How To Read A Failure

### Pattern A: wheel-level summary looks fine, but terminal gate still fails

Cause:

- one specific speed segment failed
- wheel-level summary is only a weighted aggregate

Action:

- inspect the per-segment scores from the round CSV and params file

### Pattern B: low static error, high PWM jump

Cause:

- `P` too high for the current filter / measurement noise / wheel mechanics

Action:

- reduce `P`, or increase filtering slightly, or add a small `D`

### Pattern C: lower PWM jump, but `attainment_ratio` drops and static error rises

Cause:

- too much filtering or too much `D`

Action:

- back off the filter / `D`; do not keep compensating by raising `P`

### Pattern D: low-speed segment fails repeatedly, especially around `75`

Cause:

- likely wheel dead zone / friction / weak low-speed authority

Action:

- use the open-loop bench result to confirm the wheel's low-speed baseline
- do not use `P-only` to brute-force the missing low-speed authority

## Minimum Artifact Set

For each from-scratch tuning session, preserve:

- the working params file used to start the session
- the open-loop bench CSV
- every closed-loop round CSV
- every host log
- every board log
- every round summary JSON
- one short operator note that states what was accepted and why

## Current Tooling Limitation

`auto_tune_wheel_pid.py` is good at evidence capture and repeatable deployment, but its final gate is still aligned to the integrated score.

During the `P-only` phase:

- do not blindly trust `accepted=false`
- inspect per-segment reasons manually
- only let strict static-error gating dominate after `I` is enabled
