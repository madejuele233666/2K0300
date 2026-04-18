# Current Authoritative Verification Flow

The active authoritative flow is the shared `verification-cycle` module:

- `openspec/schemas/modules/verification-cycle/README.md`
- `openspec/schemas/modules/verification-cycle/VERIFY-IMPLEMENTATION.md`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`
- `openspec/schemas/modules/verification-cycle/orchestrator/CALLER-INTEGRATION.md`
- `openspec/schemas/modules/verification-cycle/bin/verification_cycle_guard.py`

## Required Loop

```text
usable active agent?
  yes -> resume
  no  -> spawn

active agent returns block?
  yes -> repair and rerun same agent
  no  -> if pass is valid, continue state transition

block -> pass
  -> mark that agent non_active
  -> continue cycle

final active valid pass
  -> terminate
```

## Execution Rules

1. Resume usable `active` first.
2. Spawn only when no usable `active` agent exists.
3. Repair stays attached to the same blocking agent.
4. `non_active` means resolved through `block -> pass`, not through close.
5. A pass must be complete and exhaustive.
6. Partial review must name scope.

## Legacy Note

The former legacy dual-session review-loop has been archived at:

- `openspec/schemas/modules/archive/review-loop-2026-04-18`
