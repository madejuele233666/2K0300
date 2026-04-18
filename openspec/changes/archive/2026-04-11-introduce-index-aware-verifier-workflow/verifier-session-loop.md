# Verifier Session Loop Proposal

Status: discussion-only, not implemented

Superseded note:
- this note is preserved because the session-loop idea is still relevant
- however, it is no longer a standalone implementation source of truth
- use `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/` as the authoritative rollout plan

## Purpose

Define a simpler verification loop that improves finding saturation and review
efficiency without introducing a second verifier role.

The proposed model replaces the current "repair -> fresh verifier -> repair ->
fresh verifier" rhythm with a session-oriented loop:

- a verifier agent may rerun repeatedly inside its own session after repairs
- once that session reaches zero findings, the main process spawns a new fresh
  verifier agent
- if the new fresh verifier agent also returns zero findings, the checkpoint
  stops
- otherwise, that fresh verifier agent becomes the current working session and
  the loop continues automatically

## Core Decision

This model does **not** define two verifier agent types.

- `verify-reviewer` remains the only verifier agent profile
- "fresh" means a newly spawned verifier session with no inherited verifier
  conversation
- "same-session rerun" means reusing the current verifier session after repair

The distinction is operational, not role-based.

## Stop Condition

There is exactly one workflow stop condition:

- after spawning a new fresh verifier agent, that agent returns zero findings

No other intermediate state needs a separate pass/fail decision.

In particular:

- same-session zero findings does **not** stop the checkpoint by itself
- same-session zero findings only triggers the next fresh verifier spawn
- any fresh verifier that still finds issues becomes the active session for the
  next repair/rerun cycle automatically

## State Machine

```text
start
  -> spawn fresh verifier A
  -> A returns findings
  -> repair
  -> rerun inside A
  -> repair
  -> rerun inside A
  -> ...
  -> A returns zero findings
  -> spawn fresh verifier B
  -> if B returns zero findings: stop
  -> if B returns findings:
       repair
       rerun inside B
       ...
       B returns zero findings
       spawn fresh verifier C
       ...
```

## Why This Is Better Than The Current Fresh-Only Loop

### Expected gains

- the verifier keeps local review context while fixing the currently known
  problem set
- reruns no longer need to rebuild the entire mental model after each repair
- finding continuity inside one session is much easier to preserve
- the current session is more likely to uncover secondary findings that were
  masked by earlier blockers
- only one independent freshness check is required at a time: the next spawned
  verifier

### Preserved independence

- final stopping still depends on a newly spawned fresh verifier agent
- the workflow still checks whether the previous session missed anything
- independence is retained without forcing every rerun to rebuild from zero

## Difference From The Current Model

### Current model

```text
repair -> fresh verifier -> repair -> fresh verifier -> repair -> fresh verifier
```

### Proposed model

```text
fresh verifier session
  -> repair + same-session reruns until zero findings
  -> new fresh verifier
  -> if findings exist, repair + same-session reruns inside that new verifier
  -> repeat
```

## Contract Rules

### 1. Single verifier profile

- use only `verify-reviewer`
- do not create a second "acceptance reviewer" agent profile
- freshness is defined by spawning a new verifier session, not by agent type

### 2. Same-session rerun rule

- after repair, rerun verification inside the same active verifier session
- that verifier may reuse its own prior conversation and prior findings
- the main process does not need a separate stop check during this phase
- same-session reruns continue automatically until the active session reports
  zero findings

### 3. Fresh-session rule

- when the active session first reports zero findings, spawn a new fresh
  verifier session
- the fresh verifier session must not inherit the previous verifier
  conversation
- it may still receive explicit machine-readable evidence paths from prior runs

### 4. Automatic continuation

- if a fresh verifier session returns findings, it automatically becomes the
  new active session
- the workflow repairs those findings and continues rerunning inside that same
  session
- no manual branch selection is needed

## Evidence Model

This proposal needs explicit evidence that distinguishes same-session reruns
from fresh respawns, even though both use the same verifier profile.

Recommended fields:

- `verifier_session_id`
- `fresh_spawn_index`
  - monotonic counter for fresh verifier sessions within the checkpoint
- `session_rerun_index`
  - monotonic counter within one verifier session
- `spawn_reason`
  - `initial`
  - `post-zero-findings-confirmation`
- `prior_findings_path`
  - optional explicit comparison input
- `prior_verifier_evidence_path`
  - optional explicit link to the previous authoritative pass
- `session_parent_findings_path`
  - optional path to the finding set that opened the current session
- `final_assessment`
  - existing verifier result
- `coverage_status`
  - existing execution evidence field
- `saturation_status`
  - existing execution evidence field

These fields are intended for execution evidence, not for the normalized
findings envelope.

## Finding ID Rules

### Same-session reruns

- findings that are semantically unchanged should keep the same `id`
- closed findings disappear normally
- newly discovered findings receive new `id`s

### Across fresh sessions

- cross-session stability is preferred only when explicit comparison input is
  supplied
- if `prior_findings_path` is not supplied, cross-session `id` churn is not by
  itself a contract failure
- the workflow should not require hidden inherited memory across fresh sessions

## What This Proposal Explicitly Does Not Add

- no second verifier agent type
- no extra intermediate stop condition
- no business-level `session_rerun_limit`

This proposal treats repeated same-session reruns as normal operation rather
than as an exceptional loop that needs a contract-level cap.

## Main Risk

The main risk is that a same-session verifier may become anchored to its
existing finding set and overfit to local repairs.

This proposal accepts that tradeoff because:

- same-session reuse is the source of most efficiency gains
- a newly spawned fresh verifier is still required before termination
- the stop condition therefore still depends on an independent re-check

## Adoption Note

If adopted, this model should replace the current "always fresh verifier on
every rerun" rule in:

- verifier invocation contracts
- repair-loop contracts
- checkpoint evidence contracts
- skill orchestration guidance
- task/design/schema language that currently mandates a fresh verifier for every
  rerun
