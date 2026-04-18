# Code-Review-First Architecture

## Scope

This document describes a proposed target architecture for automatic review in
this repository.

It is intentionally different from
`docs/auto-review-architecture.md`, which documents the current mechanism.

This document optimizes for one primary outcome:

```text
find more real code defects earlier
```

The design still preserves independent review and auditable closure, but those
constraints are secondary to review effectiveness rather than the primary
center of gravity.

## Core Position

The current architecture is strong at protocol discipline, but it spends too
much of its effort proving that review happened and not enough effort
maximizing defect discovery in changed code.

The target architecture changes that priority.

```text
Current bias
  task -> protocol -> evidence -> review

Target bias
  change -> risk model -> review -> findings -> repair -> confirmation
```

## Review Model

Automatic review is split into two explicit phases.

### Phase 1: Artifact Review

Review changed `proposal/spec/design/tasks` artifacts as artifacts.

Goal:
- determine whether the intended change is coherent, implementable, and
  sufficiently specified

Primary questions:
- Is the intended behavior clear?
- Are scope and boundaries internally consistent?
- Are major failure modes and interfaces specified?
- Is implementation allowed to proceed?

This phase treats docs as the primary review surface.

### Phase 2: Implementation Review

Review code after artifact review passes.

Goal:
- determine whether the implementation matches approved artifacts
- determine whether the implementation has defects even if the artifacts are
  technically satisfied

Primary questions:
- Does the code match the approved docs?
- Does the code introduce correctness, safety, compatibility, or maintenance
  risks?
- Are high-risk code paths actually sound?

This phase treats docs as constraints and reference material, not as the main
review surface unless they were changed again or are directly implicated by a
finding.

## Architectural Shifts

| Current tendency | Target tendency |
|---|---|
| protocol-first | review-first |
| governed path list defines scope | risk-based scope builder defines scope |
| repo-index acts like a gate | repo-index acts like a cache |
| attempts are the main state object | findings are the main state object |
| deep scan is exceptional | deep scan is default for high-risk code |
| Gemini is tied to fixed workflow gates | Gemini is triggered when review value is high |

## Target Flow

```text
Artifact Review Phase
  -> review changed proposal/spec/design/tasks
  -> approve or block implementation entry

Implementation Review Phase
  -> collect approved docs + code diff + touched tests
  -> build risk-based review scope
  -> optionally load repo-index cache
  -> persistent reviewer session reviews code
  -> findings written into finding ledger
  -> repair loop stays in same session until convergence
  -> zero findings triggers fresh challenger pass
  -> challenger pass closes checkpoint
```

## Review Planner

The current `Stage 0` preflight should stop behaving like a standalone gate
whose main job is to prove the review surface is valid before review begins.

Instead, the system should use a single review planner that compiles the review
surface as part of review orchestration.

The planner produces:
- primary review scope
- impacted interfaces
- dependency spread
- high-risk paths that must deep scan
- optional cache inputs
- evidence requirements for closure

The planner should consume:
- code diff
- approved review artifacts
- touched tests
- dependency and interface hints
- optional repo-index cache
- prior open findings relevant to the touched area

The planner should not freeze a protocol-heavy universe of paths simply because
they are part of workflow governance.

## Scope Builder

Implementation review scope should be built from risk, not mainly from a
predeclared governed file universe.

Primary scope inputs:
- changed implementation files
- changed tests
- public interfaces touched by the diff
- dependencies directly used by changed code
- callers and callees affected by signature or behavior drift
- prior unresolved findings in the same area

Priority model:
- `P0`: changed code and directly impacted code
- `P1`: interfaces, tests, and critical dependency boundaries
- `P2`: supporting workflow or reference artifacts

This keeps the system focused without pretending the model can execute exact
percentage budgets reliably.

## Repo-Index As Cache

`repo-index` remains useful, but its role changes.

It should:
- accelerate reviewer orientation
- summarize ownership and boundaries
- hint at likely deep-scan triggers
- reduce repeated cold-start scanning in large repositories

It should not:
- block code review when missing
- become the dominant review surface
- force the system to solve index freshness before reviewing obviously risky
  code

Cache policy:

```text
if cache exists and looks usable
  -> use it to accelerate review
else
  -> review from source first
  -> refresh cache asynchronously or as follow-up maintenance
```

This keeps review moving while still preserving the long-term value of indexed
repository knowledge.

## Reviewer Model

There are still two reviewer modes, but they play different roles.

### Working Reviewer

A persistent review session that stays active through the repair loop.

Responsibilities:
- inspect the risk-based scope
- accumulate context efficiently
- keep finding continuity across reruns
- confirm whether fixes resolve previously open findings

