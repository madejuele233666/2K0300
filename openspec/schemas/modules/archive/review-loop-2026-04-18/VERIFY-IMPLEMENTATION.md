# Verify Implementation

Use this file when an AI is asked to validate whether some implementation is
correct and the caller references this module root.

Caller integration must be routed through:

- `transition-resolver/CALLER-INTEGRATION.md`
- `transition-resolver/contracts/working-session-normalization-v1.json`
- `transition-resolver/bin/normalize_working_session.py`
- `transition-resolver/bin/transition_resolver_resolve.py`

## What The Request Means

Treat the task as a strict implementation-validation workflow.

The target may be:

- a diff
- a file set
- a feature slice
- a rollout
- a repository subsystem
- a non-change repair

Planning artifacts are optional reference inputs only. Do not invent a shared
artifact phase unless the outer workflow explicitly requires one.

## Required Inputs

You need one target identifier plus the shared loop fields.

## Bootstrap Rules For Context-Free AI

If the caller only says "reference this module root and verify whether the
implementation is correct", do not invent ad hoc fields.

Bootstrap in this order:

1. Set defaults from `review-loop-core-v1.json`:
   - `review_goal=implementation_correctness`
   - initial `review_pass_type=working`
   - `findings_contract=shared-findings-v1`
   - `risk_tier=STANDARD` unless the caller explicitly asks for `LIGHT` or
     `STRICT`
2. Set `review_phase` from the declared review surface:
   - `docs_first` for document-primary checkpoints
   - `source_first` for code, test, or impacted-source checkpoints
3. Resolve the subject key:
   - OpenSpec: use `change` when the outer workflow already provides it
   - standalone: if `target_ref` is missing, synthesize one from the concrete
     subject using:
     - `diff:<name>`
     - `paths:<comma-separated-paths>`
     - `workflow:<name>`
     - `repo:<repo-name>-adhoc-review-YYYY-MM-DD`
4. Resolve `evidence_paths_or_diff_scope`:
   - first from explicit user-provided paths, diff, or target scope
   - otherwise from the current implementation diff or changed paths
   - if no concrete implementation scope can be discovered, stop and ask for
     scope before review starts

If bootstrap cannot produce a concrete implementation scope, the workflow must
not start.

Standalone usage:

- `target_ref`
- `review_goal=implementation_correctness`
- `review_phase=docs_first|source_first`
- `review_pass_type=working|challenger`
- `risk_tier`
- `evidence_paths_or_diff_scope`
- `findings_contract`

OpenSpec usage:

- `change`
- `review_goal=implementation_correctness`
- `review_phase=docs_first|source_first`
- `review_pass_type=working|challenger`
- `risk_tier`
- `evidence_paths_or_diff_scope`
- `findings_contract`

Optional but recommended context:

- `target_description`
- `acceptance_reference_paths`
- `constraints_or_invariants`

## Required Outputs Per Pass

Every pass must write:

- machine-readable `findings.json`
- machine-readable `verifier-evidence.json`

`verifier-evidence.json` must also carry the active subject key:

- standalone: `target_ref`
- OpenSpec: `change`

Implementation evidence must also record:

- `review_scope`
- `review_coverage`

## Execution Rules

1. Start with a `working` pass.
2. Write findings and verifier evidence.
3. If implementation issues are fixed, rerun in the same working session.
4. Keep rerunning in the same working session until the latest working pass has:
   - `final_assessment=pass`
   - empty findings
   - `coverage_status=complete`
   - `saturation_status=exhaustive`
5. Before any challenger pass, the main process must validate the previous
   working sub-agent outputs using:
   - `agent_id`
   - `findings_path`
   - `verifier_evidence_path`
6. Challenger must use a fresh session.
7. Only challenger may grant final closure.
8. If challenger returns new findings, that challenger result becomes the new
   active baseline and that challenger session is promoted into the next
   working review baseline.
9. A challenger failure must be recorded as a machine-readable reopen step:
   - use `review-loop-reopen-record-v1.json`
   - the reopen record must reference the failed challenger findings/evidence
  - the next pass returns to `working` by promoting the failed challenger
     session into the next working baseline
  - the caller must preserve that promoted challenger agent as
    `session.active_working_agent_id` on the next working transition
  - do not start a fresh working session after challenger findings unless an
     explicit recovery exception is separately recorded

Working-session normalization rule:

- do not infer fresh-working recovery from `completed` observer state alone
- normalize `session` from explicit reuse checks:
  - `send_input_ready`
  - `resume_probe_attempted`
  - `resume_probe_result`
- ordinary `working_rerun` without `active_working_agent_id` is invalid
- `session_completed_unresumable` is valid only when
  `resume_probe_attempted=true` and `resume_probe_result=unresumable`

Automatic execution rule:

- if the workflow uses reviewer sub-agents, do not stop after deciding that a
  review pass must run
- invoking this module to run the review loop is explicit authorization for the main process to create the reviewer sub-agents required by that loop,
  limited to the reviewer sessions required by the workflow
- verifier reviewer spawns MUST use `fork_context=false` and pass only the
  minimal verification bundle, optional `index_context`, and `output_paths`
- writing `spawn-decision.json` is preparatory only, not a completed pass
- continue in the same turn into the actual reviewer sub-agent invocation
  unless the caller explicitly requested `dry-run` or `manual_pause`
- do not replace reviewer sub-agent invocation with shell/exec when the
  built-in subagent API is available
- the caller must normalize resolver input and execute the returned decision
  instead of improvising transition logic in prompt prose

## Forbidden Actions

- Do not treat your own inspection as if it were prior sub-agent evidence.
- Do not spawn challenger just because the main process thinks the target looks
  good.
- Do not switch to a fresh working session for ordinary reruns.
- Do not treat planning artifacts as a required shared phase of this module.
- Do not close on a working-pass zero-findings result.

## Minimum Run Layout

Recommended layout:

```text
review-runs/<run-name>/
  working/
    attempt-1/
      findings.json
      verifier-evidence.json
      spawn-decision.json
    attempt-2/
      findings.json
      verifier-evidence.json
      reopen-record.json   # when challenger findings promote into the next working baseline
      spawn-decision.json  # only when an exception forced a fresh working session
  challenger/
    attempt-1/
      findings.json
      verifier-evidence.json
      spawn-decision.json
```

Rules:

- `working/attempt-1/spawn-decision.json` records the initial working session.
- Ordinary working reruns within the same working baseline must keep the same
  `agent_id`.
- A later working attempt may change `agent_id` only for an explicit recovery
  reason or because a failed challenger was promoted into the next working
  baseline; explicit recovery attempts must write an exception
  `spawn-decision.json`.
- Every challenger attempt must write `spawn-decision.json`.
- If a challenger fails and the loop returns to working, the next working
  attempt must use the failed challenger `agent_id` as the promoted working
  baseline and write a `reopen-record.json` that references the failed
  challenger output.

## Mechanical Gate

After writing run artifacts, validate them with:

```bash
python3 /home/madejuele/projects/2K0300/openspec/schemas/modules/review-loop/bin/review_loop_guard.py --run-dir <review-run-dir>
```

Do not claim the workflow was followed if this check fails.

This gate is a final-closure gate:

- at least one challenger attempt must exist
- the final attempt must be a challenger pass with
  `closure_authority=challenger_confirmed`

Reference fixtures:

- `REFERENCE-INDEX.md`
- `fixtures/README.md`
- `fixtures/review-runs/standalone-context-free-bootstrap-close`
- `fixtures/review-runs/standalone-challenger-reopen-close`
