## MODIFIED Requirements

### Requirement: Governed STANDARD/STRICT Changes Declare A Repository Index Plan
When a `STANDARD` or `STRICT` `ai-enforced-workflow` change governs repository-owned code or direct dependency touchpoints, the change artifacts SHALL declare a Repository Index Plan.

The Repository Index Plan MUST identify:
- index contract id
- canonical repository-index root
- stable governed scope objects used by manifest generation and Stage 0 preflight
- per-scope granularity policy
- fallback policy (`refresh|bypass|block` semantics for missing or stale coverage)
- shared index-preflight sequence
- `index-maintainer` agent path
- required index skill entry points
- preflight evidence path(s) and required-universe fields
- verifier invocation template
- deep-scan escalation policy

Each governed scope object MUST declare at least:
- `scope_id`
- `scope_root`
- `allowed_entry_kinds`
- `fallback_policy`
- `entry_refs` or an equivalent exact mapping to the governed entries

#### Scenario: Design and tasks expose the same Repository Index Plan
- **WHEN** a governed `STANDARD` or `STRICT` change is reviewed
- **THEN** its design and tasks both name the Repository Index Plan explicitly
- **AND** the verifier can tell which stable scope ids map to which roots, entry kinds, and governed entries
- **AND** the active fallback policy is reviewable before implementation starts

### Requirement: Governed Verification Uses Conditional Stage 0 Index Preflight
Governed `ai-enforced-workflow` verification SHALL run Stage 0 index preflight whenever the shared verification sequence requires a freshly compiled governed review surface.

Stage 0 MUST:
- discover canonical repository-index artifacts
- run deterministic contract lint over the active governed workflow artifacts
- derive the active dirty governed-path set plus any transitive governed closure
- validate contract/freshness/coverage
- refresh or bypass according to shared policy
- emit authoritative preflight evidence

#### Scenario: Verifier is not spawned before preflight
- **WHEN** a governed artifact or implementation verification step runs
- **THEN** the workflow executes the shared index-preflight sequence first
- **AND** verifier review consumes the resulting preflight evidence rather than rediscovering repository knowledge ad hoc

#### Scenario: Clean governed rerun skips unnecessary maintenance
- **WHEN** Stage 0 contract lint passes and the active dirty governed closure is
  empty
- **THEN** the workflow may reuse trusted repository-index artifacts
- **AND** it does not invoke `index-maintainer` only because a verifier rerun is
  happening

#### Scenario: Same-session rerun reuses the active verifier session
- **WHEN** repair resolves findings but the shared sequence does not require a
  fresh confirmation pass
- **THEN** the workflow may rerun Stage 0 only if trust drift or
  surface-change recompilation is needed
- **AND** it reuses the active verifier session instead of spawning a fresh
  verifier only because the checkpoint is rerunning

### Requirement: Governed Verifier Invocation Is Session-Oriented And Index-Aware
Governed `ai-enforced-workflow` verification SHALL use the shared
session-oriented verifier invocation contract in addition to the minimal
verification bundle.

The minimal verification bundle MUST preserve:
- `change`
- `mode`
- `risk_tier`
- `evidence_paths_or_diff_scope`
- `findings_contract`

The invocation MUST extend with:
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

The checkpoint-specific preflight report carried through
`index_context.preflight_report_path` SHALL be treated as authoritative
auxiliary evidence for verifier coordination. It SHALL NOT be added to
`evidence_paths_or_diff_scope` or the derived `required_paths` universe by
default unless a future checkpoint policy explicitly opts in.

When a same-session rerun is active, the workflow SHALL reuse the active
verifier session. When a fresh confirmation pass is required, the workflow
SHALL spawn a fresh verifier session and record `agent-spawn-decision-v1`
before that spawn occurs.

Any new verifier or `index-maintainer` session SHALL be preceded by an
`agent-spawn-decision-v1` record validated against
`openspec/schemas/ai-enforced-workflow/agent-spawn-decision-v1.schema.json`.
Same-session reruns SHALL NOT open a new verifier session unless shared policy
requires fresh confirmation or an exception record explicitly permits a new
spawn.

#### Scenario: Verifier receives explicit index context
- **WHEN** governed verification runs with trusted or refreshed repository-index artifacts
- **THEN** the verifier receives explicit `index_context`
- **AND** it uses that context before deciding whether source deep scan is required

#### Scenario: Bypassed preflight still serializes manifest state coherently
- **WHEN** governed verification runs with `index_mode=bypassed`
- **THEN** `index_context.manifest_path` still records the canonical attempted manifest path
- **AND** `index_context.manifest_present` explicitly records whether that manifest exists

### Requirement: Authoritative Verification Requires Review-Completion Evidence
Governed `ai-enforced-workflow` checkpoints SHALL treat findings JSON as authoritative only when verifier execution evidence also proves exhaustive review completion.

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

When more than one governed entry or scope is active, the verifier and any validation step SHALL compute the required review-axis universe as the deduplicated union of all applicable `review_axes` values, plus any checkpoint-declared axes for the active review surface. If `skipped_paths` is non-empty, `skip_reasons` SHALL be populated for each skipped path.

Default acceptance rules:
- `coverage_status=complete`
- `saturation_status=exhaustive`
- `skipped_paths=[]`
- `unreviewed_axes=[]`

#### Scenario: Early blocker does not end the pass
- **WHEN** the verifier finds one or more blocking findings early in the governed review surface
- **THEN** the workflow still requires the verifier to continue through remaining governed paths and required review axes
- **AND** the checkpoint rejects `early_stop` as authoritative completion by default

### Requirement: Verify, Apply, And Repair Share The Same Governed Preflight Contract
Governed `ai-enforced-workflow` entrypoints SHALL not implement repository-index discovery ad hoc. Verify, apply, and repair flows MUST all consume the same shared preflight contract before final verifier review.

#### Scenario: Repair reruns remain governed by preflight
- **WHEN** repair or apply reruns a governed verification step
- **THEN** the workflow reuses or refreshes repository-index artifacts through the shared preflight contract
- **AND** it does not bypass Stage 0 merely because a prior verifier pass already ran
