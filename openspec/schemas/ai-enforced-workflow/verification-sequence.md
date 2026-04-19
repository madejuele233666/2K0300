# verify-sequence/default

Shared Stage A verification orchestration for `ai-enforced-workflow`.

Consumers:

- `$CODEX_HOME/skills/openspec-artifact-verify/SKILL.md`
- `$CODEX_HOME/skills/openspec-verify-change/SKILL.md`
- `$CODEX_HOME/skills/openspec-repair-change/SKILL.md`
- `openspec/schemas/ai-enforced-workflow/schema.yaml`

## Stage A Goal

Keep the main path short:

```text
continue active verifier
  -> prefer send_input while still open
  -> if send_input says agent not found, resume that same active agent
  -> otherwise resume if that same active agent was closed
  -> or spawn for a literal no-active-agent case or a dedicated recovery case
  -> verifier returns block or pass
  -> repair until the same blocking agent returns valid pass
  -> mark that agent non_active
  -> continue with the next active verifier
  -> terminate on valid active pass
```

Repository-level indexing, when present under `.index/`, is external reference
material only. It is outside the verifier bundle and never the authority for
termination.

## Verifier Agent Contract

- Agent definition: `.codex/agents/verify-reviewer.toml`
- Role: reviewer only
- Parent-context rule: verifier spawns MUST use `fork_context=false`
- Bundle rule: pass only the minimal verification bundle and `output_paths`
- Output rule: normalized JSON only; malformed output blocks the step
- Invocation rule: built-in subagent invocation is the default path
- Automatic continuation rule: if verification is required, continue in the
  same turn unless the caller explicitly requested `dry-run` or
  `manual_pause`
- Invocation template id: `verify-reviewer-inline-v3`

## Shared JSON Contracts

Canonical shared contract files:

- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`
- `openspec/schemas/modules/verification-cycle/orchestrator/CALLER-INTEGRATION.md`

The shared module is the source of truth for:

- invocation bundle fields
- OpenSpec subject binding (`change`)
- verifier evidence fields, including persisted evidence subject binding and
  execution metadata constraints
- agent-table semantics
- valid-pass requirements
- partial-scope requirements

Schema-local rules:

- review target selection remains local to `ai-enforced-workflow`
- checkpoints may be docs-first or source-first
- review uses `review_goal=implementation_correctness`

## Orchestrator Integration

All callers that execute `verify-sequence/default` MUST use the shared
verification-cycle orchestrator.

Required orchestrator surface:

- `openspec/schemas/modules/verification-cycle/orchestrator/CALLER-INTEGRATION.md`
- `openspec/schemas/modules/verification-cycle/orchestrator/contracts/orchestrator-input-v1.json`
- `openspec/schemas/modules/verification-cycle/orchestrator/contracts/orchestrator-decision-v1.json`
- `openspec/schemas/modules/verification-cycle/orchestrator/bin/verification_cycle_resolve.py`
- `openspec/schemas/modules/verification-cycle/orchestrator/bin/verification_cycle_validate.py`

Decision mapping:

- `resume_active`
- `spawn_active`
- `enter_repair`
- `mark_non_active`
- `terminate`
- `wait_for_user`
- `blocked_invalid_state`

Caller rule:

- maintain `agent-table.json`
- normalize current state mechanically
- include `latest_result` in orchestrator input when the current `active`
  agent already produced a normalized verifier result
- include `continuation_probe` in orchestrator input when the current
  `active` agent already went through a `send_input` / `resume` recovery path
- resolve the next step through the orchestrator
- for `resume_active`, prefer `send_input` to the same open `active` agent
- if `send_input` returns `agent not found`, try `resume` for that same
  `active` agent next
- use `resume` only when that same `active` agent was closed and must be
  restored
- keep `no_usable_active_agent` reserved for the literal no-`active`-entry
  case; use `active_agent_missing` or `active_agent_not_resumable` for
  recovery spawns instead of rewriting them
- require `spawn_active` decisions to expose machine-readable
  `spawn_reason_code`
- execute that decision exactly

## Authoritative Verification Evidence

Checkpoint acceptance requires authoritative:

- findings JSON
- verifier execution evidence JSON
- agent-table JSON

Ownership split:

- verifier-subagent authority covers only findings JSON and verifier execution evidence JSON
- caller authority covers `agent-table.json`
- callers may pass `agent_table_path` in `output_paths`, but the verifier must not claim it as a verifier-authored artifact

Minimum findings fields:

- `change` or `target_ref`
- `verdict=block|pass`
- `findings`

Minimum execution evidence fields:

- subject key (`change` or `target_ref`) matching the findings payload
- all fields named in `verification-cycle-core-v1.json ->
  verifier_evidence_required`
- `review_scope`
- `review_coverage`
- optional `skip_reasons`
- optional `early_stop_reason`

Valid pass rule:

- `review_coverage.coverage_status=complete`
- `review_coverage.exhaustive=true`

Partial-scope rule:

- if `review_coverage.coverage_status=partial`, `review_scope.scope` MUST be
  explicit and non-empty

## Runtime Semantics

- if a usable `active` agent exists, continue it first with `send_input` while
  it is still open; if `send_input` returns `agent not found`, `resume` that
  same `active` agent next; otherwise `resume` it if it was closed
- if `agent-table.json` contains no `active` agent, or the prior `active`
  agent reached a dedicated recovery spawn case, spawn a new one and record
  it as `active`
- `no_usable_active_agent` means only that `agent-table.json` contains no
  `active` agent; recovery spawns keep their own distinct reason codes
- if the active agent returns `block`, route to repair and keep that same
  agent active
- only `block -> pass` may mark an agent `non_active`
- `close` or `exit` does not imply `non_active`
- termination may use only a valid pass from the current `active` agent

## Built-in Invocation Template: `verify-reviewer-inline-v3`

Caller passes:

```json
{
  "change": "<change-name>",
  "review_goal": "implementation_correctness",
  "review_phase": "docs_first|source_first",
  "risk_tier": "LIGHT|STANDARD|STRICT",
  "evidence_paths_or_diff_scope": ["..."],
  "findings_contract": "shared-findings-v1",
  "output_paths": {
    "findings_path": "...",
    "verifier_evidence_path": "...",
    "agent_table_path": "..."
  }
}
```

## Legacy Note

The historical `review-loop` module now lives under:

- `openspec/schemas/modules/archive/review-loop-2026-04-18`

It is archive-only and must not be used by active callers.
