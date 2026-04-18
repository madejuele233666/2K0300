# Review Loop Module

## Purpose

This module defines one shared process for validating whether an implementation
is correct.

Use this module when the caller points at some implementation target and says
things like:

- "reference this module root and verify whether the implementation is correct"
- "use this workflow to review a diff, code path, rollout, or system slice"
- "run a strict working/challenger verification loop"

This module is intentionally implementation-first.

Planning artifacts may be used as optional reference material, but they are not
the main model of this module and they do not create a separate shared phase.

## If You Are The Verifying AI

Open [VERIFY-IMPLEMENTATION.md](/home/madejuele/projects/2K0300/openspec/schemas/modules/review-loop/VERIFY-IMPLEMENTATION.md)
and follow it exactly.

The required loop is:

```text
working review
  -> write findings + verifier evidence
  -> repair and rerun in the same working session
  -> converge on zero findings with complete coverage
  -> validate the previous working sub-agent outputs
  -> challenger review in a fresh session
  -> if challenger finds issues, promote that challenger session to the next working baseline
  -> close or reopen
```

Caller orchestration is mechanical:

- normalize `transition-resolver-input-v1`
- resolve through
  [CALLER-INTEGRATION.md](/home/madejuele/projects/2K0300/openspec/schemas/modules/review-loop/transition-resolver/CALLER-INTEGRATION.md)
- execute the returned decision exactly
- do not improvise session transitions from prose

If the workflow uses sub-agents, this module also requires:

- automatic continuation into review once review is required
- invoking this module to run the review loop is explicit authorization for the main process to create the reviewer sub-agents required by that loop,
  limited to the reviewer sessions required by the workflow
- verifier reviewer spawns MUST use `fork_context=false` and pass only the
  minimal verification bundle, optional `index_context`, and `output_paths`
- `spawn-decision.json` is preparatory only
- actual reviewer invocation in the same turn unless the caller explicitly
  requested `dry-run` or `manual_pause`
- no shell/exec substitution when the built-in subagent API is available

If the caller does not supply a prebuilt bundle, bootstrap from
`review-loop-core-v1.json -> bootstrap_defaults / bootstrap_rules`.

## Process Boundary

The reviewer sub-agent emits findings and execution evidence.

The main process decides rerun, challenger entry, or closure, and it must not
substitute its own judgment for the prior working sub-agent output.

## Contracts

Core contract:

- `contracts/review-loop-core-v1.json`
- `contracts/review-loop-reopen-record-v1.json`

Adapters:

- `contracts/review-loop-openspec-adapter-v1.json`
- `contracts/review-loop-standalone-adapter-v1.json`

The core contract defines the shared implementation-validation loop.

Adapters bind that loop to:

- OpenSpec `change`-based orchestration
- standalone `target_ref`-based orchestration

Schema-local wrappers may still define artifact gates or other outer workflow
steps, but those wrappers are not the source of truth for the working /
challenger review loop.

Resolver integration:

- `transition-resolver/CALLER-INTEGRATION.md`
- `transition-resolver/contracts/working-session-normalization-v1.json`
- `transition-resolver/bin/normalize_working_session.py`
- `transition-resolver/contracts/transition-resolver-input-v1.json`
- `transition-resolver/contracts/transition-resolver-decision-v1.json`
- `transition-resolver/bin/transition_resolver_resolve.py`

Reference index:

- `REFERENCE-INDEX.md`

## Reference Fixtures

Fixture index:

- `fixtures/README.md`

Checked standalone run-dir examples live under:

- `fixtures/review-runs/standalone-context-free-bootstrap-close`
- `fixtures/review-runs/standalone-challenger-reopen-close`

Use them as concrete examples for:

- context-free standalone bootstrap into working review
- same-session working reruns
- challenger failure followed by machine-readable `challenger_reopen` record
  and promotion of the challenger session into the next working baseline
- final challenger closure
