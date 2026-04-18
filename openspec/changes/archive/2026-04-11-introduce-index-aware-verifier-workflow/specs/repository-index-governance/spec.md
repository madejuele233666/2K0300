## ADDED Requirements

### Requirement: Canonical Repository Index Contract
`ai-enforced-workflow` SHALL define a canonical repository-index contract `repo-index-v1` for governed scopes. The contract SHALL specify the canonical artifact layout, manifest path semantics, entry kinds, freshness metadata, and review-decision fields needed to support index-first verification.

The canonical repository-index artifact set MUST include:
- a manifest
- one or more entry cards
- both a coverage report path and a refresh report path

The contract MUST identify:
- contract id
- governed scope
- canonical root path
- canonical manifest path
- canonical entry-directory paths
- canonical report paths
- entry kind (`directory`, `file`, or `interface`)
- freshness metadata
- digest or equivalent staleness signal

Default canonical `repo-index-v1` layout:
- root: `docs/repo-index/`
- manifest: `docs/repo-index/manifest.json`
- file cards: `docs/repo-index/files/<escaped-path>.yaml`
- directory cards: `docs/repo-index/directories/<escaped-path>.yaml`
- interface cards: `docs/repo-index/interfaces/<escaped-path>.yaml`
- latest coverage report: `docs/repo-index/reports/coverage.json`
- latest refresh report: `docs/repo-index/reports/refresh.json`

Escaped-path rule:
- replace `/` with `__`
- preserve the source file extension in the escaped filename

Override policy:
- repositories MAY override the default root only when the override is explicitly declared in design/tasks and consumed consistently by preflight + verifier entrypoints

#### Scenario: Governed project-owned code uses canonical index artifacts
- **WHEN** a project marks a repository area as governed by repository-index rules
- **THEN** repository-index artifacts follow `repo-index-v1`
- **AND** the workflow can discover them without project-specific guesswork

### Requirement: Repository Index Entries Are Decision-Oriented
Repository-index entries SHALL be detailed enough to support review decisions but SHALL NOT become implementation-level restatements of source code.

Each governed file-level entry MUST provide enough information to decide:
- what the file owns
- what it must not own
- what review axes matter
- when deep source scan becomes mandatory

Each file-level entry MUST contain:
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

Each directory-level entry MUST contain:
- `path`
- `kind=directory`
- `owner_role`
- `responsibilities`
- `must_not_do`
- `covered_prefixes`
- `review_axes`
- `deep_scan_triggers`
- `confidence`
- `digest_strategy`
- `last_verified_at`

Each interface-level entry MUST contain:
- `path`
- `kind=interface`
- `owner_role`
- `responsibilities`
- `must_not_do`
- `governed_paths`
- `inputs`
- `outputs`
- `invariants`
- `review_axes`
- `deep_scan_triggers`
- `confidence`
- `digest`
- `last_verified_at`

Repository-index-enabled review evidence MUST also be able to express:
- `reviewed_paths`
- `skipped_paths`
- `reviewed_axes`
- `unreviewed_axes`
- `coverage_status`
- `saturation_status`
- optional `early_stop_reason`
- `skip_reasons` (required when `skipped_paths` is non-empty)

#### Scenario: Index entry is sufficiently specific for review
- **WHEN** the verifier reads a governed file card
- **THEN** it can decide whether the file appears to stay within declared ownership
- **AND** it knows which triggers require source deep scan
- **AND** the card does not need to describe local control flow or helper-level implementation detail

#### Scenario: Over-detailed index entry is rejected
- **WHEN** an index entry attempts to mirror implementation detail, line-by-line flow, or ephemeral helper logic
- **THEN** the entry fails repository-index review as high-drift and low-value documentation

#### Scenario: Index contract supports exhaustive review accounting
- **WHEN** verifier execution evidence is recorded for a governed review
- **THEN** the workflow can tell which paths and review axes were actually covered
- **AND** it can distinguish exhaustive review from partial or early-stopped review

### Requirement: Manifest And Report Schemas Are Normative
`repo-index-v1` SHALL normatively define the minimum schema for the manifest and its required reports so preflight decisions remain deterministic across implementations.

The canonical manifest at `docs/repo-index/manifest.json` MUST contain:
- `contract`
- `root`
- `generated_at`
- `governed_scopes`
- `entries`
- `coverage_report_path`
- `refresh_report_path`

Each `entries` item MUST contain:
- `path`
- `kind`
- `card_path`
- `confidence`
- `last_verified_at`

