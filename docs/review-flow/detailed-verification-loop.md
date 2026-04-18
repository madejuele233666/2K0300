# Detailed Verification Cycle

This document is the line-referenced execution manual for the active
verification-cycle runtime.

## Purpose

Use it when you need to answer:

- how resume/spawn is chosen
- what `non_active` really means
- what makes a pass valid
- why partial scope must be explicit
- what the guard validates mechanically

## Authority Surface

Core module authority:

- [verification-cycle README](/home/madejuele/projects/2K0300/openspec/schemas/modules/verification-cycle/README.md)
- [VERIFY-IMPLEMENTATION](/home/madejuele/projects/2K0300/openspec/schemas/modules/verification-cycle/VERIFY-IMPLEMENTATION.md)
- [verification-cycle-core-v1.json](/home/madejuele/projects/2K0300/openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json)
- [verification-cycle-agent-table-v1.json](/home/madejuele/projects/2K0300/openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json)

Orchestrator authority:

- [CALLER-INTEGRATION](/home/madejuele/projects/2K0300/openspec/schemas/modules/verification-cycle/orchestrator/CALLER-INTEGRATION.md)
- [orchestrator-input-v1.json](/home/madejuele/projects/2K0300/openspec/schemas/modules/verification-cycle/orchestrator/contracts/orchestrator-input-v1.json)
- [orchestrator-decision-v1.json](/home/madejuele/projects/2K0300/openspec/schemas/modules/verification-cycle/orchestrator/contracts/orchestrator-decision-v1.json)
- [verification_cycle_resolve.py](/home/madejuele/projects/2K0300/openspec/schemas/modules/verification-cycle/orchestrator/bin/verification_cycle_resolve.py)
- [verification_cycle_validate.py](/home/madejuele/projects/2K0300/openspec/schemas/modules/verification-cycle/orchestrator/bin/verification_cycle_validate.py)

Guard:

- [verification_cycle_guard.py](/home/madejuele/projects/2K0300/openspec/schemas/modules/verification-cycle/bin/verification_cycle_guard.py)

## End-to-End Loop

1. Read `agent-table.json`.
2. Resume a usable `active` verifier if one exists.
3. Otherwise spawn a new `active` verifier.
4. If the active verifier returns `block`, repair against that same agent.
5. When that same agent returns a valid `pass`, mark it `non_active`.
6. Continue until the current `active` verifier returns a valid `pass` and no
   active blocking state remains.

## Key Meanings

- `active`: the verifier currently allowed to drive the next decision
- `non_active`: a previously blocking agent that has been resolved by
  `block -> pass`
- `closed`: process state only; never proof of resolution
- valid `pass`: `coverage_status=complete` and `exhaustive=true`
- partial review: allowed only with explicit `scope`

## Reference Fixtures

- [standalone-resume-repair-close](/home/madejuele/projects/2K0300/openspec/schemas/modules/verification-cycle/fixtures/review-runs/standalone-resume-repair-close)
- [standalone-partial-scope-repair-close](/home/madejuele/projects/2K0300/openspec/schemas/modules/verification-cycle/fixtures/review-runs/standalone-partial-scope-repair-close)

## Legacy Note

The former review-loop and its transition-resolver materials are archived:

- `openspec/schemas/modules/archive/review-loop-2026-04-18`