This avoids paying repeated cold-start costs after every repair cycle.

### Challenger Pass

A fresh independent pass that runs only when the working reviewer has reached
zero findings and the checkpoint is ready to close.

Responsibilities:
- challenge the working reviewer result
- detect blind spots or overfit local assumptions
- confirm closure authority

This preserves independence at the point where it matters most, without forcing
every repair iteration to restart from zero context.

## Findings As The Main State Object

The system should stop treating `attempt` directories as the main working model
for review state.

Attempts remain useful as immutable audit snapshots, but findings should become
the primary operational object.

Each finding should have stable identity and lifecycle fields:
- `fingerprint`
- `status`
- `first_seen_at`
- `last_seen_at`
- `latest_evidence`
- `affected_paths`
- `opened_by`
- `resolved_by`
- `regressed`
- `false_positive`
- `linked_attempts`

This makes it possible to answer the questions that matter during code review:
- Is this a new defect or a repeated one?
- Was this actually fixed?
- Did the defect regress after another change?
- Is the reviewer converging or just producing different snapshots?

### Fingerprint Contract

The fingerprint must be stable across:
- line movement
- local refactors that preserve the same bug shape
- repeated scans over the same unresolved defect

The fingerprint should not depend mainly on raw line numbers.

It should be derived from a normalized finding shape such as:
- finding category or rule family
- normalized primary path
- owning symbol, API, state transition, or logical anchor
- normalized source or sink context when relevant
- normalized boundary type when relevant, such as `permission`, `protocol`,
  `migration`, or `resource-lifecycle`

Example intent:

```text
bad fingerprint
  path + raw line number

better fingerprint
  rule family + normalized path + symbol + boundary shape
```

This contract exists so the system can distinguish:
- a still-open finding
- a resolved finding
- a regressed finding
- a nearby but materially different finding

### Ledger State Model

The ledger should be checkpoint-aware but not attempt-centric.

Suggested finding states:
- `open`
- `accepted_risk`
- `fixed_pending_confirmation`
- `closed_verified`
- `regressed`
- `false_positive`

Suggested transitions:

```text
new finding
  -> open

repair applied
  -> fixed_pending_confirmation

challenger confirms resolution
  -> closed_verified

same bug shape reappears later
  -> regressed

reviewer or challenger rejects the finding
  -> false_positive
```

The ledger should also track evidence freshness:
- latest confirming review pass
- latest contradicting review pass
- whether the last state change came from working reviewer or challenger pass

### Ledger Use In The Review Loop

The working reviewer should consume the ledger before starting a rerun.

That enables the reviewer to:
- re-check previously open findings first
- avoid rediscovering already known defects as new findings
- prioritize risky areas with recurring regressions
- understand whether current review effort is adding new information

The challenger pass should not rewrite history silently.

If it disagrees with the working reviewer, the ledger should record:
- disagreement type
- superseding evidence
- resulting state transition

## Deep-Scan Policy

Deep scan should not be treated only as a fallback when index trust fails.

Instead, deep scan should be mandatory for high-risk code categories and
heuristically triggered elsewhere.

Default deep-scan categories:
- public APIs and externally visible contracts
- state machines
- concurrency and async coordination
- permission or trust boundaries
- serialization and protocol logic
- persistence schema and migrations
- hardware interaction and low-level resource management
- error recovery and rollback paths

Escalation signals outside those categories:
- contradictory evidence
- changed call graph shape
- signature drift
- missing or weak tests around risky behavior
- reopened findings in the same area

This gives the system a clear reason to inspect real code deeply where defects
are most expensive.

## Variant Analysis

A confirmed finding should not remain a single-location event when the same bug
shape may exist elsewhere in the repository.

The system should support repository-local variant analysis:

```text
reviewer confirms one real finding
  -> extract normalized bug pattern
  -> search for structurally similar sites
  -> produce candidate sibling findings
  -> triage candidates into confirmed / rejected / watchlist
```

This is not the same as widening the original review scope blindly.

It is a follow-on operation triggered by a real defect signal.

### When To Run Variant Analysis

Variant analysis is most valuable when:
- the confirmed finding belongs to a recurring bug family
- the affected code uses repeated framework patterns
- the repository contains multiple implementations of the same interface
- the bug shape is likely to repeat by copy-paste or local adaptation

Good candidates include:
- missing error handling
- missing permission checks
- missing boundary validation
- protocol serialization mismatches
- resource cleanup omissions
- state transition guards missing in sibling handlers

### Variant Pattern Inputs

The variant search pattern should be derived from:
- confirmed finding category
- affected API or symbol family
- neighboring control-flow shape
- relevant call targets
- relevant missing guard or missing cleanup action

