# Review-Loop Reference Index

Use this file as the single reference entrypoint for the shared
working/challenger review loop.

## Authoritative Runtime Surface

Core contracts and runtime gate:

- `contracts/review-loop-core-v1.json`
- `contracts/review-loop-reopen-record-v1.json`
- `contracts/review-loop-openspec-adapter-v1.json`
- `contracts/review-loop-standalone-adapter-v1.json`
- `contracts/review-loop-standalone-spawn-decision-v1.json`
- `../../ai-enforced-workflow/agent-spawn-decision-v1.schema.json`
- `bin/review_loop_guard.py`

Resolver authority layer:

- `transition-resolver/contracts/working-session-normalization-v1.json`
- `transition-resolver/contracts/transition-resolver-input-v1.json`
- `transition-resolver/contracts/transition-resolver-routing-v1.json`
- `transition-resolver/contracts/transition-resolver-decision-v1.json`
- `transition-resolver/bin/normalize_working_session.py`
- `transition-resolver/bin/transition_resolver_resolve.py`
- `transition-resolver/bin/transition_resolver_validate.py`
- `transition-resolver/CALLER-INTEGRATION.md`

Human-facing explanation:

- `README.md`
- `VERIFY-IMPLEMENTATION.md`

## Active Reference Runs

These are the primary concrete examples to study during normal use.

### `standalone-context-free-bootstrap-close`

Path:

- `fixtures/review-runs/standalone-context-free-bootstrap-close`

Shows:

- bootstrap into working review without caller-specific wrapper state
- same-session working rerun after repair
- fresh challenger entry
- final challenger closure

### `standalone-challenger-reopen-close`

Path:

- `fixtures/review-runs/standalone-challenger-reopen-close`

Shows:

- challenger finds issues
- failed challenger is promoted into the next working baseline
- `reopen-record.json` as the machine-readable promotion record
- rerun under the promoted working baseline
- later fresh challenger closure

## Archived But Still Useful

These are no longer part of the main active surface, but still have targeted
reference value.

### Validator-only caller-shaped run

Path:

- `fixtures/archive/validator-guard-2026-04-17/README.md`
- `fixtures/archive/validator-guard-2026-04-17/review-runs/openspec-artifact-gate-close`

Use when:

- you want a docs-first caller-shaped run rather than a standalone loop example
- you need to understand the Stage A validator's negative guard mutations

### Transition-resolver hardening harness

Path:

- `transition-resolver/archive/test-harness-2026-04-17/README.md`
- `transition-resolver/archive/test-harness-2026-04-17/fixtures/README.md`

Use when:

- you want positive and negative resolver input cases
- you want cross-caller orchestration sequences
- you need examples of easy-to-misroute transition states

## Routing Guidance

Use the smallest surface that answers the question:

- need the rules: read the contracts plus `CALLER-INTEGRATION.md`
- need a real loop example: start with the active standalone runs
- need validator mutation context: use the archived caller-shaped run
- need resolver edge cases: use the archived resolver harness
