## ADDED Requirements

### Requirement: Verification Uses Conditional Shared Index Preflight
Governed `ai-enforced-workflow` verification SHALL run repository-index
preflight whenever the shared sequence needs a freshly compiled governed review
surface. The shared preflight sequence SHALL discover existing
repository-index artifacts, validate them against `repo-index-v1`, and decide
whether to reuse, refresh, or bypass the index for the active scope.

#### Scenario: Trusted existing index is reused
- **WHEN** governed scope coverage exists and passes contract, freshness, and confidence checks
- **THEN** preflight reuses the existing repository index
- **AND** it records the manifest path and trust result in preflight evidence

#### Scenario: Missing or stale index triggers refresh
- **WHEN** governed scope coverage is missing or stale
- **THEN** preflight invokes repository-index maintenance before verifier review
- **AND** it writes and validates `agent-spawn-decision-v1` before spawning
  `index-maintainer`
- **AND** it records refresh output paths and reasons in preflight evidence

### Requirement: Preflight Runs Deterministic Contract Lint Before Trust Reuse
Before a governed verifier run may reuse repository-index artifacts, Stage 0 preflight MUST perform deterministic contract lint over the active workflow artifacts and checkpoint surface.

The lint MUST reject at least:
- missing required Repository Index Plan fields
- inconsistent governed-scope objects
- missing `required_paths` / `required_axes` requirements in mirrored workflow
  contracts
- unsupported `review_axes` or derived `required_axes` values outside the
  shared findings/review vocabulary
- contradictory path-derivation rules across capability specs, shared sequence,
  schema instructions, and checkpoint artifacts

#### Scenario: Contract inconsistency blocks before verifier review
- **WHEN** the active checkpoint artifacts disagree about the required-path or
  required-axis universe
- **THEN** preflight blocks reuse before spawning `verify-reviewer`
- **AND** repair is routed to the upstream artifact layer instead of treating
  the index as trusted

### Requirement: Refresh Is Conditional On Dirty Closure, Not Mandatory By Default
Governed verification SHALL not run `index-maintainer` blindly before every
verifier pass. Preflight SHALL compute the dirty governed-path set plus any
transitive governed closure required by the checkpoint and refresh only when
that closure is dirty or trust validation fails.

Dirty-closure inputs MAY include:
- touched governed files in the current repair/apply session
- governed paths expanded from interface cards
- governed paths implied by the active evidence scope
- stale digest or missing-coverage signals from existing repository-index
  artifacts

#### Scenario: Clean checkpoint reuses without refresh
- **WHEN** deterministic contract lint passes and the active dirty closure is
  empty
- **THEN** preflight reuses trusted repository-index artifacts
- **AND** it does not invoke `index-maintainer` unnecessarily

#### Scenario: Dirty closure triggers targeted refresh
- **WHEN** a touched governed file or transitive governed path falls inside the
  active closure
- **THEN** preflight invokes `index-maintainer` for that closure before
  verifier review
- **AND** the preflight evidence records that refresh reason instead of
  pretending the index was reused

### Requirement: Repository Index Maintenance Uses A Dedicated Shared-Context Worker
Repository-index refresh SHALL be performed by a dedicated `index-maintainer` subagent that may share the main process context. The index-maintainer SHALL create or update repository-index artifacts only and SHALL NOT serve as the final review authority.

Any new `index-maintainer` session SHALL be preceded by an
`agent-spawn-decision-v1` record validated against
`openspec/schemas/ai-enforced-workflow/agent-spawn-decision-v1.schema.json`.

#### Scenario: Shared-context worker is limited to maintenance
- **WHEN** the workflow invokes `index-maintainer`
- **THEN** it may inherit main-process context relevant to repository structure, prior mapping docs, and touched scope
- **AND** it writes repository-index artifacts and refresh reports only
- **AND** it does not emit the final pass/block review decision for the checkpoint

### Requirement: Verifier Isolation Is Preserved Across Same-Session And Fresh Passes
The governed verifier SHALL remain the read-only `verify-reviewer`.
Repository-index maintenance context SHALL NOT become inherited verifier
memory, whether the workflow reuses the active verifier session for
same-session convergence or spawns a fresh verifier for fresh confirmation.

