# Verification-Cycle Principles

This file distills the active rules behind `verification-cycle`.

## Core Idea

The system has one semantic job:

`review_goal=implementation_correctness`

The state machine is centered on agent state and verifier output:

- `active`
- `non_active`
- `closed`
- `verdict=block|pass`

## Non-Negotiable Invariants

1. A usable `active` agent is resumed before any new spawn.
2. If no usable `active` agent exists, spawn one and mark it `active`.
3. `block` keeps the same agent authoritative for repair.
4. Only `block -> pass` may produce `non_active`.
5. `close` or `exit` is observational only.
6. `pass` is valid only with complete and exhaustive coverage.
7. Partial verification requires explicit scope.
8. Termination may use only a valid `active` pass.

## What The Main Process May Not Do

- It may not invent legacy dual-session phases.
- It may not terminate on partial coverage.
- It may not treat a closed agent as resolved.
- It may not substitute its own judgment for verifier output.

## What Stays Mechanical

- `agent-table.json`
- `findings.json`
- `verifier-evidence.json`
- orchestrator input and decision contracts
- `verification_cycle_guard.py`

## Legacy Note

Historical review-loop materials remain archived only:

- `openspec/schemas/modules/archive/review-loop-2026-04-18`
