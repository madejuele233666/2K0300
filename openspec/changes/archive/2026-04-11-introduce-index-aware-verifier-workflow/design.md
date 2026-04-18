## Status: Superseded

This design is frozen as historical reference only.

Do not implement from this file.

It has been replaced by the `code-review-first` layered plan under
`docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/`.
If this design conflicts with that plan in mechanism weight, authority
boundaries, or rollout order, that archived layered plan wins.

Still-reusable ideas:
- same-session reviewer convergence followed by fresh confirmation
- strict role boundary: `index-maintainer` may maintain cache artifacts but may
  not issue review verdicts

## Context

`ai-enforced-workflow` already improved verification by separating implementer and reviewer roles, but it still assumes every review pass must reconstruct repository understanding from raw source. That assumption keeps the reviewer isolated, yet it also causes repeated cold-start scans and late-discovered findings.

The missing piece is a standard, reviewable repository knowledge layer.

Current:

```text
implementer/main flow
  -> gather evidence paths
  -> fresh verify-reviewer
  -> source scan from scratch
  -> findings
  -> rerun from scratch
```

Target:

```text
implementer/main flow
  -> index preflight
       -> deterministic contract lint
       -> derive dirty governed set + transitive closure
       -> reuse trusted index
       -> or refresh via index-maintainer when the closure is dirty or trust fails
       -> freeze review surface in preflight evidence
  -> verify-reviewer working session
       -> read frozen review surface + index first
       -> deep-scan only suspicious or drifted files
  -> findings + index/deep-scan evidence
```

The design has to preserve reviewer isolation while allowing repository knowledge to accumulate across runs. That means the workflow must distinguish three separate roles:

- main implementer/orchestrator
- repository-index maintainer
- final verifier/reviewer

Only the index-maintainer is allowed to share the main process context. The
verifier remains read-only; same-session reruns may continue inside the active
working session, while fresh confirmation uses a newly spawned verifier
session.

## Goals / Non-Goals

**Goals:**
- Define a reusable repository-index contract that is stable across projects using `ai-enforced-workflow`.
- Standardize how verification discovers, validates, and refreshes repository-index artifacts before review begins.
- Keep `verify-reviewer` isolated from implementer context while still allowing
  same-session convergence reruns and fresh confirmation spawns over
  structured repository-index context.
- Define the right information density for index entries: enough to drive review decisions and deep-scan escalation, not enough to mirror source code.
- Separate subagent responsibilities from skill orchestration responsibilities.
- Make index reuse and deep-scan escalation auditable in verification evidence.

**Non-Goals:**
- Replacing source-level review entirely with index-only review.
- Forcing every file in every repository, vendor tree, or archive to have a file-level index card.
- Turning `index-maintainer` into a reviewer, repair agent, or schema-routing agent.
- Reusing implementer context as hidden verifier memory across reruns.
- Changing Gemini's role as the optional-or-mandatory second opinion defined elsewhere in the workflow.

## Decisions

### Decision: Define `repo-index-v1` as a decision-oriented repository index contract

Problem being solved:
Repositories need reusable knowledge about file and directory responsibilities, but free-form prose drifts too easily and extremely detailed summaries collapse back into a second copy of the code.

Stack equivalent:
- Canonical repository-index root, defaulting to `docs/repo-index/`
- Canonical manifest, per-entry cards, and refresh reports
- Contract ID `repo-index-v1`

Canonical `repo-index-v1` layout:
- root: `docs/repo-index/`
- manifest: `docs/repo-index/manifest.json`
- file cards: `docs/repo-index/files/<escaped-path>.yaml`
- directory cards: `docs/repo-index/directories/<escaped-path>.yaml`
- interface cards: `docs/repo-index/interfaces/<escaped-path>.yaml`
- latest coverage report: `docs/repo-index/reports/coverage.json`
- latest refresh report: `docs/repo-index/reports/refresh.json`

Escaped-path rule:
- replace `/` with `__`
- preserve file extension in the escaped filename
- repositories MAY declare a different root only when an explicit override is documented in design/tasks and consumed consistently by preflight + verifier entrypoints

Alternatives considered:
- Let each project invent its own index structure.
- Require every file to have a long-form summary document.
- Limit the workflow to raw-source review only.

