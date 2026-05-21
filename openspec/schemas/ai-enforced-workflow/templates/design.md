## Context

<!-- Background and current state -->

## Goals / Non-Goals

**Goals:**
<!-- What this design aims to achieve -->

**Non-Goals:**
<!-- What is explicitly out of scope -->

## Decisions

<!-- Key design decisions and rationale -->

## Engineering Discipline

- Principles reference:
  `openspec/schemas/ai-enforced-workflow/engineering-principles.md`
- Domain language / ADRs consulted:
- Primary feedback loop:
- Prototype question, if any:
- Hard dependencies:
  `verify-sequence/default`, authoritative findings/evidence, valid-pass
  requirements, subject binding, and current-state `agent-table.json`
- Soft dependencies:
  glossary, ADRs, architecture heuristics, and prototype notes; use them when
  present, but do not create auxiliary review gates for them

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
- verification continues a usable `active` agent first
- callers prefer `send_input` while that same `active` agent is still open
- callers use `continuation_probe` to distinguish resume from recovery spawn
- if no usable `active` agent exists, the orchestrator spawns one
- only `block -> pass` marks an agent `non_active`
- termination depends only on a valid `active` pass

Runtime profile policy:

- Use verifier runtime profile from
  `openspec/schemas/ai-enforced-workflow/agents/verify-reviewer.toml`.

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
- Verifier agent path:
  `openspec/schemas/ai-enforced-workflow/agents/verify-reviewer.toml`
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
