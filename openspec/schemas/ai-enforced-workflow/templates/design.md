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

- checkpoints use the same `active/non_active` verification cycle
- docs-first checkpoints use changed `proposal/specs/design/tasks` as the
  primary surface
- source-first checkpoints use changed code, tests, and directly impacted code
  as the primary surface
- approved docs remain reference material when source-first review runs
- repository-level `.index/` material is optional background only
- verification continues a usable `active` agent first
- callers prefer `send_input` while that same `active` agent is still open
- callers use `continuation_probe` to distinguish resume from recovery spawn
- if no usable `active` agent exists, the orchestrator spawns one
- only `block -> pass` marks an agent `non_active`
- termination depends only on a valid `active` pass

Runtime profile policy:

- Use verifier runtime profile from `.codex/agents/verify-reviewer.toml`.

Loop rule:

- an `active` agent that reports `block` stays authoritative until that same
  agent returns `pass`
- `agent-table.json` stays current-state-only; recovery lives in
  `continuation_probe`
- valid `pass` requires
  `review_coverage.coverage_status=complete` and
  `review_coverage.exhaustive=true`
- partial verification requires explicit `review_scope.scope`
- only the main orchestrator may authorize resume/spawn/repair/terminate, and
  it must not substitute its own judgment for verifier output

## External Repository Index Reference (Optional)

Document `.index/` only when repository-level indexing is useful as
background for humans or external AI.

Required fields:

- Canonical root: `.index/`
- Run contract path: `.index/contracts/repository-index-run-v1.json`
- Manifest contract path: `.index/contracts/repository-index-manifest-v1.json`
- Entry contract path: `.index/contracts/repository-index-entry-v1.json`
- Validator entrypoint: `.index/bin/validate_repository_index.py`
- Run modes:
  - `full_refresh` (default)
  - `scoped_refresh`
- Canonical outputs per run:
  - one run JSON
  - one manifest JSON
  - per-entry JSON files
  - one human-readable summary Markdown
- Non-authority rule:
  - `.index/` is reference material only
  - it MUST NOT emit verifier verdicts
  - it MUST NOT claim workflow closure authority
  - it MUST NOT define repair routing
  - it MUST NOT be required for verification completion

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
- Review goal: `implementation_correctness`
- Verifier agent path: `.codex/agents/verify-reviewer.toml`
- Invocation template id: `verify-reviewer-inline-v3`
- Default loop behavior:
  - resume `active` first
  - prefer `send_input` while that same `active` agent is still open
  - use `continuation_probe` to distinguish resume from dedicated recovery
    spawn
  - spawn when no usable `active` agent exists
  - repair follows `block`
  - only `block -> pass` marks `non_active`
  - final termination requires a valid `active` pass
- Authoritative verifier-subagent findings JSON path:
- Verifier execution evidence JSON path:
- Agent table path:
- Optional `.index/` summary path:
- Continuation target on pass:

Checkpoint-specific primary surfaces:

- artifact-completion docs-first review: changed `proposal/specs/design/tasks`
- active-change source-first review: changed code, changed tests, directly
  impacted code

## Migration Plan

<!-- Rollout, rollback, or transition notes -->

## Open Questions

<!-- Outstanding decisions or unknowns -->

## Risks / Trade-offs

<!-- Known risks and trade-offs -->