Why this option was chosen:
The workflow needs index artifacts that are machine-consumable and reviewable. The right abstraction level is not implementation detail; it is review decision support. An index entry is sufficient when it lets the verifier decide whether a file appears to stay within declared responsibility or whether source-level inspection is required.

Granularity policy:

| Level | Intended scope | Required use |
|---|---|---|
| Directory card | large vendor / generated / third-party areas | allowed when the workflow only needs ownership boundaries and escalation policy |
| File card | governed project-owned files and direct dependency touchpoints | default for code the project owns or edits regularly |
| Interface card | high-risk public boundaries, adapter contracts, or workflow gate files | required when a file exports a contract whose invariants affect many downstream reviews |

Information sufficiency rule:
- detailed enough to answer:
  - what this scope owns
  - what it must not own
  - what review axes matter
  - when source deep scan becomes mandatory
- not detailed enough to describe local implementation flow, per-branch logic, or helper-level algorithm internals

Mandatory file-card fields:
- `path`
- `kind`
- `layer`
- `owner_role`
- `responsibilities`
- `must_not_do`
- `inputs`
- `outputs`
- `depends_on`
- `public_symbols`
- `review_axes`
- `deep_scan_triggers`
- `confidence`
- `digest`
- `last_verified_at`

Named deliverables:
- repository-index contract text under change specs
- canonical manifest layout guidance
- schema/template wording that references `repo-index-v1`

Failure semantics:
- If the repository index is missing for a governed scope, the workflow cannot assume index-first review and must either refresh the index or fall back to source review according to the preflight rules.
- If an index card is too detailed and mirrors implementation rather than stable responsibilities, it fails review as low-value or high-drift documentation.
- If an index card is too vague to support deep-scan decisions, it also fails review.

Boundary examples:
- Allow: `owner_role=periodic control orchestration`, `must_not_do=direct vendor device access`, `deep_scan_triggers=vendor include added`
- Forbid: a prose dump of line-by-line control flow or every helper function body

Contrast structure:
- Allow: role/boundary/trigger information
- Forbid: pseudo-source summaries that decay with every code edit

Verification hook:
Artifact review can inspect the contract and example cards to ensure they encode stable review decision data rather than code paraphrases.

### Decision: Introduce a shared-context `index-maintainer` subagent and keep review separate

Problem being solved:
Repository knowledge needs a maintainer that can benefit from the main process context, especially when the user has already identified affected areas, prior mapping docs, or rollout constraints. That does not mean the final reviewer should inherit the same context.

Stack equivalent:
- `.codex/agents/index-maintainer.toml`
- built-in subagent API invocation with `fork_context=true`
- index-refresh output artifacts under the repository-index root

Alternatives considered:
- Make `verify-reviewer` maintain the index itself.
- Make the main implementer write and update index artifacts inline.
- Use only skills, with no dedicated subagent role.

Why this option was chosen:
Index maintenance is a knowledge-synthesis task, not a final review verdict. It benefits from inherited project context and can run as a background worker. Keeping it separate prevents the verifier from becoming both knowledge maintainer and judge.

Role boundary:
- `index-maintainer` subagent:
  - discovers existing docs and mappings
  - builds or refreshes repository-index artifacts
  - produces update reports and coverage manifests
  - does not issue final pass/block verdicts
- index-related skills:
  - decide when to invoke index maintenance
  - normalize inputs, output paths, and routing
  - hand index artifacts to verification flows
- `verify-reviewer`:
  - consumes index context
  - performs final review
  - escalates to deep scan when triggers fire
  - does not maintain the index

Named deliverables:
- `.codex/agents/index-maintainer.toml`
- `$CODEX_HOME/skills/openspec-index-maintain/SKILL.md`
- `$CODEX_HOME/skills/openspec-index-preflight/SKILL.md`

Failure semantics:
- If `index-maintainer` cannot build or refresh the index, the preflight step either blocks or falls back to explicit deep-scan review according to checkpoint policy.
- If the subagent tries to emit review verdicts instead of index artifacts, that is a contract violation.

Boundary examples:
- Caller: `openspec-index-preflight`
- Payload: active change, governed scope, existing mapping docs, touched files, output root
- Forbidden leak: final `pass|blocked` review verdict
- Return form: manifest path, refresh report path, updated index entries

