# Phase A Control Unveto

- Evidence log: `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-control-unveto.log`
- Board run date: `2026-04-17`
- Result: `closed_for_runtime_observability`

## Dominant Blocker Classification

The captured run shows a short startup veto interval dominated by `perception_stale`:

1. `control.veto.perception_stale`
2. `control.veto.sustained` after roughly `202 ms`
3. `control.veto.interval.closed` after roughly `315 ms`

After that startup window, the runtime records:

1. `control.apply.drive`
2. `control.apply.command`
3. `control.arm.transition`
4. `control.unveto.sustained` after roughly `205 ms`

## Interpretation

1. The runtime no longer treats veto as the steady-state outcome of the default direct-match profile.
2. The dominant veto cause in this run is startup freshness, not `perception_emergency`, low voltage, IMU invalidity, or encoder invalidity.
3. Because `perception_emergency` and exposure-related markers are not the dominant blocker profile here, exposure remains a downstream Phase A decision rather than an immediately promoted control-unlock prerequisite.

## Limits

1. This note closes the runtime-owned observability question.
2. At the time of capture, the remaining sensor and actuator trust questions were still tracked in the IMU, encoder, and motor notes.
3. Those remaining Phase A blockers were later closed by the final IMU and encoder evidence bundle; this note should now be read as the control-unlock component of that closure.
4. The current non-veto evidence was gathered in a static board context, so it should not be over-read as proof of wheel motion.