#### Scenario: Same-session convergence rerun keeps verifier isolation
- **WHEN** repair reruns a checkpoint inside the active verifier session
- **THEN** the verifier receives only the declared verification bundle plus
  explicit `index_context`
- **AND** it does not inherit maintainer working memory or parent reasoning
  transcript beyond the active verifier session itself

#### Scenario: Fresh confirmation remains a fresh verifier session
- **WHEN** the active working session first reaches zero findings and the
  workflow triggers fresh confirmation
- **THEN** verifier review starts as a fresh `verify-reviewer` instance
- **AND** the verifier receives only the declared verification bundle plus
  explicit `index_context`
- **AND** it does not inherit the maintainer's working memory or parent
  reasoning transcript

### Requirement: Verifier Consumes Index First And Escalates On Drift
When `index_context` is available, `verify-reviewer` SHALL read the repository index first and SHALL deep-scan source only when triggers or drift evidence require it.

The verifier MUST escalate to source deep scan when at least one of the following holds:
- a declared `deep_scan_trigger` fires
- governed-scope coverage is missing for the active review surface
- digest or exported-surface evidence contradicts the index
- runtime, artifact, or diff evidence conflicts with index claims

#### Scenario: Index-first review stays valid without full deep scan
- **WHEN** trusted repository-index coverage exists and no escalation trigger fires
- **THEN** the verifier MAY complete review using index-first reasoning plus cheap consistency checks
- **AND** execution evidence records that source deep scan was not required

#### Scenario: Drift evidence forces deep scan
- **WHEN** the verifier detects an index conflict or declared trigger
- **THEN** it MUST inspect the relevant source files directly
- **AND** execution evidence records `deep_scanned_paths` and `deep_scan_reasons`

### Requirement: Exhaustive Review Coverage Is Recorded Before Acceptance
Authoritative verifier output SHALL require explicit review-coverage and saturation accounting. Default governed verification SHALL reject results that stop after the first few blockers without completing the declared review surface.

Verifier execution evidence MUST record:
- `verifier_output_path`
- `reviewed_paths`
- `skipped_paths`
- `reviewed_axes`
- `unreviewed_axes`
- `coverage_status` (`complete|partial`)
- `saturation_status` (`exhaustive|early_stop`)
- optional `early_stop_reason`
- `skip_reasons` (required when `skipped_paths` is non-empty)

Acceptance rules:
- `coverage_status=complete` is required for authoritative completion
- `saturation_status=exhaustive` is required for authoritative completion
- `skipped_paths` MUST be empty for authoritative completion unless a future explicit policy exception says otherwise
- `unreviewed_axes` MUST be empty for authoritative completion
- default governed verification treats `partial` or `early_stop` as non-authoritative and blocks checkpoint acceptance

#### Scenario: Blockers do not authorize premature stop
- **WHEN** the verifier finds one or more blocking findings early in the pass
- **THEN** it still continues through the remaining governed paths and required review axes
- **AND** the final authoritative result reflects the full supported finding set for that pass

#### Scenario: Early-stopped review is rejected
- **WHEN** verifier execution evidence records `coverage_status=partial` or `saturation_status=early_stop`
- **THEN** the checkpoint remains blocked by default
- **AND** repair routing does not treat the finding set as exhaustive acceptance evidence

#### Scenario: Validators compute completion instead of trusting self-report alone
- **WHEN** authoritative checkpoint validation runs
- **THEN** it derives completeness from the required path/axis universe plus `reviewed_paths`, `skipped_paths`, `reviewed_axes`, and `unreviewed_axes`
- **AND** it does not trust `coverage_status` or `saturation_status` alone when the underlying evidence contradicts them

### Requirement: Index Preflight Evidence Is Authoritative
Governed verification checkpoints SHALL record repository-index preflight evidence in addition to verifier findings and verifier execution evidence.

Preflight evidence MUST record at least:
- index contract id
- manifest path
- `manifest_present`
- `coverage_report_path`
- `refresh_report_path`
- `governed_scopes`
- `index_mode` (`reused|refreshed|bypassed`)
- `required_paths`
- `required_axes`
- `deep_scan_candidates`
- refresh or bypass reason
- `fallback_policy`