Contrast structure:
- Allow: shared-context repository knowledge maintenance
- Forbid: shared-context final reviewer verdicts

Verification hook:
Review the agent definition and skill docs to ensure the maintainer outputs index artifacts only and uses inherited context intentionally.

### Decision: Add a shared `index-sequence/default` to compile confirmation surfaces and trust/deep-scan preparation

Problem being solved:
If every verify-related skill invents its own "look for docs first" behavior, repositories will drift again. The workflow needs one preflight sequence shared by artifact review, implementation review, repair reruns, and future workflow extensions.

Stack equivalent:
- `openspec/schemas/ai-enforced-workflow/index-sequence.md`
- logical sequence id `index-sequence/default`
- shared outputs: manifest path, coverage report path, refresh report path, `index_mode`, and pre-verifier deep-scan candidates

Alternatives considered:
- Hard-code index discovery separately inside each verify skill.
- Add index logic only to artifact verification.
- Skip a shared sequence and rely on local agent intuition.

Why this option was chosen:
The same repository-index discovery and refresh rules apply regardless of whether the workflow is reviewing artifacts or implementation. A shared sequence keeps policy centralized and auditable.

`index-sequence/default` stages:
1. Discover canonical repository-index root and manifest.
2. Validate the active workflow artifacts needed to compile a trustworthy confirmation surface and block when required fields, path-universe rules, or review-surface contracts are inconsistent.
3. Derive the dirty governed-path set plus any transitive governed closure pulled in by interface cards or checkpoint evidence.
4. Validate contract id, freshness metadata, governed-scope coverage, and trust signals against that closure.
5. Decide:
   - reuse existing index
   - refresh missing/stale/dirty closure entries via `index-maintainer`
   - or mark the review as index-bypassed and require source deep scan
6. Emit preflight evidence with:
   - manifest path
   - `manifest_present`
   - coverage report path
   - refresh report path
   - governed scopes
   - `index_mode` (`reused|refreshed|bypassed`)
   - authoritative `required_paths`
   - authoritative `required_axes`
   - active `fallback_policy`
   - refresh or bypass reason
   - candidate deep-scan triggers already known before fresh confirmation review

Design rule:
- the preflight report is the frozen review-surface snapshot for the verifier and validator
- verifier execution evidence must prove coverage against that frozen surface instead of re-deriving it ad hoc

Named deliverables:
- shared index sequence document
- template wording that references `index-sequence/default`
- evidence path rules for index preflight output

Failure semantics:
- If governed-scope coverage is required and neither reuse nor refresh succeeds, the checkpoint is blocked.
- If the sequence marks a scope as bypassed, verifier review must explicitly record source deep scan instead of silently pretending index-first review occurred.

Boundary examples:
- Caller: `openspec-verify-change`
- Payload: governed scope, touched files, expected index contract id, output paths
- Return form: preflight report + index manifest handoff

Contrast structure:
- Current: each review pass starts from source
- Target: fresh confirmation uses index preflight to compile the authoritative surface, while same-session reruns reuse that active surface unless trust drift forces recompilation

Verification hook:
Schema and tasks can be checked for explicit use of `index-sequence/default` before verifier invocation.

### Decision: Extend verifier invocation with optional `index_context` while keeping findings JSON stable

Problem being solved:
The verifier needs a standard way to consume repository-index context, but the workflow should not break every downstream consumer of the current findings JSON contract.

Stack equivalent:
- keep the existing minimal verification bundle
- extend the invocation envelope with optional `index_context`
- preserve `shared-findings-v1` as the findings output contract
- keep findings-envelope stability at the schema/field level rather than
  requiring hidden verifier memory for cross-rerun `id` continuity
- record index/deep-scan details and review coverage/saturation evidence in execution evidence, not in the findings JSON envelope

Proposed invocation shape:

```json
{
  "agent_type": "verify-reviewer",
  "bundle": {
    "change": "<change-name>",
    "mode": "artifact|implementation",
    "risk_tier": "LIGHT|STANDARD|STRICT",
    "evidence_paths_or_diff_scope": ["..."],
    "findings_contract": "shared-findings-v1"
  },
  "index_context": {
    "contract": "repo-index-v1",
    "manifest_path": "...",
    "manifest_present": true,
    "preflight_report_path": "...",
    "fallback_policy": "refresh"
  },
  "output_paths": {
    "findings_path": ".../findings.json",
    "verifier_evidence_path": ".../verifier-evidence.json",
    "gemini_raw_path": ".../gemini-raw.json",
    "gemini_report_path": ".../gemini-report.json"
  }
}
```

