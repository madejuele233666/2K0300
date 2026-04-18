# index-sequence/default

Shared repository-index cache helper for Stage A `ai-enforced-workflow`
verification.

Consumers:
- `$CODEX_HOME/skills/openspec-artifact-verify/SKILL.md`
- `$CODEX_HOME/skills/openspec-verify-change/SKILL.md`
- `$CODEX_HOME/skills/openspec-apply-change/SKILL.md`
- `$CODEX_HOME/skills/openspec-repair-change/SKILL.md`
- `openspec/schemas/ai-enforced-workflow/schema.yaml`

## Purpose

Use repository-index artifacts as optional cache support for review
orientation.

This helper may:
- discover existing index artifacts
- validate whether they look usable
- refresh them when the caller requests maintenance and the cache is worth
  refreshing
- hand normalized cache context to verifier orchestration

This helper does not:
- act as Stage 0 gate
- compile an authoritative review surface
- define `required_paths` or `required_axes` for implementation closure
- block review when cache is missing or stale

## Contracts

- Repository-index contract id: `repo-index-v1`
- Canonical repository-index root: `docs/repo-index/`
- Canonical manifest: `docs/repo-index/manifest.json`
- Canonical reports:
  - `docs/repo-index/reports/coverage.json`
  - `docs/repo-index/reports/refresh.json`
- Maintenance agent: `.codex/agents/index-maintainer.toml`
- Maintenance skill entry point: `openspec-index-maintain`
- Cache-helper skill entry point: `openspec-index-preflight`

## Inputs

The caller supplies:
- `change`
- `review_goal` (`implementation_correctness`)
- optional `surface_hint` (`docs_first|source_first`)
- `risk_tier`
- `evidence_paths_or_diff_scope`
- `fallback_policy` (`refresh|bypass`)
- optional `expected_index_root`
- optional `governed_scopes`
- optional `preflight_report_path`
- optional `spawn_decision_path`

Rules:
- docs-first review normally bypasses cache helper work
- source-first review MAY use cache helper work
- callers SHOULD use a non-blocking fallback for review

## Cache Evidence Contract

The cache-helper report is normalized cache evidence written to a
checkpoint-scoped path. It MUST contain:
- `contract`
- `manifest_path`
- `manifest_present`
- `coverage_report_path`
- `refresh_report_path`
- `cache_mode`
- `fallback_policy`
- `reason`
- `cache_inputs`

The report MAY also include:
- `index_root`
- `refreshed_entries`
- `missing_entries`
- `stale_entries`

`cache_mode` MUST be one of:
- `used`
- `missed`
- `stale_but_ignored`
- `refreshed`
- `bypassed`

Field rules:
- `contract` MUST be `repo-index-v1`
- `manifest_path` MUST record the canonical attempted manifest location even
  when the manifest does not exist
- `coverage_report_path` and `refresh_report_path` MUST record the canonical
  report locations even when `cache_mode=missed|bypassed`
- `cache_inputs` MUST record the index artifacts or cache-derived hints
  actually handed to the verifier; it MAY be empty

## Trust And Refresh Rules

Use existing cache when:
- manifest exists at the resolved path
- manifest contract looks coherent
- the cache appears relevant to the current review surface
- no obvious drift makes it misleading

Refresh cache when:
- the caller wants cache help
- cache coverage is missing or obviously stale
- a scoped refresh is likely to help future review orientation
- policy allows refresh

Ignore stale cache when:
- the cache looks outdated or contradictory
- source-first review is cheaper than blocking on refresh
- the caller can continue without cache authority

Bypass cache when:
- the checkpoint is docs-first
- the caller intentionally chooses source-first review
- the repository has no useful index artifacts for the active surface

## Sequence

1. Resolve the canonical repository-index root.
2. Derive canonical manifest and report paths.
3. Discover existing repository-index artifacts relevant to the active review
   surface.
4. Validate manifest and report coherence when artifacts exist.
5. Decide cache outcome:
- `used` when existing artifacts look usable
- `missed` when no usable cache exists
- `stale_but_ignored` when stale cache is detected but source-first review
  continues
- `refreshed` when maintenance successfully restores usable cache
- `bypassed` when the caller intentionally skips cache helper work
6. If refresh is required, write `agent-spawn-decision-v1` before invoking
   `openspec-index-maintain`.
6a. Invoking a workflow entrypoint that declares `index-sequence/default` explicitly authorizes the `index-maintainer` sub-agents required by that cache-refresh step; this authorization is
    limited to the maintenance sessions required by the workflow.
7. If refresh is required, invoke `openspec-index-maintain` with the active
   review surface and any scoped refresh hints.
8. Write normalized cache evidence JSON.
9. Hand normalized `index_context` to verifier orchestration:
- `contract`
- `manifest_path`
- `manifest_present`
- `preflight_report_path`
- `cache_mode`
- `fallback_policy`

## Failure Semantics

- Missing cache MUST NOT block review.
- Stale cache MUST NOT block review when source-first inspection is still
  possible.
- Refresh failure MUST be recorded explicitly, then the caller should continue
  with `cache_mode=stale_but_ignored` or `cache_mode=missed` unless the caller
  is running an explicit cache-maintenance task.
- Docs-first review normally runs with `cache_mode=bypassed`.