`coverage_report_path` and `refresh_report_path` MUST record the canonical report paths for the governed scope even when the active checkpoint bypasses index reuse or refresh. `deep_scan_candidates` MUST always be present and MAY be empty when preflight finds no known deep-scan triggers before verifier review.

The preflight report is the frozen review-surface snapshot for the governed
checkpoint. Verifier execution evidence and later validation SHALL use its
`required_paths` and `required_axes` as the authoritative surface for coverage
checking instead of re-deriving a different universe later in the pass.

#### Scenario: Bypassed index is explicit
- **WHEN** preflight cannot provide a trusted repository index and policy allows verification to continue by source review
- **THEN** the checkpoint records `index_mode: bypassed`
- **AND** `manifest_path` still records the canonical attempted manifest location while `manifest_present: false` makes the missing manifest explicit
- **AND** later verifier evidence makes the resulting deep-scan behavior explicit rather than silently claiming index-first review

### Requirement: Verify, Apply, And Repair Skills Share The Same Preflight
Any verify-related workflow skill in governed `ai-enforced-workflow` projects SHALL consume the same index-preflight contract instead of reimplementing ad hoc repository-document discovery.

#### Scenario: Artifact and implementation verification reuse the same preflight model
- **WHEN** `openspec-artifact-verify` and `openspec-verify-change` run on governed scopes
- **THEN** both use the same shared index-preflight sequence and evidence contract
- **AND** repair or apply reruns do not bypass repository-index preflight by default

### Requirement: Required Review Axes And Fallback Policy Are Derivable In Every Preflight Outcome
The workflow SHALL define an authoritative source for required review axes and fallback behavior for every allowed preflight outcome, including `index_mode=bypassed`.

Required review-axis derivation rules:
- when a governed file card is used, required axes come from that card's `review_axes`
- when a governed directory or interface card is used, required axes come from that card's `review_axes`
- when multiple governed entries are active, required axes are the deduplicated union of every active entry's `review_axes`
- when `index_mode=bypassed`, required axes default to `Completeness`, `Correctness`, and `Coherence`, plus any additional checkpoint-declared axes from the Repository Index Plan

Required reviewed-path derivation rules:
- when a governed file card is used, required paths come from that file card's `path`
- when a governed directory card is used, required paths are the concrete file paths under `covered_prefixes` that intersect the active evidence scope
- when a governed interface card is used, required paths come from that card's `governed_paths`
- when multiple governed entries are active, required paths are the deduplicated union of every active entry's required paths
- when `index_mode=bypassed`, required paths default to the normalized file paths from `evidence_paths_or_diff_scope`
- when a governed checkpoint also includes nongoverned evidence files, required
  paths additionally include the remaining normalized
  `evidence_paths_or_diff_scope` paths not already claimed by the active
  governed entries
- the checkpoint-specific preflight report passed via
  `index_context.preflight_report_path` is authoritative auxiliary evidence for
  verifier orchestration, but it is NOT part of `required_paths` by default
  unless a future checkpoint policy explicitly opts in

Fallback-policy rules:
- the Repository Index Plan MUST declare whether missing or stale index coverage leads to `refresh`, `bypass`, or `block`
- preflight evidence MUST record the active `fallback_policy` decision used for the checkpoint

#### Scenario: Bypassed review still has authoritative required axes
- **WHEN** preflight allows `index_mode=bypassed`
- **THEN** the verifier still has an authoritative source of required review axes
- **AND** review-completion evidence can be validated without repository-index cards

#### Scenario: Bypassed review still has authoritative required paths
- **WHEN** preflight allows `index_mode=bypassed`
- **THEN** the verifier still has an authoritative source of required reviewed paths
- **AND** `coverage_status` can be validated against normalized evidence-scope file paths

#### Scenario: Mixed governed review still accounts for nongoverned evidence
- **WHEN** preflight validates a governed checkpoint whose evidence scope
  includes nongoverned change artifacts
- **THEN** the required reviewed-path universe includes both the paths derived
  from governed entries and the remaining normalized evidence-scope paths
- **AND** checkpoint acceptance still validates `coverage_status` against that
  full union