Alternatives considered:
- Add repository-index data by overloading `evidence_paths_or_diff_scope`.
- Change the findings output schema to embed coverage and deep-scan telemetry.
- Leave verifier invocation unchanged and rely on ad hoc prompt text.

Why this option was chosen:
The minimal verification bundle remains stable for task/design policy text,
while the invocation envelope gains explicit extension points for index use.
Output consumers remain compatible because findings JSON does not change, and
fresh-confirmation authority no longer depends on cross-rerun comparison input.

Verifier behavior with `index_context`:
- read repository-index artifacts first
- perform cheap consistency checks against current scope/diff
- continue through the declared review surface instead of stopping after the first few independent blockers
- deep-scan source only when:
  - a declared `deep_scan_trigger` fires
  - index coverage is missing for the touched scope
  - file digest or exported surface conflicts with the index
  - evidence elsewhere contradicts index claims

Execution evidence additions:
- `index_contract`
- `index_manifest_path`
- `index_preflight_report_path`
- `index_mode` (`reused|refreshed|bypassed`)
- `deep_scanned_paths`
- `deep_scan_reasons`
- `reviewed_paths`
- `skipped_paths`
- `reviewed_axes`
- `unreviewed_axes`
- `coverage_status` (`complete|partial`)
- `saturation_status` (`exhaustive|early_stop`)
- optional `early_stop_reason`
- `skip_reasons` (required when `skipped_paths` is non-empty)

Named deliverables:
- updated `.codex/agents/verify-reviewer.toml`
- updated shared verification-sequence invocation template
- execution evidence requirements for index usage

Failure semantics:
- If the governed preflight report is unreadable, missing frozen-surface fields, or contradictory to the declared bundle, the verifier must fail closed with a blocking result; bypass is allowed only when a readable authoritative preflight report explicitly records `index_mode=bypassed`.
- If deep-scan triggers fire and source review is skipped anyway, the checkpoint is invalid.
- If `coverage_status` is `partial` or `saturation_status` is `early_stop` without an explicit policy exception, the verifier result is non-authoritative and the checkpoint is blocked before routing or acceptance.

Boundary examples:
- Allow: file card says "must not own vendor includes", diff adds a vendor include, verifier deep-scans the file
- Allow: verifier finds one blocking spec issue early but still continues through the remaining governed paths and review axes before returning the final authoritative findings set
- Forbid: verifier trusts a stale card despite digest mismatch
- Forbid: verifier returns the first 3-4 obvious findings and stops without recording exhaustive scope coverage

Contrast structure:
- Allow: stable findings output + richer execution evidence
- Forbid: contract drift in the findings JSON envelope for non-index consumers

Verification hook:
Review verifier evidence paths to ensure index usage and deep-scan escalation are recorded whenever preflight runs.

### Decision: Make review coverage and saturation explicit acceptance gates

Problem being solved:
Index-first review reduces cold-start cost, but it does not by itself guarantee that the verifier continues reviewing after the first batch of blockers. Without an explicit completion contract, the workflow can still accept partial reviews that discover only a few early findings per pass.

Stack equivalent:
- review coverage fields recorded in authoritative verifier execution evidence
- explicit no-early-stop rule for default governed verification
- checkpoint acceptance that depends on both findings JSON and exhaustive coverage evidence

Alternatives considered:
- Trust verifier intuition and treat any returned findings list as sufficiently complete.
- Add extra findings-schema fields and break downstream routing consumers.
- Record review coverage only in optional markdown notes.

Why this option was chosen:
The workflow already has a stable machine-consumable findings envelope. The missing contract is not more finding fields; it is evidence that the verifier completed the declared review surface. Execution evidence is the right place for that because it governs provenance and acceptance rather than repair routing.

Review completion contract:
- authoritative verifier output requires both:
  - valid `shared-findings-v1` findings JSON
  - execution evidence proving declared review coverage was completed
