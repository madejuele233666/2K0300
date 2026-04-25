# Verification Cycle Module

## Purpose

This module defines the active verification state machine for implementation
correctness.

Use it when the caller needs an automatic:

- verify
- repair
- resume
- respawn
- terminate

loop over a concrete implementation target.

The active model is a current-state agent baseline, not a historical
dual-session log.

## Core Rules

The cycle is governed by eight rules:

1. If a usable `active` agent exists, continue it first: prefer `send_input`
   while that agent is still open, and if `send_input` reports `agent not
   found`, probe `resume` for that same `active` agent next.
2. If no usable `active` agent exists, spawn one and record it as `active`.
3. When an `active` agent reports `block`, repair until that same agent reports
   `pass`.
4. Only `block -> pass` may mark an agent `non_active`.
5. `continuation_probe` is the only recovery signal for missing or
   non-resumable active agents.
6. `pass` is valid only when `coverage_status=complete` and
   `exhaustive=true`.
7. Termination may use only a valid `pass`.
8. Partial verification must declare explicit `scope`.

## If You Are The Verifying AI

Open
[VERIFY-IMPLEMENTATION.md](/home/madejuele/projects/2K0300/openspec/schemas/modules/verification-cycle/VERIFY-IMPLEMENTATION.md)
and follow it exactly.

## Process Boundary

The verifier sub-agent emits findings and verifier evidence only.

The main process:

- records agent state in `agent-table.json`
- keeps `agent-table.json` current-state-only
- decides `send_input`/`resume`/spawn/repair/terminate through the orchestrator
- must not substitute its own judgment for verifier output

## Contracts

Core contracts:

- `contracts/verification-cycle-core-v1.json`
- `contracts/verification-cycle-agent-table-v1.json`

Adapters:

- `contracts/verification-cycle-openspec-adapter-v1.json`
- `contracts/verification-cycle-standalone-adapter-v1.json`

Orchestrator:

- `orchestrator/CALLER-INTEGRATION.md`
- `orchestrator/contracts/orchestrator-input-v1.json`
- `orchestrator/contracts/orchestrator-decision-v1.json`
- `orchestrator/bin/verification_cycle_resolve.py`
- `orchestrator/bin/verification_cycle_validate.py`

Runtime guard:

- `bin/verification_cycle_guard.py`

Reference index:

- `REFERENCE-INDEX.md`

## Reference Fixtures

Checked standalone examples live under:

- `fixtures/review-runs/standalone-resume-repair-close`
- `fixtures/review-runs/standalone-partial-scope-repair-close`
