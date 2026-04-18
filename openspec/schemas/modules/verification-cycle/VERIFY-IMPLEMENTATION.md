# Verify Implementation

Use this module when an AI is asked to validate whether an implementation is
correct under the active verification-cycle semantics.

## What The Request Means

Treat the task as a strict implementation-validation loop over the current
workspace state.

The target may be:

- a change
- a diff
- a file set
- a repository subsystem
- a direct standalone target

Planning artifacts are optional reference material only.

## Required Inputs

You need one subject key plus the shared invocation fields.

Common required fields:

- `review_goal=implementation_correctness`
- `review_phase=docs_first|source_first`
- `risk_tier`
- `evidence_paths_or_diff_scope`
- `findings_contract`

OpenSpec usage:

- `change`

Standalone usage:

- `target_ref`

## Bootstrap Rules

If the caller references this module root without a prebuilt bundle:

1. Start from `verification-cycle-core-v1.json -> bootstrap_defaults`.
2. Set `review_phase` from the declared review surface.
3. Resolve `change` or synthesize `target_ref`.
4. Derive `evidence_paths_or_diff_scope` from the explicit target or current
   changed paths.
5. If no concrete implementation scope can be discovered, stop and ask for
   scope before review starts.

## Required Outputs Per Attempt

Every attempt must write:

- `findings.json`
- `verifier-evidence.json`

The caller must also maintain:

- `agent-table.json`

Ownership rule:

- the verifier owns only findings and verifier-evidence payloads
- `agent-table.json` is caller-owned state and MUST NOT be treated as verifier-authored output
- `final_state` and other verifier execution metadata do not by themselves decide `resumable`

`findings.json` must expose top-level:

- subject key (`change` or `target_ref`)
- `verdict=block|pass`
- `findings`

`verifier-evidence.json` must expose:

- the same subject key as `findings.json` (`change` or `target_ref`)
- `review_goal`
- `review_phase`
- `template_id`
- `findings_contract`
- `agent_id`
- `start_at`
- `end_at`
- `final_state`
- `review_scope`
- `review_coverage`
- `reviewed_paths`
- `skipped_paths`
- `reviewed_axes`
- `unreviewed_axes`

Execution metadata constraints:

- `template_id=verify-reviewer-inline-v3`
- `final_state` is one of `completed|completed_pass`
- `start_at` and `end_at` are valid date-time strings
- `end_at >= start_at`

Valid pass rule:

- `review_coverage.coverage_status=complete`
- `review_coverage.exhaustive=true`

If `review_coverage.coverage_status=partial`, `review_scope.scope` must be
explicit and non-empty.

## Execution Rules

1. If a usable `active` agent exists, continue it first: prefer `send_input`
   while that agent is still open. If `send_input` returns `agent not found`,
   route to `resume` for that same `active` agent instead of spawning.
2. Then use `resume` only when that same `active` agent was closed and must be restored.
3. If and only if `agent-table.json` contains no `active` agent, spawn one and
   record it as `active`. `no_usable_active_agent` has only that literal
   meaning.
4. If the active agent returns `block`, route to repair and keep that agent as
   the active verifier baseline.
5. After repair, rerun the same agent until it returns a valid `pass`.
6. Only `block -> pass` may change that agent to `non_active`.
7. `close`, `exit`, timeout, or observer-only completion never imply
   `non_active`.
8. When all prior blocking agents are `non_active`, continue the next `active`
   verifier attempt by `send_input` if it is still open, otherwise `resume`
   it if it was closed; spawn only when `agent-table.json` has no `active`
   agent.
9. Terminate only when the current `active` agent returns a valid `pass` and
   no active blocking state remains.

## Forbidden Actions

- Do not invent archived dual-session phases.
- Do not use closure authority as a substitute for verifier output.
- Do not treat `close` or `exit` as proof that a problem is resolved.
- Do not terminate on partial coverage.
- Do not allow a partial verifier to omit `scope`.

## Recommended Run Layout

```text
verification-runs/<run-name>/
  agent-table.json
  attempt-1/
    findings.json
    verifier-evidence.json
  attempt-2/
    findings.json
    verifier-evidence.json
```

## Mechanical Gate

After writing run artifacts, validate them with:

```bash
python3 /home/madejuele/projects/2K0300/openspec/schemas/modules/verification-cycle/bin/verification_cycle_guard.py --run-dir <run-dir>
```

Do not claim the workflow was followed if this check fails.
