## Status: Superseded

This change is frozen as historical reference only.

Do not continue implementation from this change.

Reason:
- it reflects the older `index-first` / heavy preflight direction
- it conflicts with the current `code-review-first` layered plan

Authoritative replacement:
- `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/README.md`
- `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/01-principles-and-boundaries.md`
- `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/02-layered-rollout-overview.md`
- `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/08-migration-validation-and-stop-rules.md`

Replacement rollout:
- Change A: `evolve-ai-enforced-workflow-core-review-loop`
- Change B: `introduce-tracked-findings-for-repeatable-families`
- Change C: `introduce-non-blocking-variant-analysis`
- Change D: `harden-stable-code-review-first-contracts`

What may still be reused from this change:
- same-session convergence plus fresh confirmation
- `index-maintainer` as cache maintenance only, never as review authority

## Why

`ai-enforced-workflow` currently treats every verifier rerun as a fresh isolated review pass with no reusable repository knowledge layer. That preserves reviewer independence, but it also creates two practical failures across projects:

- the verifier often returns only a small first batch of findings before stopping, so later reruns discover additional issues that could have been found earlier
- each rerun must rediscover file responsibilities and architecture boundaries from source, which is slow and wastes review budget on repeated codebase scanning

Some repositories already maintain mapping or structure documents, but the workflow has no standard for discovering, validating, or reusing them. As a result, one project may have rich file-level reference material while another project forces the verifier to rescan everything from code alone, and neither case is encoded in the schema as a reviewable contract.

The workflow needs a standard repository-index layer that is detailed enough to support review decisions, but not so detailed that it degenerates into a second copy of the source. It also needs a dedicated index-maintenance role that can share the main process context to keep repository knowledge up to date without weakening the verifier's isolation.

## What Changes

- Define a project-agnostic repository index contract `repo-index-v1` for governed repository areas, including canonical manifest layout, index-entry granularity rules, freshness requirements, and deep-scan escalation triggers.
- Introduce a shared index-preflight sequence that first discovers an existing repository index, validates whether it is current and sufficiently trusted for the active scope, and invokes index maintenance only when the index is missing, stale, or coverage-incomplete.
- Make Stage 0 preflight mandatory before every governed verifier run and require it to perform deterministic contract lint over the active workflow artifacts before trust/reuse is allowed.
- Make refresh conditional on a dirty governed-path set plus transitive governed closure, instead of blindly refreshing before every verifier run.
- Add a dedicated `index-maintainer` subagent that may inherit the main process context and is responsible only for creating or refreshing repository-index artifacts, never for issuing final review verdicts.
- Add indexing skills that orchestrate index discovery, validation, refresh, and report handoff while keeping subagent responsibilities separate from skill responsibilities.
- Update `verify-reviewer` so it can consume repository-index context and use it as the first review surface, while preserving the existing rule that the verifier itself remains a fresh read-only reviewer and escalates to source-level deep scans only when triggers require it.
- Add a review-completion contract so authoritative verifier output requires reviewed-path and reviewed-axis coverage accounting, prohibits default early-stop behavior after the first few blockers, and records whether a pass was exhaustive or partial through explicit coverage/saturation evidence.
- Treat preflight evidence as the frozen review-surface snapshot for the verifier and validator by requiring authoritative `required_paths` and `required_axes`.
- Extend shared verification contracts, schema instructions, templates, and verify-related skills so artifact review, implementation review, and repair reruns all follow the same index-first behavior.
- Require verification evidence to record whether an index was reused, refreshed, or bypassed, plus which files were escalated to deep scan and why.
- **BREAKING**: `ai-enforced-workflow` design and task artifacts for governed projects will need to describe repository-index coverage, preflight behavior, index-maintainer orchestration, and deep-scan escalation conditions in addition to the existing verifier/Gemini flow.

## Capabilities

### New Capabilities

- `repository-index-governance`: Defines canonical repository-index artifacts, granularity rules, sufficiency criteria, freshness policy, and escalation triggers for governed scopes.
- `index-aware-verification-preflight`: Defines the shared discovery, validation, refresh, and handoff flow that runs before verifier review and keeps index maintenance separate from final review.

### Modified Capabilities

- `ai-enforced-workflow`: Extends verification planning so governed projects must perform repository-index preflight before verifier review, may use a shared-context index-maintainer subagent to refresh knowledge artifacts, and must record index/deep-scan evidence alongside existing verifier execution evidence.

## Risk Tier

- `STRICT`: this change rewires core workflow behavior across schema rules, templates, verifier invocation contracts, skill entry points, and review evidence. It changes how repositories are summarized, how verification context is prepared, and how review isolation coexists with reusable project knowledge.

## Impact

- Affected schema and shared workflow artifacts: `openspec/schemas/ai-enforced-workflow/schema.yaml`, `verification-sequence.md`, a new shared index sequence, and schema templates.
- Affected agent definitions and skills: `.codex/agents/verify-reviewer.toml`, new `.codex/agents/index-maintainer.toml`, verify/apply/repair skills, and new index-related skills.
- Affected evidence model: verification checkpoints must record index manifest/report paths, reuse vs refresh outcome, deep-scan escalation reasons, and review coverage/saturation fields in addition to verifier findings and Gemini outputs.
- Dependencies and systems: Codex built-in subagent API, forked-context subagent support for index maintenance, repo-local index artifacts, and OpenSpec-generated change artifacts that must now describe index governance explicitly.
- Expected skills: `openspec-propose`, `openspec-artifact-verify`, `openspec-verify-change`, `openspec-repair-change`, and the new index-preflight / index-maintain skills.
