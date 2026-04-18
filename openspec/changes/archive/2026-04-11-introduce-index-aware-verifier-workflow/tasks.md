## Status: Superseded

This task list is frozen.

Do not continue checking off or implementing these tasks.

It has been replaced by the layered rollout defined in:
- `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/README.md`
- `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/02-layered-rollout-overview.md`
- `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/08-migration-validation-and-stop-rules.md`

Operational rule:
- treat every task below as historical context only
- do not use this file as the active execution checklist
- start new work from Change A instead of mutating this checklist in place

## 0. Repository Index Plan

- Canonical index contract: `repo-index-v1`
- Canonical repository-index root: `docs/repo-index/`
- Shared preflight sequence: `index-sequence/default`
- Verifier invocation template: `verify-reviewer-inline-v2`
- Index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- Index-preflight skill entry point: `openspec-index-preflight`
- Index-maintain skill entry point: `openspec-index-maintain`
- Stable governed scope objects for this change:
  - `workflow-schema-surface`
    - `scope_root`: `openspec/schemas/ai-enforced-workflow`
    - `allowed_entry_kinds`: `file`, `interface`
    - `granularity_policy`:
      - `openspec/schemas/ai-enforced-workflow/agent-spawn-decision-v1.schema.json`: `file`
      - `openspec/schemas/ai-enforced-workflow/schema.yaml`: `file`
      - `openspec/schemas/ai-enforced-workflow/index-sequence.md`: `interface`
      - `openspec/schemas/ai-enforced-workflow/verification-sequence.md`: `interface`
      - `openspec/schemas/ai-enforced-workflow/templates/design.md`: `file`
      - `openspec/schemas/ai-enforced-workflow/templates/tasks.md`: `file`
    - `fallback_policy`: `refresh`
    - `entry_refs`:
      - `openspec/schemas/ai-enforced-workflow/agent-spawn-decision-v1.schema.json`
      - `openspec/schemas/ai-enforced-workflow/schema.yaml`
      - `openspec/schemas/ai-enforced-workflow/index-sequence.md`
      - `openspec/schemas/ai-enforced-workflow/verification-sequence.md`
      - `openspec/schemas/ai-enforced-workflow/templates/design.md`
      - `openspec/schemas/ai-enforced-workflow/templates/tasks.md`
  - `workflow-agent-surface`
    - `scope_root`: `.codex/agents`
    - `allowed_entry_kinds`: `interface`
    - `granularity_policy`:
      - `.codex/agents/verify-reviewer.toml`: `interface`
      - `.codex/agents/index-maintainer.toml`: `interface`
    - `fallback_policy`: `refresh`
    - `entry_refs`:
      - `.codex/agents/verify-reviewer.toml`
      - `.codex/agents/index-maintainer.toml`
- Mixed governed+nongoverned evidence rule:
  - change-local specs, change-local design/tasks, and touched skill entrypoints under `$CODEX_HOME/skills/` remain in scope through `evidence_paths_or_diff_scope`
  - Stage 0 preflight MUST add those nongoverned paths into `required_paths` when a checkpoint mixes governed and nongoverned review surfaces
  - the checkpoint-specific preflight report is auxiliary evidence passed through `index_context.preflight_report_path`; it is not added to `required_paths` by default
  - interface-card review MAY expand the checkpoint surface through transitive `governed_paths`; those transitive governed files MUST be added to `required_paths` even when they live under a different governed scope root
- Active fallback policy: `refresh` by default for missing or stale governed coverage; `bypass` only for explicitly logged exercise checkpoints; `block` when a required governed scope cannot be refreshed outside bypass exercises.
- Deterministic contract-lint policy:
  - Stage 0 preflight MUST lint Repository Index Plan fields, governed-scope objects, and mirrored required-path/axis contracts before trusted reuse is allowed
  - lint failure blocks verifier invocation before any `reused` claim is accepted
- Dirty-set / transitive closure refresh policy:
  - preflight MUST derive the dirty governed set from touched governed files, stale digest evidence, and interface-card `governed_paths`
  - `index-maintainer` runs only when that closure is dirty or trust validation fails; clean closures reuse existing trusted index artifacts
