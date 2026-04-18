## Context

<!-- Background and current state -->

## Goals / Non-Goals

**Goals:**
<!-- What this design aims to achieve -->

**Non-Goals:**
<!-- What is explicitly out of scope -->

## Decisions

<!-- Key design decisions and rationale -->

## Independent Verification Plan (STANDARD/STRICT)

Document verification using shared sequence `verify-sequence/default` from:
`openspec/schemas/ai-enforced-workflow/verification-sequence.md`
and shared verification-cycle contracts:

- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`

Stage A flow:

- checkpoints use the same `active/non_active/closed` verification cycle
- docs-first checkpoints use changed `proposal/specs/design/tasks` as the
  primary surface
- source-first checkpoints use changed code, tests, and directly impacted code
  as the primary surface
- approved docs remain reference material when source-first review runs
- repository-index support is optional cache help only
- verification continues a usable `active` agent first
- callers prefer `send_input` while that same `active` agent is still open
- callers use `resume` only when that same `active` agent was closed and must
  be restored
- if no usable `active` agent exists, the orchestrator spawns one
- only `block -> pass` marks an agent `non_active`
- termination depends only on a valid `active` pass

Runtime profile policy:

- Use verifier runtime profile from `.codex/agents/verify-reviewer.toml`.
- Use index-maintainer runtime profile from
  `.codex/agents/index-maintainer.toml` when cache refresh is useful.

Loop rule:

- an `active` agent that reports `block` stays authoritative until that same
  agent returns `pass`
- `close` or `exit` never implies `non_active`
- valid `pass` requires
  `review_coverage.coverage_status=complete` and
  `review_coverage.exhaustive=true`
- partial verification requires explicit `review_scope.scope`
- only the main orchestrator may authorize resume/spawn/repair/terminate, and
  it must not substitute its own judgment for verifier output

## Repository Index Cache Plan (When Useful)

Document repository-index support explicitly when cache artifacts can help
review orientation.

Required fields:

- Index contract id: `repo-index-v1`
- Canonical repository-index root
- Shared cache-helper sequence: `index-sequence/default`
- Optional refresh scoping hints
- Fallback policy (`refresh|bypass`)
- Verifier invocation template: `verify-reviewer-inline-v3`
- Index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- Index skill entry points:
  - `openspec-index-preflight`
  - `openspec-index-maintain`
- Cache-helper evidence path convention
- Findings path convention
- Verifier execution evidence path convention

Shared field groups from `verification-cycle-core-v1.json` and
`verification-cycle-openspec-adapter-v1.json`:

- `invocation_common_required`
- `output_paths_required`
- `verifier_evidence_required`
- `valid_pass_requirements`
- `partial_scope_rule`

Review completion contract:

- execution evidence MUST record:
  - `review_goal`
  - `review_phase`
  - `review_scope`
  - `review_coverage`
  - `reviewed_paths`
  - `skipped_paths`
  - `reviewed_axes`
  - `unreviewed_axes`
- each checkpoint MUST maintain `agent-table.json`

### Review Checkpoints

- Shared sequence reference: `verify-sequence/default`
- Cache-helper sequence reference: `index-sequence/default`
- Review goal: `implementation_correctness`
- Verifier agent path: `.codex/agents/verify-reviewer.toml`
- Index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- Invocation template id: `verify-reviewer-inline-v3`
- Default loop behavior:
  - resume `active` first
  - prefer `send_input` while that same `active` agent is still open
  - use `resume` only when that same `active` agent was closed and must be
    restored
  - spawn when no usable `active` agent exists
  - repair follows `block`
  - only `block -> pass` marks `non_active`
  - final termination requires a valid `active` pass
- Authoritative verifier-subagent findings JSON path:
- Verifier execution evidence JSON path:
- Agent table path:
- Optional cache-helper report path:
- Continuation target on pass:

Checkpoint-specific primary surfaces:

- artifact-completion docs-first review: changed `proposal/specs/design/tasks`
- active-change source-first review: changed code, changed tests, directly
  impacted code

### Optional Gemini Dual Review

Use only when repository or checkpoint policy explicitly enables dual review.

- Runner contract: `gemini-capture`
- Raw report path:
- Normalized report path:
- Maximum attempts:
- Recovery behavior:
  - `input_raw_path -> report_path`
- Resolved command goes in execution evidence, not workflow policy prose

## Migration Plan

<!-- Rollout, rollback, or transition notes -->

## Open Questions

<!-- Outstanding decisions or unknowns -->

## Risks / Trade-offs

<!-- Known risks and trade-offs -->