It should avoid using raw text search as the only mechanism when the bug shape
is structural rather than lexical.

### Variant Result Classes

Variant analysis output should be separated into:
- `confirmed_variants`
- `candidate_variants`
- `rejected_variants`
- `watchlist_variants`

This separation matters because the system should not turn every structural
similarity hit into an authoritative finding automatically.

Suggested meanings:
- `confirmed_variants`: verified defects that should enter the main ledger
- `candidate_variants`: plausible defects that still need reviewer validation
- `rejected_variants`: searched sites that were similar but not defective
- `watchlist_variants`: sites worth revisiting if more evidence appears

### Relationship To The Finding Ledger

Variant analysis should attach to the originating finding rather than becoming a
disconnected side report.

Each originating finding should be able to reference:
- `variant_search_performed`
- `variant_query_summary`
- `variant_scope`
- `variant_results`
- linked confirmed sibling findings

This allows the system to answer:
- Did this finding trigger broader hygiene work?
- Were sibling defects found?
- Was the pattern searched and cleared in nearby code?

### Review Boundaries

Variant analysis should be advisory during ordinary working-reviewer passes and
authoritative only after human or reviewer confirmation.

That keeps the system from exploding one real finding into dozens of low-quality
alerts.

The target behavior is:
- one real finding increases search coverage
- not one real finding creating uncontrolled alert multiplication

## Gemini Policy

Gemini should no longer run only because a checkpoint is `STRICT` and the flow
reached a fixed confirmation stage.

It should run when expected review value is high.

Good triggers:
- high-risk code categories are present
- working reviewer confidence is low
- findings are borderline or ambiguous
- changes span multiple files with nontrivial interaction
- the change alters public interfaces or compatibility behavior
- the challenger pass needs an additional independent opinion

This turns Gemini from a protocol tax into a targeted second-opinion tool.

## Minimal Closure Evidence

The system still needs closure evidence, but the evidence should stay minimal
and directly tied to review authority.

Implementation checkpoint closure should require:
- the approved artifact baseline used for comparison
- the final risk-based review scope
- the final finding ledger state for the checkpoint
- working-reviewer result
- challenger-pass result
- explicit record of deep-scanned paths
- explicit record of any intentionally skipped paths with reason

The system should not require large volumes of auxiliary state files unless
they materially improve review quality or auditability.

## Simplifications From The Current Architecture

The following simplifications are intentional.

- `repo-index` is demoted from authority source to accelerator.
- `required_paths` stops being the main driver of implementation review scope.
- `Stage 0` becomes planner logic rather than a heavyweight standalone gate.
- `attempt` snapshots remain for audit, but `finding ledger` becomes the main
  reviewer working state.
- artifact review and implementation review are explicit separate phases rather
  than one mixed review surface.
- second-opinion invocation becomes value-based rather than purely
  checkpoint-class-based.

## Design Constraints Kept From The Current System

The target architecture still keeps several current strengths.

- implementer and final closer remain separated
- machine-readable findings remain mandatory
- same-session repair convergence remains allowed
- final closure still requires an independent confirmation step
- audit snapshots still exist for traceability

## Practical Migration Direction

If the current system is migrated incrementally, the order should be:

1. Separate artifact review and implementation review explicitly.
2. Introduce a risk-based scope builder for implementation review.
3. Demote `repo-index` from gate to cache.
4. Preserve persistent reviewer sessions and formalize challenger pass closure.
5. Add stable finding fingerprints and finding-ledger state while keeping
   attempt snapshots for compatibility.
6. Add repository-local variant analysis triggered by confirmed findings.
7. Move Gemini to a value-based trigger policy.

This order improves review quality early without requiring a total rewrite
before any value is realized.

## Success Criteria

The target architecture is better only if it produces visibly better review
outcomes.

Success should be judged by:
- more independent findings caught earlier in the repair loop
- fewer reruns caused only by review cold-start loss
- less reviewer time spent on workflow protocol files during implementation
  review
- clearer visibility into finding lifecycle and regression
- lower overhead to review code when cache artifacts are missing or stale
- more sibling defects found from one confirmed seed finding
- fewer duplicate findings caused only by line movement or repeated scans

## Relationship To Current Documentation

- `docs/auto-review-architecture.md`: current-state architecture map
- `docs/code-review-first-architecture.md`: target-state architecture focused
  on better code review outcomes
- `docs/review-flow/archive/code-review-first-2026-04-17/code-review-first/`:
  layered execution plan with clear document boundaries
- `docs/code-review-first-execution-plan.md`: historical single-file reference

Both documents are needed:
- one explains how the current system works
- one explains what the system should become
- one explains how to implement that change in layered form
