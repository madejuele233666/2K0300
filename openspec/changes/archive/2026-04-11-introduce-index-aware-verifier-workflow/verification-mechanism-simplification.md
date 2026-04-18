# Verification Mechanism Simplification Handoff

Status: discussion-only, intended as standalone handoff input for a future
implementation conversation

Superseded note:
- this note remains useful only as historical reasoning input
- it is not an implementation authority
- if it conflicts with `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/`, the layered
  `code-review-first` documents win

Related notes:
- `openspec/changes/introduce-index-aware-verifier-workflow/verifier-session-loop.md`

## Intent

This document is not only a design note. It is meant to be good enough for a
new conversation to use as the working source of truth for simplifying the
verification workflow without relying on the prior chat history.

The main problem with the current workflow is not authoritative confirmation
itself. The main problem is that the workflow tries to make every rerun behave
like a fresh authoritative pass. That choice introduces comparison machinery,
retry machinery, and preflight machinery that are larger than the core safety
goal actually requires.

## Executive Summary

The target model is:

- only a `fresh confirmation pass` may end a checkpoint
- same-session reruns are allowed and are not authoritative by themselves
- if a fresh confirmation pass still finds issues, that fresh verifier becomes
  the next active working session
- Gemini runs only on fresh confirmation passes
- preflight exists to define the authoritative confirmation surface, not to
  force fresh-on-every-rerun behavior
- cross-rerun finding identity is not a core gate rule

In short:

```text
same-session reruns handle convergence
fresh confirmation handles authority
preflight defines what authority must review
Gemini confirms authority only when policy requires it
```

## Why The Previous Draft Was Not Enough

The earlier draft captured the direction, but it was not sufficient as a
standalone implementation handoff because it did not pin down:

- exact target invariants
- exact file surfaces that need to change
- which current mechanisms are deleted vs narrowed vs retained
- migration order
- completion criteria
- explicit non-goals

This version adds those missing pieces.

## Scope

### In scope

- simplifying the shared verification workflow
- replacing fresh-on-every-rerun with session convergence plus fresh
  confirmation
- reducing shared-contract complexity
- narrowing preflight to authority-surface compilation and trust/deep-scan
  preparation
- restricting Gemini to confirmation-only use

### Out of scope

- changing the normalized finding envelope away from `shared-findings-v1`
- adding a second verifier agent type
- redesigning redirect-layer routing semantics
- redesigning Gemini runner transport details
- preserving every current rerun-comparison feature

## Current Mechanism Inventory

The current workflow is several layers chained together.

```text
artifact-verify
  -> Stage 0 index preflight
  -> verifier-subagent review
  -> Gemini when required
  -> repair/apply continuation

apply
  -> checkpoint tasks may invoke the same verify flow
  -> implementation verification after all tasks complete

verify-change
  -> Stage 0 index preflight
  -> verifier-subagent review
  -> Gemini when required
  -> auto-fix or repair routing

repair-change
  -> route findings by redirect_layer
  -> rerun artifact or implementation verification
```

### Stage 0 currently does too much

The shared preflight currently acts as all of the following:

- repository-index trust gate
- refresh/bypass/block decision maker
- frozen review-surface compiler
- broad cross-artifact contract linter

### Shared verification currently does too much

The shared verification sequence currently couples together:

- authoritative confirmation
- ordinary reruns
- rerun identity comparison
- retry stopping policy

This is the main source of complexity.

## Core Decision

The future workflow should separate convergence from authority.

```text
same-session rerun
  = convergence

fresh confirmation pass
  = authority
```

This implies:

- `verify-reviewer` remains the only verifier profile
- a working verifier session may rerun after repairs
- same-session zero findings do not end the checkpoint
- same-session zero findings trigger a fresh confirmation pass
- only a newly spawned verifier session with zero findings may end the
  checkpoint
- when fresh confirmation finds issues, that fresh verifier becomes the next
  active working session

## Target Workflow

```text
1. Start fresh verifier session A
2. A finds issues
3. Repair
4. Rerun inside session A until A reports zero findings
5. Spawn fresh confirmation verifier B
6. Recompute authoritative confirmation surface
7. Run verifier review on B
8. Run Gemini on B only if policy requires it
9. If B returns zero findings, stop
10. If B returns findings, B becomes the active working session
11. Repeat
```

## Required Invariants

The simplified system must preserve these invariants.

### 1. Only fresh confirmation may terminate the checkpoint

- no same-session pass may directly complete the gate
- no repair step may directly complete the gate

### 2. Confirmation must review an explicit authoritative surface