- the verifier MUST review all declared governed paths and mandatory review axes before finalizing authoritative output
- when a checkpoint spans multiple governed entries or scopes, the required review-axis universe is the deduplicated union of every active entry's `review_axes`, plus any checkpoint-declared axes that apply to the active review surface
- finding a blocker early does not authorize stopping the pass
- default governed verification forbids early stop unless a future policy explicitly declares a partial exploratory mode

Mandatory coverage fields in verifier execution evidence:
- `reviewed_paths`
- `skipped_paths`
- `reviewed_axes`
- `unreviewed_axes`
- `coverage_status` (`complete|partial`)
- `saturation_status` (`exhaustive|early_stop`)
- optional `early_stop_reason`

Acceptance semantics:
- `coverage_status=complete` is required for authoritative completion
- `saturation_status=exhaustive` is required for authoritative completion
- `skipped_paths=[]` is required for authoritative completion
- `unreviewed_axes=[]` is required for authoritative completion
- `partial` or `early_stop` results are non-authoritative and block checkpoint acceptance by default

Boundary examples:
- Caller: `verify-sequence/default`
- Payload: governed evidence scope plus required review axes
- Returned form: findings JSON plus execution evidence proving exhaustive coverage
- Forbidden shortcut: return after first blocker cluster with `coverage_status` omitted

Verification hook:
Checkpoint review must reject verifier evidence that lacks reviewed-path/axis accounting or records `early_stop` without an explicit policy exception.

### Decision: Update schema, templates, and generator skills to require an explicit Repository Index Plan

Problem being solved:
Even if the runtime flow becomes index-aware, generated change artifacts will drift unless schema instructions and templates require authors to declare governed scopes, index coverage expectations, and preflight behavior.

Stack equivalent:
- `schema.yaml` instruction updates
- template updates
- generation-skill updates for `openspec-propose`, `openspec-ff-change`, and `openspec-continue-change`

Required design content for governed changes:
- canonical repository-index root
- shared index-preflight sequence
- governed scopes
- granularity policy by scope
- fallback policy for missing or stale index coverage (`refresh|bypass|block`)
- index contract id
- verifier invocation template
- index-preflight evidence/report paths
- index-maintainer agent path and skill entry points
- deep-scan escalation policy

Required task content for governed changes:
- explicit Repository Index Plan that repeats the spec-required fields for governed changes
- bootstrap or refresh tasks for repository-index artifacts
- explicit `index-sequence/default` planning
- verify checkpoints that record index preflight evidence plus verifier execution evidence

Named deliverables:
- updated `schema.yaml`
- updated `templates/design.md`
- updated `templates/tasks.md`
- updated proposal/generation skills

Failure semantics:
- If a `STANDARD` or `STRICT` governed change omits the Repository Index Plan, artifact review is blocking.
- If tasks mention index-first verification without naming evidence paths or refresh policy, the task artifact is incomplete.

Boundary examples:
- Caller: `openspec-propose`
- Output: design/task artifacts that already name index-preflight paths and governed scopes
- Forbidden leak: vague "use docs if available" wording with no canonical contract

Contrast structure:
- Current: index use is ad hoc and undocumented
- Target: index use is part of schema-generated workflow text

Verification hook:
Artifact review can check generated design/tasks for explicit Repository Index Plan sections and index-sequence references.

## Independent Verification Plan (STANDARD/STRICT)

Document verification using shared sequence `verify-sequence/default` from:
`openspec/schemas/ai-enforced-workflow/verification-sequence.md`

Three-stage flow:
- Stage 0: index preflight through `index-sequence/default` only when the
  shared sequence requires a freshly compiled governed review surface
- Stage 1: read-only verifier subagent (`.codex/agents/verify-reviewer.toml`) review
- Stage 2: Gemini second opinion through logical runner contract
  `gemini-capture` only on fresh confirmation passes when required (`STRICT`
  or explicit dual gate)

Runtime profile policy:
- Use verifier runtime profile from `.codex/agents/verify-reviewer.toml` by default.
- Use index-maintainer runtime profile from `.codex/agents/index-maintainer.toml`.

