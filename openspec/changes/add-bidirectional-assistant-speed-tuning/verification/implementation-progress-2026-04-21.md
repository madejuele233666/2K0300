# Implementation Progress Audit (2026-04-21)

This note records actual implementation progress for `add-bidirectional-assistant-speed-tuning` by comparing the current code and preserved runtime evidence against `tasks.md`.

## Completed Implementation Scope

The following task groups are implemented in code today:

- `1.1` inbound command contract and JSON-line decoder
- `1.2` transport-to-decoder-to-runtime receive path with `input_rejected`
- `1.3` runtime-owned ACK/state feedback with `accepted`/`rejected`, `override_cleared`, `snapshot_cleared`, `null` override encoding, and phase-based `effective_speed_target`
- `2.1` dedicated runtime tuning snapshot with TTL expiry and disconnect / disable clear paths
- `2.2` remote `start` / `stop` routed through `motion_intent`
- `2.3` override-driven running target selection plus tuning-only applied-turn suppression
- `3.1` structured telemetry for left/right targets, measured speed, PWM, raw/applied turn, override state, and phase
- `3.2` host tuning workflow in `new/user/tune_speed.py`
- `3.3` project-owned wrapper and runbook coverage
- `3.4` preserved accepted end-to-end host workflow evidence

## Code Evidence

Primary implementation evidence:

- `new/code/platform/assistant_protocol.cpp`
  - typed command decode for `start`, `stop`, `enable_tuning_mode`, `disable_tuning_mode`, `set_turn_suppressed`, and `set_target_speed`
  - `seq` handling, target-speed validation, TTL validation, ACK/state/telemetry JSON encoding
- `new/code/platform/assistant_link.cpp`
  - assistant TCP receive bytes buffered into project-owned JSON-line decode
- `new/code/runtime/tuning_state.cpp`
  - dedicated runtime tuning state, TTL expiry, full snapshot clear on disconnect / disable
- `new/code/runtime/assistant_service.cpp`
  - command handling, `input_rejected`, ACK publishing, `override_cleared`, `snapshot_cleared`
  - remote `start` / `stop` routed into `motion_intent`
- `new/code/runtime/control_loop.cpp`
  - lifecycle-owned running-speed selection from volatile override
  - raw turn preserved while tuning-only suppression zeros only `applied_turn_output`
- `new/user/tune_speed.py`
  - command send support, live plotting fallback, CSV capture, ACK/state parsing
- `new/user/debug.sh`
  - project-owned wrapper entry point for the host tuning workflow

## Preserved Runtime Evidence

Accepted host/board evidence already present in the repo:

- `new/verification/phase-d-speed-tuning-host-stable.log`
  - rejected `set_target_speed` while tuning mode is off
  - rejected `set_turn_suppressed` while tuning mode is off
  - accepted `enable_tuning_mode`, `start`, target-speed steps, `stop`, `disable_tuning_mode`
  - rejected invalid target speed above `Speed_base`
  - `override_cleared`
  - `snapshot_cleared`
- `new/verification/phase-d-speed-tuning-stable.csv`
  - 286 telemetry frames
  - one `override_cleared` state frame
  - one `snapshot_cleared` state frame
  - accepted and rejected ACK coverage for the accepted workflow
- `new/verification/phase-d-speed-tuning-board-stable.log`
  - board-side `assistant.command.rejected` diagnostics for disabled-mode and invalid-target-speed cases
  - `assistant.motion.start.requested`
  - `assistant.motion.stop.requested`
  - `assistant.override.cleared`
- `new/verification/phase-b-assistant-*.log`
  - `motion.start.blocked` evidence while gate reasons remain active
- `new/verification/phase-b-b1-manual-lifecycle.log`
  - `motion.failsafe.latched` evidence for the accepted lifecycle owner

## Remaining Gaps Before Full Completion

The change is not fully closed yet.

Pending implementation-adjacent verification:

- `2.4`
  - still missing a preserved run that explicitly proves assistant disconnect clears tuning state during an active tuning session
  - still missing preserved evidence for remote `start` rejection while already in `FAIL_SAFE_LATCHED`
  - still missing preserved evidence for remote `stop` rejection while already in `FAIL_SAFE_LATCHED`
  - still missing a preserved run showing fail-safe authority remains dominant while tuning mode is enabled

Pending OpenSpec checkpoint closure:

- `2.5`
  - `verification/checkpoint-2/attempt-1/` exists but remains blocked on missing focused runtime/board evidence for the checkpoint-2 scope
  - the local correctness issue found in that review is already fixed in source, but the checkpoint itself is still not closed

## Practical Status

Actual status on 2026-04-21:

- implementation progress: `15 / 17` task items complete
- blocked completion reason: targeted runtime edge-case evidence for `2.4` is still missing, which keeps `2.5` open
- user-visible feature status: bidirectional speed tuning workflow is usable, builds successfully, and all currently pursued local verify checkpoints except the focused runtime-evidence checkpoint are closed

## 2026-04-21 Local Verify Follow-Up

Additional local verification progress completed after the initial audit:

- `1.4` is now complete
  - authoritative artifacts live under `verification/checkpoint-1/`
  - `attempt-1` found two real local correctness issues:
    - same-poll disconnect bytes could be decoded after `snapshot_cleared`, allowing a closed transport epoch to mutate runtime state and queue stale feedback
    - `set_turn_suppressed.value` accepted string spellings like `"true"` / `"false"` outside the frozen first-release contract
  - the repair loop fixed both:
    - `new/code/platform/assistant_link.cpp`
      - treats `connection_lost` as a hard session boundary
      - clears buffered inbound bytes on connect/loss boundaries
      - drops bytes from the poll that reports `connection_lost`
    - `new/code/runtime/assistant_service.cpp`
      - clears pending feedback on `connection_lost`
      - skips inbound-message application from a closed transport epoch
    - `new/code/platform/assistant_protocol.cpp`
      - narrows `set_turn_suppressed.value` parsing to the frozen JSON-boolean representation
  - `attempt-2` passed and `verification_cycle_guard.py` passed for `checkpoint-1`

- `3.5` is now complete
  - authoritative artifacts live under `verification/checkpoint-3/`
  - `attempt-1` recorded an evidence/runbook block
  - the repair loop added:
    - `new/user/simulate_assistant_peer.py`
    - `new/verification/phase-d-speed-tuning-local-sim.csv`
    - `new/verification/phase-d-speed-tuning-local-sim-host.log`
    - `new/verification/phase-d-speed-tuning-local-sim-peer.log`
    - updated `new/verification/phase-d-speed-tuning.md`
  - `attempt-2` passed and `verification_cycle_guard.py` passed for `checkpoint-3`

- `2.5` was partially advanced but remains blocked
  - a source-first verifier review now exists under `verification/checkpoint-2/attempt-1/`
  - blocker summary:
    - missing real board/runtime tuning-session evidence for the checkpoint-2 scenarios
    - simulator-only artifacts are not authoritative runtime-correctness proof for checkpoint-2
  - one local correctness issue from that review was fixed anyway:
    - `new/code/runtime/tuning_state.cpp` now clears `has_last_seq` and `last_seq` inside `ClearRuntimeTuningSnapshot()`
  - `cmake --build new/out -j2` passed after that fix

- `4.1`, `4.2`, and `4.3` are now complete
  - authoritative artifacts live under `verification/checkpoint-4/`
  - `attempt-1` passed as the final source-first implementation bundle review
  - the review explicitly rechecked:
    - `checkpoint-1/attempt-2` pass artifacts for the command-boundary surface
    - `checkpoint-3/attempt-2` pass artifacts for host-tooling/runbook scope
    - `checkpoint-2/attempt-1` as an open focused-runtime checkpoint with its local source fix already applied
  - `verification_cycle_guard.py` passed for `checkpoint-4`