- confirmation must have one declared `required_paths`
- confirmation must have one declared `required_axes`
- confirmation evidence must prove coverage against that declared surface

### 3. Confirmation remains machine-auditable

- authoritative findings JSON must exist
- authoritative execution evidence JSON must exist
- execution evidence must point to the emitted findings

### 4. Gemini remains secondary

- Gemini does not replace verifier-subagent review
- Gemini runs only on confirmation passes where policy requires it

### 5. Routing remains explicit

- `redirect_layer` remains authoritative for repair routing
- implementation auto-fix remains restricted to implementation findings that
  are explicitly eligible

## What Must Stay

These mechanisms still make sense after simplification.

### 1. One verifier profile

Keep:

- `.codex/agents/verify-reviewer.toml`
- one verifier role only

Do not add a second acceptance-only reviewer.

### 2. Authoritative review-surface fields

Keep:

- `required_paths`
- `required_axes`

These remain necessary for authoritative confirmation.

### 3. Review-completion evidence

Keep:

- `verifier_output_path`
- `reviewed_paths`
- `skipped_paths`
- `reviewed_axes`
- `unreviewed_axes`
- `coverage_status`
- `saturation_status`
- `early_stop_reason` when relevant
- `skip_reasons` when relevant

### 4. Findings routing semantics

Keep:

- `redirect_layer`
- `blocking`
- `auto_fixable`

### 5. Gemini raw/report recovery

Keep the runner recovery contract when Gemini is mandatory for confirmation.

## What Should Be Deleted

These items should be removed from the shared workflow contract rather than
preserved as first-class semantics.

### 1. Fresh verifier on every rerun

Delete all shared wording equivalent to:

- every verify pass must use a fresh verifier instance
- every rerun must use a fresh verifier instance

Replace with:

- working reruns may remain in the active verifier session
- confirmation reruns must use a fresh verifier session

### 2. Global cross-rerun stable finding-ID semantics

Delete global shared-contract expectations that treat stable finding IDs across
reruns as normal workflow law.

Reason:

- stable IDs solve comparison
- stable IDs do not define authority

### 3. `rerun_context.prior_findings_path` as a shared-core concept

Delete it from the shared contract surface unless a future design explicitly
reintroduces optional comparison support.

It should not remain part of the minimal shared verifier invocation contract.

### 4. Repeated-blocker stop logic in the shared sequence

Delete shared rules equivalent to:

- stop auto-fix because the same blocking finding repeated by `id` and
  `redirect_layer`

If such logic is ever needed, keep it local to orchestration policy rather than
the shared review contract.

### 5. Retry budgets as core semantic workflow concepts

Downgrade:

- `artifact_rerun_budget`
- `implementation_auto_fix_budget`

These may remain runtime safeguards, but they should not remain central
concepts in the shared verification contract.

## What Should Be Narrowed

These parts still matter, but their current role is too broad.

### 1. Preflight

Keep preflight, but narrow it to:

- compiling the authoritative confirmation review surface
- deciding whether repository-index input is trustworthy enough to use
- declaring deep-scan requirements before confirmation

Preflight should stop trying to be:

- a broad artifact-contract linter
- a rerun-comparison engine
- the mechanism that enforces fresh-on-every-rerun

### 2. Repository-index policy

Simplify the mental model to:

- trusted index available -> use it to accelerate and define the review surface
- trusted index unavailable -> confirmation falls back to explicit source review

This means the shared workflow should no longer depend so heavily on a large
`reused|refreshed|bypassed|block` taxonomy.

### 3. Index-maintainer

Narrow the maintainer role to:

- maintenance
- acceleration
- documentation/index upkeep

Do not treat it as the thing that determines whether authoritative review is
possible in principle.

## Target Shared Contract

The target shared verification contract should be reduced to roughly this shape.

### Minimal verifier invocation bundle

- `change`
- `mode`
- `risk_tier`
- `evidence_paths_or_diff_scope`
- `findings_contract`

Optional confirmation-only support data:

- `index_context`
- `output_paths`

Not part of the minimal shared contract:

- generic rerun-comparison input
- global cross-rerun ID continuity requirements
- shared repeated-blocker stop semantics

### Confirmation evidence expectations

Authoritative confirmation requires:

- findings JSON
- execution evidence JSON
- review-completion fields proving coverage
- preflight evidence only insofar as it defines the confirmation surface for
  governed review
- Gemini outputs only when policy requires them

## File Touch Map

A future implementation conversation should expect to modify at least these
surfaces.

### Shared contracts and schema

