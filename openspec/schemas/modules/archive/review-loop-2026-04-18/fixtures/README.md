# Review-Loop Fixtures

Reference index:

- `../REFERENCE-INDEX.md`

This directory keeps only the small set of review-run examples that still have
direct reference value during normal use.

Active reference runs:

- `review-runs/standalone-context-free-bootstrap-close`
- `review-runs/standalone-challenger-reopen-close`

These two examples are kept active because they show the shared loop mechanics
without caller-specific noise:

- standalone bootstrap into a working review session
- same-session working rerun after repair
- fresh challenger entry
- challenger failure promotion into the next working baseline
- final challenger closure

Archived validator-only fixture:

- `archive/validator-guard-2026-04-17/review-runs/openspec-artifact-gate-close`

That archived run still matters for automated guard mutation tests, but it is
not part of the normal human-facing reference surface.