Loop rule:
- Same-session reruns MAY continue inside the active verifier session after repair.
- Same-session zero findings are convergence-only and MUST trigger a fresh confirmation pass.
- Fresh confirmation MUST run in a newly spawned verifier session.
- If a fresh confirmation pass still returns findings, that spawned session becomes the new active verifier session for subsequent repair-driven same-session reruns.
- Only a fresh confirmation pass with zero findings may close the checkpoint.
- Repository-index refresh MAY reuse inherited main-process context through `index-maintainer`, but that context MUST NOT be transferred to `verify-reviewer`.

Repository Index Plan:
- Canonical index contract: `repo-index-v1`
- Canonical sequence: `index-sequence/default`
- Verifier invocation template: `verify-reviewer-inline-v2`
- Default repository-index root: `docs/repo-index/`
- Index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- Required index skill entry points:
  - `openspec-index-preflight`
  - `openspec-index-maintain`
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
- Mixed governed+nongoverned evidence rule for this change:
  - change-local specs, change-local design/tasks, and touched skill entrypoints under `$CODEX_HOME/skills/` participate through `evidence_paths_or_diff_scope`
  - Stage 0 preflight MUST add the remaining nongoverned evidence paths into `required_paths` whenever a checkpoint mixes governed and nongoverned review surfaces
  - the checkpoint-specific preflight report is auxiliary evidence passed through `index_context.preflight_report_path`; it is not added to `required_paths` by default
  - interface-card review MAY expand the checkpoint surface through transitive `governed_paths`; those transitive governed files MUST be added to `required_paths` even when they live under a different governed scope root
- Active fallback policy for this change: `refresh` on missing or stale governed index coverage; `bypass` is allowed only for explicitly logged checkpoint exercises that prove fallback behavior; `block` applies when a required governed scope cannot be refreshed and the checkpoint is not an exercise of bypass behavior.
- Deterministic contract-lint policy for this change:
  - Stage 0 preflight MUST lint Repository Index Plan fields, governed-scope objects, required-path/axis rules, and mirrored schema/template/sequence wording before trusted reuse is allowed
  - lint failure blocks the verifier before index reuse or refresh claims are accepted
- Dirty-set / transitive closure refresh policy for this change:
  - preflight MUST derive the dirty governed set from touched governed files, stale digest evidence, and transitive `governed_paths` pulled in by interface cards
  - `index-maintainer` runs only when that closure is dirty or trust validation fails; a clean closure reuses existing trusted index artifacts
- Active deep-scan escalation policy for this change: any digest mismatch, forbidden dependency drift, missing governed-scope coverage after refresh, contradictory evidence, or declared `deep_scan_trigger` forces source inspection.
- Checkpoint evidence naming convention for this change:
  - immutable attempt directory: `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-<n>/attempt-<m>/`
  - preflight: `.../checkpoint-<n>/attempt-<m>/<artifact|implementation>-index-preflight.json`
  - findings: `.../checkpoint-<n>/attempt-<m>/findings.json`
  - verifier evidence: `.../checkpoint-<n>/attempt-<m>/verifier-evidence.json`
  - verifier spawn decision: `.../checkpoint-<n>/attempt-<m>/verifier-spawn-decision.json`
  - index-maintainer spawn decision: `.../checkpoint-<n>/attempt-<m>/index-maintainer-spawn-decision.json`
  - Gemini raw/report: `.../checkpoint-<n>/attempt-<m>/gemini-raw.json` and `.../checkpoint-<n>/attempt-<m>/gemini-report.json`
  - each checkpoint directory also maintains `latest-attempt.json`, which records the current authoritative attempt id plus any superseded attempts
- Repository Index Plan MUST also declare fallback policy (`refresh|bypass|block`) for missing or stale index coverage.

Minimal verification bundle (reuse exactly):
- `change`
- `mode`
- `risk_tier`
- `evidence_paths_or_diff_scope`
- `findings_contract`

Verifier envelope extension:
- `index_context.contract`
- `index_context.manifest_path`
- `index_context.manifest_present`
- `index_context.preflight_report_path`
- `index_context.fallback_policy`
- `output_paths.findings_path`
- `output_paths.verifier_evidence_path`
- optional `output_paths.gemini_raw_path`
- optional `output_paths.gemini_report_path`
- optional `output_paths.spawn_decision_path`