- `openspec/schemas/ai-enforced-workflow/verification-sequence.md`
- `openspec/schemas/ai-enforced-workflow/index-sequence.md`
- `openspec/schemas/ai-enforced-workflow/schema.yaml`
- `openspec/schemas/ai-enforced-workflow/templates/design.md`
- `openspec/schemas/ai-enforced-workflow/templates/tasks.md`

### Verifier agent surface

- `.codex/agents/verify-reviewer.toml`

### Skills

- `$CODEX_HOME/skills/openspec-artifact-verify/SKILL.md`
- `$CODEX_HOME/skills/openspec-verify-change/SKILL.md`
- `$CODEX_HOME/skills/openspec-repair-change/SKILL.md`
- `$CODEX_HOME/skills/openspec-apply-change/SKILL.md`
- `$CODEX_HOME/skills/openspec-index-preflight/SKILL.md`
- optionally `$CODEX_HOME/skills/openspec-index-maintain/SKILL.md`

### Change-local design/task/spec references

If this repository still uses the change-local artifacts as a working design
source, they should also be updated for consistency:

- `openspec/changes/introduce-index-aware-verifier-workflow/design.md`
- `openspec/changes/introduce-index-aware-verifier-workflow/tasks.md`
- any change-local specs that restate the old rerun model

## Migration Order

The safest order is:

1. Rewrite shared verification semantics
   - remove fresh-on-every-rerun wording
   - define confirmation-only authority
2. Rewrite preflight semantics
   - narrow Stage 0 to surface compilation and trust/deep-scan preparation
3. Rewrite verifier agent instructions
   - distinguish working reruns from confirmation semantics
4. Rewrite schema/templates
   - remove old rerun law from generated workflow text
5. Rewrite skills
   - ensure orchestration matches the new contract
6. Update change-local docs/specs if they are still used as local source of
   truth
7. Regenerate or rerun verification evidence only after the contract is
   coherent again

## Completion Criteria

The simplification is not done until all of the following are true.

### Contract coherence

- no shared sequence still says every rerun must be fresh
- no schema/template still teaches fresh-on-every-rerun
- no skill still passes or expects generic rerun comparison as part of the
  minimal verifier contract

### Confirmation semantics

- the workflow has one and only one stop rule:
  fresh confirmation pass with zero findings
- same-session zero findings do not terminate the checkpoint by themselves
- fresh confirmation findings reopen convergence by making that verifier the
  next active working session

### Gemini policy

- Gemini is invoked only on confirmation passes when policy requires it

### Preflight narrowing

- preflight is no longer framed as a large rerun-governance machine
- preflight is framed as authoritative surface compilation plus trust/deep-scan
  preparation

### Comparison simplification

- stable finding IDs across reruns are no longer part of the shared gate law
- repeated-blocker detection is no longer a shared gate rule
- retry budgets are no longer presented as core workflow semantics

## Validation Checklist For A Future Implementation Conversation

A future implementation conversation should explicitly verify these points
before claiming success.

- Search for `fresh verifier instance`, `no inherited verifier memory`, and
  equivalent wording across schema, sequences, templates, and skills.
- Search for `prior_findings_path`, repeated-finding detection, and stable-ID
  wording across the same surfaces.
- Search for Gemini wording to ensure it now attaches only to confirmation
  passes.
- Search for preflight wording to ensure Stage 0 is no longer described as a
  broad contract-lint gate beyond what the simplified model still needs.
- Check that the verifier agent instructions no longer imply that every rerun
  is authoritative.
- Check that generated guidance still preserves `required_paths`,
  `required_axes`, and review-completion evidence.

## Suggested Starting Brief For A New Conversation

The next implementation conversation can start from this brief:

```text
Use `openspec/changes/introduce-index-aware-verifier-workflow/verification-mechanism-simplification.md`
as the source of truth.

Goal: replace the fresh-on-every-rerun verification model with:
- same-session reruns for convergence
- fresh confirmation pass as the only authoritative stop condition
- Gemini only on fresh confirmation
- preflight narrowed to confirmation-surface compilation and trust/deep-scan preparation

Do not preserve global cross-rerun stable-ID semantics, generic `prior_findings_path`,
or repeated-blocker stop rules unless they are reintroduced as explicitly optional
non-core features.
```

## Final Position

Yes, after the updates in this file, it should be sufficient to support a new
conversation executing the discussed direction without needing the original
chat history.

It is still a design handoff rather than executable code, but it now includes:

- the target model
- explicit keep/remove/narrow decisions
- the expected file surfaces
- migration order
- completion criteria
- a starter brief for the next conversation
