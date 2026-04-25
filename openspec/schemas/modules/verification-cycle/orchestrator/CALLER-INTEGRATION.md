# Verification-Cycle Caller Integration

Use the orchestrator to make mechanical state decisions from current verifier
artifacts and `agent-table.json`.

## Required Contracts

- `contracts/orchestrator-input-v1.json`
- `contracts/orchestrator-decision-v1.json`
- `../contracts/verification-cycle-agent-table-v1.json`

## Caller Rule

The caller must:

1. read the latest `agent-table.json`
2. read the latest findings/evidence artifacts when present
3. build normalized orchestrator input
   - include `latest_result` when the current verifier attempt already
     produced a normalized outcome for the same `active` agent
   - include `continuation_probe` when a `send_input` / `resume` recovery path
     was attempted for the current `active` agent
   - treat `agent-table.json` as current-state-only; do not mirror full
     attempt history there
4. run `verification_cycle_resolve.py`
5. execute the returned decision exactly
6. for `resume_active`, prefer `send_input` when the same `active` agent is
   still open; if `send_input` returns `agent not found`, use `resume` for
   that same `active` agent next; use `continuation_probe.resume_result` to
   distinguish resume from recovery spawn before continuing

The caller must not improvise resume/spawn/terminate logic from prompt prose.

## Decision Semantics

- `resume_active`: a usable active agent exists and must be continued; use
  `send_input` if it is still open, route `agent not found` to `resume`, and
  keep following the same active baseline unless `continuation_probe` proves a
  dedicated recovery spawn case
- `spawn_active`: spawn a new active verifier only when either
  `agent-table.json` contains no active agent (`reason_code=no_usable_active_agent`,
  literal only) or the prior `active` agent entered a dedicated recovery case
  (`reason_code=active_agent_missing` or `reason_code=active_agent_not_resumable`)
  Recovery reason codes remain distinct: `active_agent_missing` and
  `active_agent_not_resumable`.
  The orchestrator decision MUST expose that branch via machine-readable
  `spawn_reason_code`, not prose alone.
- `enter_repair`: the active agent has a blocking verdict
- `mark_non_active`: the same agent moved from block to valid pass, as
  evidenced by `latest_result`
- `terminate`: the current active agent has a valid pass and no active block
  remains
- `wait_for_user`: explicit manual pause
- `blocked_invalid_state`: state is malformed and must not proceed

## Notes

- `continuation_probe` is the only recovery input for active-agent
  missing/not-resumable branches
- `agent-table.json` stores only current state, not complete attempt history
- partial verification requires explicit `scope`
- the active agent is the only agent that may drive termination