- Active deep-scan escalation policy: digest mismatch, forbidden dependency drift, missing governed coverage after refresh, contradictory evidence, or a declared `deep_scan_trigger`.
- Checkpoint evidence naming convention:
  - immutable attempt directory: `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-<n>/attempt-<m>/`
  - preflight: `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-<n>/attempt-<m>/<artifact|implementation>-index-preflight.json`
  - findings: `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-<n>/attempt-<m>/findings.json`
  - verifier evidence: `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-<n>/attempt-<m>/verifier-evidence.json`
  - verifier spawn decision: `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-<n>/attempt-<m>/verifier-spawn-decision.json`
  - index-maintainer spawn decision: `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-<n>/attempt-<m>/index-maintainer-spawn-decision.json`
  - Gemini raw/report: `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-<n>/attempt-<m>/gemini-raw.json` and `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-<n>/attempt-<m>/gemini-report.json`
  - latest attempt marker: `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-<n>/latest-attempt.json`
- Shared governed verification contract for every checkpoint in this change:
  - shared sequence: `openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`
  - preflight sequence: `openspec/schemas/ai-enforced-workflow/index-sequence.md#index-sequence/default`
  - preflight caller contract:
    - `change`
    - `mode`
    - `risk_tier`
    - `governed_scopes`
    - `evidence_paths_or_diff_scope`
    - `fallback_policy`
    - optional `expected_index_root`
    - optional `checkpoint_declared_axes`
    - optional `preflight_report_path`
  - minimal verification bundle:
    - `change`
    - `mode`
    - `risk_tier`
    - `evidence_paths_or_diff_scope`
    - `findings_contract`
  - `index_context` fields:
    - `contract`
    - `manifest_path`
    - `manifest_present`
    - `preflight_report_path`
    - `fallback_policy`
  - `output_paths` fields:
    - `findings_path`
    - `verifier_evidence_path`
    - optional `gemini_raw_path`
    - optional `gemini_report_path`
    - optional `spawn_decision_path`
  - agent spawn decision contract:
    - `agent-spawn-decision-v1`
    - schema: `openspec/schemas/ai-enforced-workflow/agent-spawn-decision-v1.schema.json`
    - required before any new verifier or index-maintainer session
    - each spawn record MUST state whether the spawn is `policy_required` or `exception`
    - same-session reruns MUST reuse the active verifier session unless an exception record explicitly allows a new spawn
    - do not open a second working verifier while the current active working session remains reusable and unresolved
  - governed-only execution evidence:
    - `index_contract`
    - `index_manifest_path`
    - `index_preflight_report_path`
    - `index_mode`
    - `deep_scanned_paths`
    - `deep_scan_reasons`
  - governed preflight required-universe fields:
    - `required_paths`
    - `required_axes`
    - preflight report is the frozen review-surface snapshot for verifier coverage validation
  - routing target for blocking findings: `openspec-repair-change`
  - caller-local runtime guardrails:
    - artifact rerun cap and implementation auto-fix cap MAY exist as safety limits
    - these limits do not change the authority rule
  - supported continuation overrides:
    - `verify-only`
    - `dry-run`
    - `manual_pause`
  - rerun rule:
    - same-session reruns handle convergence
    - only a fresh confirmation pass with zero findings may close the checkpoint
    - if a fresh confirmation pass returns findings, that spawned verifier session becomes the new active working session
  - Gemini runner rule for `STRICT` checkpoints:
    - run Gemini only on fresh confirmation passes
    - write both raw Gemini envelope and normalized JSON report
    - require Gemini runner output to normalize to JSON
    - retry once before blocking
    - if raw exists but normalized report is missing, rerun recovery using `input_raw_path -> report_path`
    - record the resolved Linux/Windows runner command in execution evidence
  - verifier caller skill entry points:
    - artifact checkpoints: `openspec-artifact-verify`
    - implementation checkpoints: `openspec-verify-change`
  - review-completion contract:
    - `verifier_output_path`
    - `reviewed_paths`
    - `skipped_paths`
    - `reviewed_axes`
    - `unreviewed_axes`
    - `coverage_status`
    - `saturation_status`
    - optional `early_stop_reason`
    - `skip_reasons` when `skipped_paths` is non-empty
  - authoritative acceptance defaults:
    - `coverage_status=complete`
    - `saturation_status=exhaustive`
    - `skipped_paths=[]`
    - `unreviewed_axes=[]`
    - `partial` or `early_stop` results are non-authoritative

## 1. Define the repository-index contract and preflight sequence

