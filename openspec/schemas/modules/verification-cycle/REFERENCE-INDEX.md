# Verification-Cycle Reference Index

Use this file as the single reference entrypoint for the active
verification-cycle runtime surface.

## Authoritative Runtime Surface

Core contracts:

- `contracts/verification-cycle-core-v1.json`
- `contracts/verification-cycle-agent-table-v1.json`
- `contracts/verification-cycle-openspec-adapter-v1.json`
- `contracts/verification-cycle-standalone-adapter-v1.json`

Orchestrator:

- `orchestrator/CALLER-INTEGRATION.md`
- `orchestrator/contracts/orchestrator-input-v1.json`
- `orchestrator/contracts/orchestrator-decision-v1.json`
- `orchestrator/bin/verification_cycle_resolve.py`
- `orchestrator/bin/verification_cycle_validate.py`

Guard:

- `bin/verification_cycle_guard.py`

Human-facing docs:

- `README.md`
- `VERIFY-IMPLEMENTATION.md`

## Active Reference Runs

### `standalone-resume-repair-close`

Shows:

- initial active agent block
- repair against the same agent
- `block -> pass -> non_active`
- final active pass for termination

### `standalone-partial-scope-repair-close`

Shows:

- partial scoped verification
- required `scope`
- follow-up full verification before termination