Preflight caller contract for this change:
- `change`
- `mode`
- `risk_tier`
- `governed_scopes`
- `evidence_paths_or_diff_scope`
- `fallback_policy`
- optional `expected_index_root`
- optional `checkpoint_declared_axes`
- optional `preflight_report_path`

Agent spawn decision contract for this change:
- `agent-spawn-decision-v1`
- schema: `openspec/schemas/ai-enforced-workflow/agent-spawn-decision-v1.schema.json`
- required before any new verifier or index-maintainer session
- same-session reruns MUST reuse the active verifier session unless an
  exception record explicitly allows a new spawn
- do not open a second working verifier while the current active working
  session remains reusable and unresolved

Governed preflight required-universe contract:
- `required_paths`
- `required_axes`
- the preflight report is the frozen review-surface snapshot for verifier and validator coverage checks

Verifier execution evidence / review-completion contract:
- `verifier_output_path`
- `reviewed_paths`
- `skipped_paths`
- `reviewed_axes`
- `unreviewed_axes`
- `coverage_status`
- `saturation_status`
- optional `early_stop_reason`
- `skip_reasons` (required when `skipped_paths` is non-empty)

### Artifact Verification

- Sequence reference: `openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`
- Preflight sequence reference: `openspec/schemas/ai-enforced-workflow/index-sequence.md#index-sequence/default`
- Mode: `artifact`
- Verifier agent path: `.codex/agents/verify-reviewer.toml`
- Index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- Index-preflight skill entry point: `openspec-index-preflight`
- Index-maintain skill entry point: `openspec-index-maintain`
- Verifier invocation method: built-in subagent API in the main process
- Invocation template id: `verify-reviewer-inline-v2`
- Minimal verification bundle fields:
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
- Repository-index preflight evidence path: `openspec/changes/<change>/verification/checkpoint-<n>/attempt-<m>/<artifact|implementation>-index-preflight.json`
- Checkpoint-specific preflight evidence paths MUST be unique per attempt, for example `verification/checkpoint-1/attempt-1/artifact-index-preflight.json`.
- Authoritative verifier-subagent findings JSON path: `openspec/changes/<change>/verification/checkpoint-<n>/attempt-<m>/findings.json`
- Verifier execution evidence JSON path: `openspec/changes/<change>/verification/checkpoint-<n>/attempt-<m>/verifier-evidence.json`
- Findings path convention for this change: `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-<n>/attempt-<m>/findings.json`
- Verifier evidence path convention for this change: `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-<n>/attempt-<m>/verifier-evidence.json`
- Governed-only execution evidence:
  - `index_contract`
  - `index_manifest_path`
  - `index_preflight_report_path`
  - `index_mode`
  - `deep_scanned_paths`
  - `deep_scan_reasons`
- Output format: `index preflight evidence + authoritative verifier findings json + verifier execution evidence json with coverage/saturation fields` (+ Gemini raw/report when enabled)
- Supported continuation overrides: `verify-only`, `dry-run`, `manual_pause`
- Gemini policy: mandatory on fresh confirmation because this change is `STRICT`
- Runner contract: `gemini-capture`
- Runner prompt inputs: artifact files under the active checkpoint, the checkpoint-specific preflight report, and any changed schema/skill/agent files governed by the checkpoint
- Raw report path (when Gemini enabled): `openspec/changes/<change>/verification/checkpoint-<n>/attempt-<m>/gemini-raw.json`
- `report_path` (when Gemini enabled): `openspec/changes/<change>/verification/checkpoint-<n>/attempt-<m>/gemini-report.json`
- Maximum attempts: `2` total runner attempts before recovery/blocking
- JSON-response requirement: Gemini runner output MUST normalize to JSON; a non-JSON response is a checkpoint blocker unless recovery from an existing raw envelope succeeds
- Command-resolution evidence path: record the resolved Linux/Windows runner command in `openspec/changes/<change>/verification/checkpoint-<n>/attempt-<m>/verifier-evidence.json`
- Fallback behavior: retry once; if raw exists but normalized report is missing, rerun recovery using `input_raw_path -> report_path`; if recovery also fails, block the checkpoint
- Originating phase field: `artifact_gate`
- Routing target on blocking findings: `openspec-repair-change`
- Continuation target on pass: `apply`
- Skill entry point: `openspec-artifact-verify`

### Implementation Verification

