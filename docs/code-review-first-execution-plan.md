# Code-Review-First Execution Plan

## Status

This file has been downgraded to a historical single-file reference.

The archived split-by-boundary document set now lives under:

- `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/README.md`

## Why This File Was Downgraded

The old single-file plan became too large and mixed together:
- core-loop rules
- rollout sequencing
- optional enhancements
- compatibility details
- stop rules

That structure made it too easy to blur the boundary between:
- the minimal review path
- optional sidecar capabilities

The split document set fixes that.

## Use This Reading Order Instead

1. `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/README.md`
2. `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/01-principles-and-boundaries.md`
3. `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/02-layered-rollout-overview.md`
4. `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/03-layer-1-core-review-loop.md`
5. `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/04-layer-2-lightweight-scope-summary.md`
6. `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/05-layer-3-tracked-findings.md`
7. `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/06-layer-4-variant-analysis.md`
8. `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/07-layer-5-contract-hardening.md`
9. `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/08-migration-validation-and-stop-rules.md`

## Relationship To Other Documents

- `docs/auto-review-architecture.md`
  - current-state architecture map
- `docs/code-review-first-architecture.md`
  - high-level target architecture
- `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/`
  - authoritative layered implementation plan
