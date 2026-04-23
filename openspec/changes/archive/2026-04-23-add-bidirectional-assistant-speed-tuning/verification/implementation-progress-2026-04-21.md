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

- none

Pending OpenSpec checkpoint closure:

- none

## Practical Status

Actual status on 2026-04-21:

- implementation progress: `17 / 17` task items complete
- blocked completion reason: none in the current task list
- user-visible feature status: bidirectional speed tuning workflow is implemented, builds successfully, and all current OpenSpec checkpoints now have authoritative verifier closure

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

- `2.4` and `2.5` are now complete
  - authoritative artifacts live under `verification/checkpoint-2/`
  - `attempt-1` remained blocked on missing real board/runtime evidence and surfaced the already-fixed seq-bookkeeping clear bug
  - new real board/runtime evidence captured in this session:
    - `new/verification/phase-d-speed-tuning-disconnect-clear-host.log`
    - `new/verification/phase-d-speed-tuning-disconnect-clear.csv`
    - `new/verification/phase-d-speed-tuning-disconnect-clear-board.log`
    - `new/verification/phase-d-speed-tuning-failsafe-latched-attempt2-host.log`
    - `new/verification/phase-d-speed-tuning-failsafe-latched-attempt2.csv`
    - `new/verification/phase-d-speed-tuning-failsafe-latched-attempt2-board.log`
    - `new/verification/phase-d-speed-tuning-turn-suppressed-board-host.log`
    - `new/verification/phase-d-speed-tuning-turn-suppressed-board.csv`
    - `new/verification/phase-d-speed-tuning-turn-suppressed-board.log`
  - the new board artifacts now prove:
    - assistant disconnect clears the active tuning snapshot on the real board path
    - remote `stop` rejection while `FAIL_SAFE_LATCHED`
    - remote `start` rejection while `FAIL_SAFE_LATCHED`
    - fail-safe authority remains dominant while tuning mode and override remain enabled
    - turn suppression on the real board path zeroes only applied turn while raw turn remains observable
  - `attempt-2` passed and `verification_cycle_guard.py` passed for `checkpoint-2`

- `4.1`, `4.2`, and `4.3` are now complete
  - authoritative artifacts live under `verification/checkpoint-4/`
  - `attempt-1` passed as the final source-first implementation bundle review
  - the review explicitly rechecked:
    - `checkpoint-1/attempt-2` pass artifacts for the command-boundary surface
    - `checkpoint-3/attempt-2` pass artifacts for host-tooling/runbook scope
    - `checkpoint-2/attempt-1` as an open focused-runtime checkpoint with its local source fix already applied
  - `verification_cycle_guard.py` passed for `checkpoint-4`