- Sequence reference: `openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`
- Preflight sequence reference: `openspec/schemas/ai-enforced-workflow/index-sequence.md#index-sequence/default`
- Mode: `implementation`
- Verifier agent path: `.codex/agents/verify-reviewer.toml`
- Index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- Index-preflight skill entry point: `openspec-index-preflight`
- Index-maintain skill entry point: `openspec-index-maintain`
- Verifier invocation method: built-in subagent API in the main process
- Invocation template id: `verify-reviewer-inline-v2`
- Minimal verification bundle fields:
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
- Repository-index preflight evidence path: `openspec/changes/<change>/verification/checkpoint-<n>/attempt-<m>/<artifact|implementation>-index-preflight.json`
- Checkpoint-specific preflight evidence paths MUST be unique per attempt, for example `verification/checkpoint-6/attempt-1/implementation-index-preflight.json`.
- Authoritative verifier-subagent findings JSON path: `openspec/changes/<change>/verification/checkpoint-<n>/attempt-<m>/findings.json`
- Verifier execution evidence JSON path: `openspec/changes/<change>/verification/checkpoint-<n>/attempt-<m>/verifier-evidence.json`
- Findings path convention for this change: `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-<n>/attempt-<m>/findings.json`
- Verifier evidence path convention for this change: `openspec/changes/introduce-index-aware-verifier-workflow/verification/checkpoint-<n>/attempt-<m>/verifier-evidence.json`
- Governed-only execution evidence:
  - `index_contract`
  - `index_manifest_path`
  - `index_preflight_report_path`
  - `index_mode`
  - `deep_scanned_paths`
  - `deep_scan_reasons`
- Output format: `index preflight evidence + authoritative verifier findings json + verifier execution evidence json with coverage/saturation fields` (+ Gemini raw/report when enabled)
- Supported continuation overrides: `verify-only`, `dry-run`, `manual_pause`
- Gemini policy: mandatory on fresh confirmation because this change is `STRICT`
- Runner contract: `gemini-capture`
- Runner prompt inputs: implementation files or exercise fixtures covered by the checkpoint, the checkpoint-specific preflight report, and the active design/spec/task contract files
- Raw report path (when Gemini enabled): `openspec/changes/<change>/verification/checkpoint-<n>/attempt-<m>/gemini-raw.json`
- `report_path` (when Gemini enabled): `openspec/changes/<change>/verification/checkpoint-<n>/attempt-<m>/gemini-report.json`
- Maximum attempts: `2` total runner attempts before recovery/blocking
- JSON-response requirement: Gemini runner output MUST normalize to JSON; a non-JSON response is a checkpoint blocker unless recovery from an existing raw envelope succeeds
- Command-resolution evidence path: record the resolved Linux/Windows runner command in `openspec/changes/<change>/verification/checkpoint-<n>/attempt-<m>/verifier-evidence.json`
- Fallback behavior: retry once; if raw exists but normalized report is missing, rerun recovery using `input_raw_path -> report_path`; if recovery also fails, block the checkpoint
- Originating phase field: `apply` or `implementation_verify`
- Routing target on blocking findings: `openspec-repair-change`
- Continuation target on pass: `archive/report completion` unless the active caller is still in apply
- Skill entry point: `openspec-verify-change`

## Migration Plan

- Phase 1: define the repository-index contract and shared index sequence
- Phase 2: introduce index-maintainer agent and index skills
- Phase 3: update verifier invocation, shared verification sequence, and verify/apply/repair entry points
- Phase 4: update schema/templates/generation skills and exercise the workflow with index reuse, refresh, and deep-scan fallback evidence

## Open Questions

- How much execution evidence should be mandatory for digest or exported-surface checks when a repository cannot produce a lightweight structural snapshot automatically?
- Which generated or vendored directories should the default schema treat as directory-card-only scopes unless a project explicitly opts into finer indexing?

## Risks / Trade-offs

- If the index contract is too vague, the verifier will still need frequent source scans and the workflow gains little.
- If the index contract is too detailed, index maintenance becomes as expensive as code review.
- Shared-context index maintenance improves throughput but introduces a second trust boundary that must be documented clearly.
- Repositories with huge vendor trees still need disciplined governed-scope selection to avoid wasting effort on low-value indexing.