- [x] 1.1 Add capability specs under `openspec/changes/introduce-index-aware-verifier-workflow/specs/repository-index-governance/spec.md`, `openspec/changes/introduce-index-aware-verifier-workflow/specs/index-aware-verification-preflight/spec.md`, and `openspec/changes/introduce-index-aware-verifier-workflow/specs/ai-enforced-workflow/spec.md`, defining `repo-index-v1`, canonical repository-index artifact layout, granularity policy (`directory|file|interface`), mandatory decision-support fields, freshness metadata, deep-scan escalation semantics for governed scopes, the review-completion contract (`reviewed_paths`, `skipped_paths`, `reviewed_axes`, `unreviewed_axes`, `coverage_status`, `saturation_status`, `early_stop_reason`, `skip_reasons`), and the modified `ai-enforced-workflow` capability requirements for Repository Index Plan, Stage 0 preflight, index-aware verifier invocation, and authoritative review-completion evidence.
- [x] 1.2 Add shared sequence `openspec/schemas/ai-enforced-workflow/index-sequence.md` with logical sequence id `index-sequence/default`; define discovery, validation, refresh, bypass, and authoritative preflight evidence outputs (`manifest_path`, `manifest_present`, `coverage_report_path`, `refresh_report_path`, stable governed scope objects, `index_mode`, `fallback_policy`, authoritative `required_paths`, authoritative `required_axes`, refresh-or-bypass reason, `deep_scan_candidates`) without embedding project-specific shell commands.
- [x] 1.3 Update `openspec/schemas/ai-enforced-workflow/schema.yaml` so governed `STANDARD` and `STRICT` changes must declare a Repository Index Plan, reference `index-sequence/default` when a fresh confirmation surface must be compiled, and treat missing or stale index coverage as either a refresh trigger or an explicit deep-scan fallback rather than an unstated reviewer assumption.
- [x] 1.4 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for the repository-index contract and shared sequence changes. Use the shared governed verification contract above, including the minimal bundle, `index_context`, routing target, caller-local runtime guardrails, and review-completion fields. Write repository-index preflight evidence JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-1/attempt-1/artifact-index-preflight.json`, authoritative verifier-subagent findings JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-1/attempt-1/findings.json`, verifier execution evidence JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-1/attempt-1/verifier-evidence.json`, and run `gemini-capture` with raw/report outputs at `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-1/attempt-1/gemini-raw.json` and `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-1/attempt-1/gemini-report.json` only if checkpoint-1 is executed as the fresh confirmation pass. Record originating phase `artifact_gate`, continuation target `apply`, and the active resolved command in execution evidence.

## 2. Add the index-maintainer subagent and index orchestration skills

- [x] 2.1 Add `.codex/agents/index-maintainer.toml` defining a repository-index maintenance worker that may run with `fork_context=true`, may read existing repository documentation and touched-scope context, and must emit repository-index artifacts and update reports only, never final review verdicts or repair routing decisions.
- [x] 2.2 Add `$CODEX_HOME/skills/openspec-index-maintain/SKILL.md` as the explicit maintenance entry point that discovers canonical repository-index paths, scopes the refresh request, invokes the `index-maintainer` subagent, and verifies that the expected `repo-index-v1` artifacts were written.
- [x] 2.3 Add `$CODEX_HOME/skills/openspec-index-preflight/SKILL.md` as the reusable preflight entry point for verify/apply/repair flows. It must discover existing index artifacts, validate contract/freshness/coverage, invoke `openspec-index-maintain` only when needed, and return normalized preflight output paths and trust result without performing final review itself.
- [x] 2.4 Update any skill or README-level documentation under `.codex/agents/` or related workflow docs so the boundary is explicit: subagents perform bounded work, while skills own orchestration, path normalization, and handoff.
- [ ] 2.5 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for the new index-maintainer agent and index skill entry points. Use the shared governed verification contract above, including the minimal bundle, `index_context`, routing target, caller-local runtime guardrails, and review-completion fields. Write repository-index preflight evidence JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-2/attempt-1/artifact-index-preflight.json`, authoritative verifier-subagent findings JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-2/attempt-1/findings.json`, verifier execution evidence JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-2/attempt-1/verifier-evidence.json`, and write Gemini raw/report outputs to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-2/attempt-1/gemini-raw.json` and `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-2/attempt-1/gemini-report.json` only if checkpoint-2 is executed as the fresh confirmation pass. Record originating phase `artifact_gate` and continuation target `apply`.

## 3. Update `verify-reviewer` and shared verification flow to consume repository index context

- [x] 3.1 Update `.codex/agents/verify-reviewer.toml` so the verifier remains read-only, supports same-session convergence reruns, and requires a fresh verifier session only for fresh confirmation passes. It can consume optional `index_context` containing `repo-index-v1` manifest state, `manifest_present`, preflight evidence paths, and the active preflight fallback policy. Document that the verifier reads index artifacts first, continues through the full declared review surface instead of stopping after the first blocker cluster, and escalates to deep scan when declared triggers, digest conflicts, coverage gaps, or contradictory evidence appear.
- [x] 3.2 Update `openspec/schemas/ai-enforced-workflow/verification-sequence.md` to add Stage 0 preflight via `index-sequence/default`, define built-in verifier invocation template `verify-reviewer-inline-v2`, preserve the minimal verification bundle, add top-level `output_paths` (`findings_path`, `verifier_evidence_path`, optional Gemini raw/report paths), and specify execution evidence fields for `verifier_output_path`, `index_contract`, `index_manifest_path`, `index_preflight_report_path`, `index_mode`, `deep_scanned_paths`, `deep_scan_reasons`, `reviewed_paths`, `skipped_paths`, `reviewed_axes`, `unreviewed_axes`, `coverage_status`, `saturation_status`, `skip_reasons` (required when `skipped_paths` is non-empty), and optional `early_stop_reason`. Require the reviewer and validator to compute the active review-axis universe as the deduplicated union of all governed entries selected for the checkpoint, plus any bypass or checkpoint-declared axes, derive the required-path universe for file, directory, interface, mixed governed+nongoverned, and bypassed reviews, normalize those paths to concrete repository file paths, and validate completion from `reviewed_paths` / `skipped_paths` rather than trusting status flags alone.
- [x] 3.3 Ensure findings output remains `shared-findings-v1`, repository-index telemetry stays in execution evidence rather than the normalized findings envelope, and checkpoint acceptance blocks default `partial` or `early_stop` review results.
- [ ] 3.4 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for the verifier-contract and shared-verification-sequence updates. Use the shared governed verification contract above, including the minimal bundle, `index_context`, routing target, caller-local runtime guardrails, and review-completion fields. Write repository-index preflight evidence JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-3/attempt-1/artifact-index-preflight.json`, authoritative verifier-subagent findings JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-3/attempt-1/findings.json`, verifier execution evidence JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-3/attempt-1/verifier-evidence.json`, and write `STRICT` Gemini raw/report outputs to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-3/attempt-1/gemini-raw.json` and `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-3/attempt-1/gemini-report.json` only if checkpoint-3 is executed as the fresh confirmation pass. Record originating phase `artifact_gate` and continuation target `apply`.

## 4. Update schema templates and generator artifacts to require an explicit Repository Index Plan

- [x] 4.1 Update `openspec/schemas/ai-enforced-workflow/templates/design.md` so `STANDARD` and `STRICT` governed changes must include a Repository Index Plan naming canonical index root, index contract id, stable governed scope objects, shared index-preflight sequence, per-scope granularity policy, fallback policy, verifier invocation template, index-maintainer agent path, skill entry points, preflight evidence paths including `required_paths/required_axes`, and deep-scan escalation policy.
- [x] 4.2 Update `openspec/schemas/ai-enforced-workflow/templates/tasks.md` so generated governed tasks include an explicit Repository Index Plan with the same mandatory fields as the spec, plus independent verification planning and checkpoint tasks that mention index preflight evidence including `required_paths/required_axes`, repository-index refresh behavior, and the full review-completion contract (`reviewed_paths`, `skipped_paths`, `reviewed_axes`, `unreviewed_axes`, `coverage_status`, `saturation_status`, optional `early_stop_reason`, and `skip_reasons` when skips occur), along with the relationship between `index-sequence/default` and `verify-sequence/default`, without inlining environment-specific command bodies.
- [x] 4.3 Update `openspec/schemas/ai-enforced-workflow/schema.yaml` instruction text so generated proposal/design/tasks artifacts describe repository-index governance as a standard workflow concept rather than project-specific free text.
- [x] 4.4 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for the schema/template updates. Use the shared governed verification contract above, including the minimal bundle, `index_context`, routing target, caller-local runtime guardrails, the required `agent-spawn-decision-v1` reason category (`policy_required` or `exception`) before any new verifier or index-maintainer spawn, and review-completion fields. Write repository-index preflight evidence JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-4/attempt-1/artifact-index-preflight.json`, authoritative verifier-subagent findings JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-4/attempt-1/findings.json`, verifier execution evidence JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-4/attempt-1/verifier-evidence.json`, and write `STRICT` Gemini raw/report outputs to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-4/attempt-1/gemini-raw.json` and `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-4/attempt-1/gemini-report.json` only if checkpoint-4 is executed as the fresh confirmation pass with a maximum of two total Gemini attempts (initial run plus one retry) before blocking. Record originating phase `artifact_gate` and continuation target `apply`.

## 5. Integrate index preflight into verify, apply, repair, and generation skills

- [x] 5.1 Update `$CODEX_HOME/skills/openspec-artifact-verify/SKILL.md` and `$CODEX_HOME/skills/openspec-verify-change/SKILL.md` so both remain thin entry points but invoke `index-sequence/default` through `openspec-index-preflight` only when the shared sequence requires a freshly compiled governed review surface, pass `index_context` to the verifier invocation, and require repository-index evidence paths alongside verifier findings/evidence paths.
- [x] 5.2 Update `$CODEX_HOME/skills/openspec-repair-change/SKILL.md` and `$CODEX_HOME/skills/openspec-apply-change/SKILL.md` so reruns reuse or refresh repository index through the shared preflight sequence, allow same-session convergence reruns, require a fresh confirmation pass before completion, and never substitute inherited verifier memory for refreshed index artifacts.
- [x] 5.3 Update `$CODEX_HOME/skills/openspec-propose/SKILL.md`, `$CODEX_HOME/skills/openspec-ff-change/SKILL.md`, and `$CODEX_HOME/skills/openspec-continue-change/SKILL.md` so generated workflow artifacts include the new Repository Index Plan requirements and reference `index-sequence/default` whenever the selected schema is `ai-enforced-workflow` and the governed scope policy applies.
- [ ] 5.4 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for the verify/apply/repair/generation skill updates. Use the shared governed verification contract above, including the minimal bundle, `index_context`, routing target, caller-local runtime guardrails, and review-completion fields. Write repository-index preflight evidence JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-5/attempt-1/artifact-index-preflight.json`, authoritative verifier-subagent findings JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-5/attempt-1/findings.json`, verifier execution evidence JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-5/attempt-1/verifier-evidence.json`, and write `STRICT` Gemini raw/report outputs to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-5/attempt-1/gemini-raw.json` and `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-5/attempt-1/gemini-report.json` only if checkpoint-5 is executed as the fresh confirmation pass. Record originating phase `artifact_gate` and continuation target `apply`.

## 6. Add exercise evidence for reuse, refresh, and deep-scan fallback

- [x] 6.1 Create exercise artifacts under `openspec/changes/introduce-index-aware-verifier-workflow/verification/` that demonstrate repository-index reuse when a governed scope already has trusted `repo-index-v1` artifacts, including manifest path, trust decision, verifier execution evidence showing index consumption without unnecessary source deep scan, and `coverage_status=complete` with `saturation_status=exhaustive`.
- [x] 6.2 Create exercise artifacts showing missing or stale index coverage triggers `openspec-index-preflight` to invoke the shared-context `index-maintainer`, refresh the affected entries, and hand the updated manifest to `verify-reviewer` without leaking shared-context reasoning into the verifier step.
- [x] 6.3 Create exercise artifacts showing a declared `deep_scan_trigger` or digest/coverage conflict forces verifier source inspection for specific files, and record `deep_scanned_paths` plus `deep_scan_reasons` in execution evidence.
- [x] 6.4 Create exercise artifacts proving the role boundary: `index-maintainer` shares main-process context and writes index artifacts, while `verify-reviewer` still runs as a fresh read-only reviewer with no inherited verifier memory.
- [x] 6.5 Create exercise artifacts proving that the verifier does not stop after the first few blockers: use a governed review surface with multiple independent findings across more than one path or review axis, and record exhaustive reviewed-path/axis evidence in the same verifier execution output.
- [ ] 6.6 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for the exercise evidence and end-to-end workflow behavior. Use the shared governed verification contract above, including the minimal bundle, `index_context`, routing target, caller-local runtime guardrails, and review-completion fields. Write repository-index preflight evidence JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-6/attempt-1/implementation-index-preflight.json`, authoritative verifier-subagent findings JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-6/attempt-1/findings.json`, verifier execution evidence JSON to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-6/attempt-1/verifier-evidence.json`, and write `STRICT` Gemini raw/report outputs to `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-6/attempt-1/gemini-raw.json` and `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-6/attempt-1/gemini-report.json` only if checkpoint-6 is executed as the fresh confirmation pass. Record originating phase `implementation_verify`, continuation target `archive/report completion`, and the active resolved command in execution evidence.