Each `governed_scopes` item MUST contain:
- `scope_id`
- `scope_root`
- `allowed_entry_kinds`
- `fallback_policy`
- `entry_refs`

The canonical coverage report at `docs/repo-index/reports/coverage.json` MUST contain:
- `contract`
- `manifest_path`
- `generated_at`
- `governed_scopes`
- `scope_results`
- `coverage_status`

The canonical refresh report at `docs/repo-index/reports/refresh.json` MUST contain:
- `contract`
- `manifest_path`
- `generated_at`
- `governed_scopes`
- `updated_entries`
- `unchanged_entries`
- `failed_entries`
- `refresh_reason`

Each `scope_results` item in the canonical coverage report MUST contain:
- `governed_scope`
- `covered_entries`
- `missing_entries`
- `coverage_status`

Each item in `updated_entries`, `unchanged_entries`, and `failed_entries` MUST contain:
- `path`
- `kind`
- `card_path`
- `status`
- optional `reason`

#### Scenario: Preflight trust decision uses stable manifest/report fields
- **WHEN** repository-index preflight validates a governed scope
- **THEN** it can derive governed-scope mapping, entry references, freshness signals, and coverage state from the canonical manifest/report schemas
- **AND** different implementations do not need private conventions to decide whether the index is trusted

#### Scenario: Multi-scope checkpoints stay deterministic
- **WHEN** a single checkpoint covers more than one governed scope
- **THEN** the canonical coverage and refresh reports still encode all governed scopes explicitly
- **AND** preflight can resolve coverage status per scope without inventing private report naming rules

### Requirement: Review Axes Are Defined For Every Entry Kind
Any repository-index entry kind that can satisfy governed review coverage SHALL declare authoritative `review_axes`.

For `ai-enforced-workflow`, supported review-axis vocabulary MUST remain within
`Completeness`, `Correctness`, and `Coherence` unless the shared findings
contract is extended in the same change. Coverage exhaustion and stop-state
tracking MUST remain encoded through `coverage_status` and
`saturation_status`, not through additional review-axis names.

#### Scenario: Directory and interface cards carry review axes
- **WHEN** a governed scope is represented by a directory card or interface card
- **THEN** that card still declares `review_axes`
- **AND** the verifier can derive required reviewed-axis coverage without assuming file-card-only semantics

#### Scenario: Unsupported axis names are rejected
- **WHEN** a file, directory, or interface card declares a review axis outside
  the active shared findings vocabulary
- **THEN** deterministic contract lint rejects the governed review surface
- **AND** preflight does not freeze a `required_axes` universe the verifier
  cannot encode or route coherently

### Requirement: Active Review-Axis Universe Is Deterministic Across Multiple Governed Entries
When a checkpoint spans multiple governed entries or scopes, the authoritative required review-axis universe SHALL be the deduplicated union of every active entry's `review_axes`, plus any checkpoint-declared axes that apply to the active review surface.

#### Scenario: Multi-scope checkpoint aggregates axes deterministically
- **WHEN** a checkpoint covers both a file card and an interface card
- **THEN** the required review-axis universe is the union of both cards' `review_axes`, deduplicated by axis name
- **AND** validators can compare `reviewed_axes` and `unreviewed_axes` against that same universe without guessing

### Requirement: Trust Invalidation Is Closure-Based
Repository-index trust SHALL be invalidated from the active governed dirty set
plus any transitive governed closure required by the checkpoint review surface.

Dirty-set invalidation rules:
- a changed governed file invalidates its own card immediately
- an interface card invalidates any governed paths it pulls into the checkpoint
  through `governed_paths`
- a checkpoint that mixes governed and nongoverned evidence invalidates the
  affected governed closure before reuse is allowed
- reuse is invalid when any required path in that closure has stale digest,
  missing coverage, or contradictory report evidence

#### Scenario: Interface card expands the invalidation closure
- **WHEN** a checkpoint reviews `verification-sequence.md` and that interface
  card governs `.codex/agents/verify-reviewer.toml`
- **THEN** trust validation includes the transitive agent path in the active
  invalidation closure
- **AND** preflight cannot claim complete trusted coverage while that transitive
  path is omitted

### Requirement: Reviewed-Path Universe Is Deterministic
`repo-index-v1` SHALL define the authoritative reviewed-path universe for each permitted entry kind so `reviewed_paths`, `skipped_paths`, and `coverage_status` can be validated deterministically.

