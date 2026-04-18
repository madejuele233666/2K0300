# Verification Notes

This change now follows the shared review-loop semantics from:

- `openspec/schemas/ai-enforced-workflow/verification-sequence.md`
- `openspec/schemas/modules/review-loop/README.md`
- `openspec/schemas/modules/review-loop/VERIFY-IMPLEMENTATION.md`

Active review semantics:

- `review_goal=implementation_correctness`
- `review_phase=docs_first|source_first`
- working reruns stay in the same working session
- challenger must be fresh
- challenger findings reopen through `reopen-record.json` and promote the
  failed challenger session into the next working baseline

Active path conventions for this change:

- docs-first gate:
  - `review-runs/docs-first-gate/...`
  - summary mirrors: `docs-first-working-*.json`,
    `docs-first-challenger-*.json`
- source-first checkpoints:
  - `review-runs/sensor-closure/...`
  - `review-runs/actuator-control/...`
  - `review-runs/final-phase-a-closure/...`

Historical pre-unified records live under:

- `archive/pre-unified-review-loop-2026-04-16/`

Those archived records are preserved for traceability only.
They are not authoritative for the current review-loop semantics.
