# Verification Execution Evidence

## Checkpoint 1

- Logical runner contract: `gemini-capture`
- Resolved platform command:
  `./openspec/bin/gemini_capture.sh --prompt "<checkpoint-1 prompt>" --raw-path "openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-1-gemini-raw.json" --report-path "openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-1-gemini-report.json" --max-attempts 2 --require-json-response`
- Local review input:
  `openspec/changes/introduce-verify-subagent-workflow/verification/schema-contract-subagent-review.json`
- Outputs:
  - `openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-1-gemini-raw.json`
  - `openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-1-gemini-report.json`

## Checkpoint 2

- Logical runner contract: `gemini-capture`
- Resolved platform command:
  `./openspec/bin/gemini_capture.sh --prompt "<checkpoint-2 prompt>" --raw-path "openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-2-gemini-raw.json" --report-path "openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-2-gemini-report.json" --max-attempts 2 --require-json-response`
- Local review input:
  `openspec/changes/introduce-verify-subagent-workflow/verification/entrypoint-subagent-review.json`
- Outputs:
  - `openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-2-gemini-raw.json`
  - `openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-2-gemini-report.json`

## Recovery Exercise (Linux)

- Logical runner contract: `gemini-capture` recovery path
- Resolved platform command:
  `./openspec/bin/gemini_capture.sh --input-raw-path "openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-1-gemini-raw.json" --report-path "openspec/changes/introduce-verify-subagent-workflow/verification/recovery-demo-report.json" --require-json-response`
- Output:
  - `openspec/changes/introduce-verify-subagent-workflow/verification/recovery-demo-report.json`

## Checkpoint 3

- Logical runner contract: `gemini-capture`
- Resolved platform command:
  `./openspec/bin/gemini_capture.sh --prompt "<checkpoint-3 prompt>" --raw-path "openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-3-gemini-raw.json" --report-path "openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-3-gemini-report.json" --max-attempts 2 --require-json-response`
- Local review input:
  `openspec/changes/introduce-verify-subagent-workflow/verification/repair-loop-subagent-review.json`
- Outputs:
  - `openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-3-gemini-raw.json`
  - `openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-3-gemini-report.json`

## Checkpoint 4

- Logical runner contract: `gemini-capture`
- Resolved platform command:
  `./openspec/bin/gemini_capture.sh --prompt "<checkpoint-4 prompt>" --raw-path "openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-4-gemini-raw.json" --report-path "openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-4-gemini-report.json" --max-attempts 2 --require-json-response`
- Local review input:
  `openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-4-subagent-review.json`
- Outputs:
  - `openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-4-gemini-raw.json`
  - `openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-4-gemini-report.json`

## Checkpoint 4 Agent Invocation Test

- Iteration 1 (independent agent):
  - model: `gpt-5.4`
  - reasoning effort: `high`
  - result: `pass_with_warnings`
  - finding: fallback-order wording drift in authoring skills
- Auto-fix applied:
  - explicit fallback chain added to `openspec-propose`, `openspec-ff-change`, `openspec-continue-change`
- Iteration 2 (fresh independent agent):
  - model: `gpt-5.4`
  - reasoning effort: `high`
  - result: `pass`
- Structured evidence:
  - `openspec/changes/introduce-verify-subagent-workflow/verification/checkpoint-4-agent-invocation-evidence.json`