Path-derivation rules:
- file card: the required path is the card's `path`
- directory card: the required paths are the concrete file paths in the active evidence scope that fall under `covered_prefixes`
- interface card: the required paths are the files listed under `governed_paths`
- bypassed review: the required paths are the normalized file paths from `evidence_paths_or_diff_scope`
- mixed governed review: the required paths are the deduplicated union of
  paths derived from active governed entries plus any remaining normalized
  `evidence_paths_or_diff_scope` paths not already claimed by those entries

Normalization rule:
- `reviewed_paths` and `skipped_paths` MUST record concrete workspace-accessible
  file paths, not card paths or raw prefixes
- use repository-relative paths for files under the active repository root
- use absolute filesystem paths for nongoverned evidence outside the repository
  root, such as `$CODEX_HOME/skills/...`

Completion-derivation rules:
- `coverage_status=complete` is valid only when every required path appears in `reviewed_paths`
- any required path in `skipped_paths` forces `coverage_status=partial`
- `saturation_status=exhaustive` is valid only when no required path or required review axis remains unreviewed
- authoritative completion requires `skipped_paths=[]` and `unreviewed_axes=[]`

Skip-eligibility rules:
- a required path MAY be skipped only when it is unreadable, unavailable in the active environment, or intentionally excluded by a checkpoint policy explicitly declared in the Repository Index Plan
- every skipped required path MUST record a machine-readable skip reason

#### Scenario: Directory-card review expands to concrete file paths
- **WHEN** a directory card covers `new/code/runtime/` and the active evidence scope touches two files under that prefix
- **THEN** those two concrete file paths MUST be represented explicitly in `reviewed_paths` or `skipped_paths` for coverage accounting
- **AND** `coverage_status=complete` remains valid only when both paths appear in `reviewed_paths`
- **AND** card-path-only accounting is insufficient

#### Scenario: Skipped required path blocks complete coverage
- **WHEN** a required path appears in `skipped_paths`
- **THEN** `coverage_status=complete` is invalid
- **AND** authoritative completion remains blocked until the path is reviewed or the Repository Index Plan changes the required path universe

#### Scenario: Mixed governed and nongoverned evidence stays in scope
- **WHEN** a checkpoint reviews governed indexed files and additional
  nongoverned evidence files in the same `evidence_paths_or_diff_scope`
- **THEN** the authoritative required-path universe is the union of both sets
- **AND** `coverage_status=complete` remains invalid until the nongoverned
  evidence files also appear in `reviewed_paths`

### Requirement: Granularity Is Risk-Based, Not Uniform
`repo-index-v1` SHALL support multiple entry kinds so repositories can index high-value scopes precisely without paying the cost of full file-card coverage for low-value trees.

Granularity rules:
- `directory` cards MAY summarize large vendor, generated, or third-party areas
- `file` cards SHALL be the default for governed project-owned files and direct dependency touchpoints
- `interface` cards SHALL be used where public contracts or workflow-gate files carry cross-cutting invariants

#### Scenario: Large vendor tree does not require universal file-card coverage
- **WHEN** a project depends on a large vendor or generated tree outside the governed change surface
- **THEN** a directory card MAY define ownership and escalation policy for that area
- **AND** file-level indexing is required only for direct touchpoints or explicitly governed files

### Requirement: Freshness And Drift Signals Are Auditable
Repository-index artifacts SHALL include enough freshness and drift metadata to support refresh or deep-scan decisions.

The workflow MUST be able to detect at least:
- missing coverage for governed scope
- stale digest or equivalent mismatch
- insufficient confidence
- contradictory evidence from other workflow artifacts

#### Scenario: Stale index cannot be silently trusted
- **WHEN** a governed file card has a digest mismatch or stale freshness signal
- **THEN** the workflow cannot silently accept the existing card as trusted context
- **AND** it must refresh the card or escalate to deep source scan

### Requirement: Deep-Scan Triggers Are Part Of The Contract
Each governed entry SHALL define explicit deep-scan escalation conditions. The verifier MUST escalate to source review when a declared trigger fires or when equivalent drift evidence invalidates the card.

Deep-scan triggers MAY include:
- new forbidden dependency or include
- exported surface change
- digest mismatch
- missing governed-scope coverage
- contradictory runtime or artifact evidence

#### Scenario: Trigger forces source review
- **WHEN** a governed file card says a new vendor include forces deep scan
- **AND** the active diff adds that include
- **THEN** the verifier MUST inspect the source for that file
- **AND** execution evidence records the deep-scan reason
