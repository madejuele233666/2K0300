# Exercise Evidence

## Schema-Driven Verifier Invocation

- Evidence file:
  `openspec/changes/introduce-verify-subagent-workflow/verification/schema-driven-invocation-evidence.json`
- Captures that `openspec instructions apply` now emits shared sequence
  `verify-sequence/default`, verifier-subagent bundle fields, normalized JSON
  requirement, and routing gate policy.

## Normalized Local Review Output

- `openspec/changes/introduce-verify-subagent-workflow/verification/schema-contract-subagent-review.json`
- `openspec/changes/introduce-verify-subagent-workflow/verification/entrypoint-subagent-review.json`

Both files are normalized JSON local-review outputs from `verify-reviewer`.

## Separate Retry-Budget Behavior

- Evidence file:
  `openspec/changes/introduce-verify-subagent-workflow/verification/retry-budget-exercise.json`
- Shows independent counters for:
  - `artifact_rerun_budget`
  - `implementation_auto_fix_budget`

## Runner Recovery Behavior (Active Platform)

- Active platform command:
  `./openspec/bin/gemini_capture.sh --input-raw-path "openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-1-gemini-raw.json" --report-path "openspec/changes/introduce-verify-subagent-workflow/verification/recovery-demo-report.json" --require-json-response`
- Output report:
  `openspec/changes/introduce-verify-subagent-workflow/verification/recovery-demo-report.json`
- The recovery run succeeded and produced normalized JSON from an existing raw envelope.

## Optional Manual Convenience Invocation

Manual invocation remains optional convenience only; it is not required for acceptance.
