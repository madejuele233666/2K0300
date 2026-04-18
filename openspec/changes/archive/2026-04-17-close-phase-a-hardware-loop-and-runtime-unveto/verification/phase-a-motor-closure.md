# Phase A Motor Closure

- Evidence log: `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-motor-closure.log`
- Supporting bench evidence:
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-bench-pwm-left.log`
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-bench-pwm-right.log`
- Board run date: `2026-04-17`
- Result: `closed_for_safe_pulse`

## What Changed In Code

1. Logical left/right motor routing is now localized in the true LS2K0300 bridge instead of being assumed from vendor device numbering.
2. The bridge keeps rollback and emergency-stop fail-safe handling inside the platform-owned boundary, and now clears PWM before any GPIO direction update so sign flips stay fail-safe at the bridge edge.
3. Runtime evidence now exposes `control.apply.command` so board logs show the project-owned left/right command actually sent.

## What The Run Proves

1. The runtime leaves the initial veto interval and repeatedly records:
   - `control.apply.drive`
   - `control.apply.command`
   - `control.arm.transition`
2. The logical command surface is no longer ambiguous: board logs now state left/right PWM in project-owned terms.

## What The Run Does Not Yet Prove

1. Physical actuator closure is still not proven. In the same run, encoder summaries remained zero while non-zero drive commands were being applied.
2. In the current board context, that result is inconclusive rather than self-proving a bad motor path: the chassis may have been fully static, the wheel path may not have been free to move, or board-side actuator enable conditions may still have been absent.
3. Follow-on bench PWM pulse tests now show that a short logical-left pulse only moves the left-side encoder response, and a short logical-right pulse only moves the right-side encoder response.
4. That means the current evidence now proves “motor write path accepted logical drive commands and safe pulse output reaches the expected wheel-side encoder path”.
5. What is still missing is sustained direction/magnitude validation under a longer controlled motion context.

## Stage Impact

- Software-side motor apply and fail-safe semantics are clearer and better localized.
- Safe bench pulse actuator closure is now confirmed.
- The earlier encoder/control-trust blocker referenced by this note was closed by the later evidence bundle in `phase-a-encoder-closure.md` and `phase-a-exit-judgment.md`.
