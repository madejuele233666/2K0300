# Auto Review Architecture

## Scope

This document describes the current architecture used in this repository to
support task-driven automatic review under `ai-enforced-workflow`.

It reflects the system as it is currently designed across:

- OpenSpec specs
- schema and shared sequences
- skills
- agents
- repository-index artifacts
- checkpoint evidence files

It is not a future-state simplification proposal. It is a map of the current
mechanism.

## Core Intent

The system is trying to achieve this control loop:

```text
Task Done
  -> Build Review Scope
  -> Run Independent Review
  -> Route Findings
  -> Repair / Rerun / Confirm
  -> Persist Evidence
```

The current implementation realizes that loop through a protocol-oriented
workflow rather than a single review service.

## Full Architecture

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│                                  User / Main Agent                         │
│                    drives implementation, apply, verify, repair, rerun      │
└──────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                               OpenSpec CLI / Skills                         │
│                                                                              │
│  Entry skills                                                                │
│  - openspec-artifact-verify                                                  │
│  - openspec-apply-change                                                     │
│  - openspec-verify-change                                                    │
│  - openspec-repair-change                                                    │
│                                                                              │
│  Index skills                                                                │
│  - openspec-index-preflight                                                  │
│  - openspec-index-maintain                                                   │
│                                                                              │
│  Responsibilities                                                             │
│  - resolve change state                                                      │
│  - decide current phase                                                      │
│  - orchestrate preflight / review / repair / rerun                           │
│  - own output paths and evidence flow                                        │
└──────────────────────────────────────────────────────────────────────────────┘
                                      │
                         ┌────────────┴────────────┐
                         │                         │
                         ▼                         ▼
┌──────────────────────────────────┐  ┌───────────────────────────────────────┐
│        Schema / Sequence Layer   │  │         Change Artifact Layer         │
│                                  │  │                                       │
│  Schema                          │  │  Per-change artifacts                 │
│  - ai-enforced-workflow/schema   │  │  - proposal.md                       │
│                                  │  │  - design.md                         │
│  Shared sequences                │  │  - tasks.md                          │
│  - verify-sequence/default       │  │  - specs/<capability>/spec.md        │
│  - index-sequence/default        │  │                                       │
│                                  │  │  Per-checkpoint plan                 │
│  Contracts                       │  │  - governed scopes                   │
│  - shared-findings-v1            │  │  - evidence paths                    │
│  - repo-index-v1                 │  │  - output path convention            │
│  - agent-spawn-decision-v1       │  │                                       │
│  - gemini-capture                │  │                                       │
│                                  │  │                                       │
│  Responsibilities                │  │  Responsibilities                     │
│  - define workflow rules         │  │  - define review target per change   │
│  - define verification protocol  │  │  - define checkpoint boundaries      │
│  - define evidence contract      │  │  - define governed review surfaces   │
└──────────────────────────────────┘  └───────────────────────────────────────┘
                         │                         │
                         └────────────┬────────────┘
                                      ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                             Stage 0: Index Preflight                        │
│                                                                              │
│  Inputs                                                                      │
│  - change / mode / risk_tier                                                 │
│  - governed_scopes                                                           │
│  - evidence_paths_or_diff_scope                                              │
│  - fallback_policy                                                           │
│                                                                              │
│  Work                                                                        │
│  1. discover docs/repo-index/                                                │
│  2. validate manifest / coverage / refresh                                   │
│  3. lint checkpoint contract                                                 │
│  4. derive dirty governed set + transitive closure                           │
│  5. decide reused / refreshed / bypassed                                     │
│  6. freeze required_paths / required_axes                                    │
│                                                                              │
│  Outputs                                                                     │
│  - preflight report                                                          │
│  - index_context                                                             │
└──────────────────────────────────────────────────────────────────────────────┘
                                      │
                    ┌─────────────────┴──────────────────┐
                    │                                    │
          index reusable / trusted              index stale / missing
                    │                                    │
                    ▼                                    ▼
┌──────────────────────────────────┐      ┌───────────────────────────────────┐
│        Reuse Existing Index      │      │      Index Maintenance Path       │
│                                  │      │                                   │
│  consume:                        │      │  requires spawn decision          │
│  - manifest.json                 │      │  -> spawn index-maintainer        │
│  - coverage.json                 │      │  -> refresh repo-index-v1         │
│  - refresh.json                  │      │  -> update manifest/cards/reports │
│  - file/interface cards          │      │  -> return normalized summary     │
└──────────────────────────────────┘      └───────────────────────────────────┘
                    │                                    │
                    └─────────────────┬──────────────────┘
                                      ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                         Frozen Review Surface / index_context                │
│                                                                              │
│  - contract                                                                  │
│  - manifest_path                                                             │
│  - manifest_present                                                          │
│  - preflight_report_path                                                     │
│  - fallback_policy                                                           │
│  - required_paths                                                            │
│  - required_axes                                                             │
│  - deep_scan_candidates                                                      │
└──────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                           Verifier Orchestration Layer                       │
│                                                                              │
│  Before spawn                                                                │
│  - write agent-spawn-decision-v1                                             │
│                                                                              │
│  Invocation bundle                                                           │
│  - change                                                                    │
│  - mode                                                                      │
│  - risk_tier                                                                 │
│  - evidence_paths_or_diff_scope                                              │
│  - findings_contract                                                         │
│  - optional index_context                                                    │
│  - output_paths                                                              │
│                                                                              │
│  Rules                                                                       │
│  - same-session reruns = convergence only                                    │
│  - only fresh confirmation can close checkpoint                              │
│  - no second working verifier while reusable one exists                      │
└──────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                              verify-reviewer Agent                           │
│                                                                              │
│  Role                                                                        │
│  - read-only reviewer                                                        │
│  - does not modify files                                                     │
│  - does not inherit full implementer context                                 │
│                                                                              │
│  Inputs                                                                      │
│  - minimal verification bundle                                               │
│  - index_context                                                             │
│  - output_paths                                                              │
│                                                                              │
│  Review behavior                                                             │
│  - load preflight report first                                               │
│  - review index first                                                        │
│  - deep-scan only on trigger or contradiction                                │
│  - cover all required_paths / required_axes                                  │
│  - emit shared-findings-v1 JSON                                              │
│                                                                              │
│  Outputs                                                                     │
│  - findings.json                                                             │
│  - verifier-evidence.json                                                    │
└──────────────────────────────────────────────────────────────────────────────┘
                                      │
                         ┌────────────┴────────────┐
                         │                         │
                         ▼                         ▼
┌──────────────────────────────────┐  ┌───────────────────────────────────────┐
│        No Blocking Findings      │  │          Blocking / Routed Findings   │
│                                  │  │                                       │
│  if not fresh confirmation:      │  │  route by redirect_layer             │
│  - continue same session         │  │  - proposal                          │
│  - convergence only              │  │  - specs                             │
│                                  │  │  - design                            │
│  if fresh confirmation:          │  │  - tasks                             │
│  - candidate pass                │  │  - implementation                    │
└──────────────────────────────────┘  └───────────────────────────────────────┘
                         │                         │
                         │                         ▼
                         │          ┌──────────────────────────────────────────┐
                         │          │          openspec-repair-change          │
                         │          │                                          │
                         │          │  - normalize findings                    │
                         │          │  - merge Gemini supplemental findings    │
                         │          │  - build repair order                    │
                         │          │  - route upstream or implementation      │
                         │          │  - decide next rerun                     │
                         │          └──────────────────────────────────────────┘
                         │                         │
                         │                         ▼
                         │          ┌──────────────────────────────────────────┐
                         │          │      Apply / Edit / Artifact Update      │
                         │          │                                          │
                         │          │  - main agent edits code or artifacts    │
                         │          │  - only implementation+auto_fixable can  │
                         │          │    stay in auto-fix loop                 │
                         │          └──────────────────────────────────────────┘
                         │                         │
                         └───────────────rerun─────┘
                                      │
                                      ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                          Fresh Confirmation / Gemini Gate                    │
│                                                                              │
│  Conditions                                                                  │
│  - STRICT or dual-gated checkpoint                                           │
│  - only on fresh confirmation pass                                           │
│                                                                              │
│  Runner                                                                      │
│  - openspec/bin/gemini_capture.sh                                            │
│                                                                              │
│  Responsibilities                                                             │
│  - resolve active command                                                    │
│  - write gemini-raw.json                                                     │
│  - normalize to gemini-report.json                                           │
│  - retry once                                                                │
│  - recover from existing raw envelope if needed                              │
└──────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                              Checkpoint Evidence Store                       │
│                                                                              │
│  openspec/changes/<change>/verification/checkpoint-<n>/attempt-<m>/         │
│                                                                              │
│  Files                                                                       │
│  - artifact-index-preflight.json / implementation-index-preflight.json       │
│  - findings.json                                                             │
│  - verifier-evidence.json                                                    │
│  - verifier-spawn-decision.json                                              │
│  - index-maintainer-spawn-decision.json                                      │
│  - gemini-prompt.txt                                                         │
│  - gemini-raw.json                                                           │
│  - gemini-report.json                                                        │
│                                                                              │
│  Summary                                                                     │
│  - latest-attempt.json                                                       │
│                                                                              │
│  Responsibilities                                                            │
│  - state storage                                                             │
│  - audit trail                                                               │
│  - rerun continuity                                                          │
│  - checkpoint authority                                                      │
└──────────────────────────────────────────────────────────────────────────────┘
```

## Repository-Index Subgraph

```text
docs/repo-index/
├── manifest.json
├── reports/
│   ├── coverage.json
│   └── refresh.json
├── files/
│   └── <escaped-path>.yaml
└── interfaces/
    └── <escaped-path>.yaml
```

```text
manifest
  -> governed_scopes
  -> entries
      -> card_path
      -> confidence
      -> last_verified_at

file/interface card
  -> responsibilities
  -> must_not_do
  -> inputs / outputs
  -> depends_on / governed_paths
  -> review_axes
  -> deep_scan_triggers
  -> digest / confidence
```

## Runtime Sequence

```text
User/Main Agent
  -> openspec-apply-change or openspec-artifact-verify
  -> openspec-index-preflight
     -> maybe openspec-index-maintain
     -> emit preflight report + index_context
  -> write spawn decision
  -> spawn verify-reviewer
  -> emit findings + verifier evidence
  -> if needed: repair
     -> edit artifacts/code
     -> rerun same verifier session or fresh confirmation
  -> if STRICT fresh confirmation: run Gemini
  -> write latest-attempt
  -> checkpoint pass or block
```

## Component Responsibilities

### 1. Specs

Specs define the normative workflow contract.

- `verify-subagent-orchestration` defines reviewer isolation, verifier bundle,
  routing contract, and auto-fix boundaries.
- `platform-neutral-verifier-runner` defines the Gemini second-opinion runner
  contract.
- the current change specs add repository-index governance and Stage 0
  preflight.

### 2. Schema

`openspec/schemas/ai-enforced-workflow/schema.yaml` turns those rules into a
generation and execution discipline.

It requires:

- shared sequence usage
- governed preflight for governed review surfaces
- spawn-decision records before new verifier or maintainer sessions
- execution evidence with coverage and saturation fields

### 3. Shared Sequences

The system centralizes orchestration policy in two sequence documents:

- `verify-sequence/default`
- `index-sequence/default`

This is the real workflow protocol layer.

### 4. Skills

Skills are the runtime entrypoints.

They remain intentionally thin and primarily:

- resolve change state
- gather context
- invoke shared sequence logic
- write evidence paths
- hand off to repair or continuation

### 5. Agents

There are two distinct agent roles:

- `verify-reviewer`: final read-only reviewer
- `index-maintainer`: shared-context repository-index maintainer

The maintainer may inherit main-process context, but the reviewer may not.

### 6. Repository Index

The current index is not a global engineering knowledge graph.

It is a governed review support layer used to:

- summarize owned scopes
- support trust / freshness / coverage checks
- reduce repeated cold-start review
- decide when deep scan is needed

### 7. Evidence Store

Checkpoint folders are effectively the current state database.

They store:

- attempt history
- preflight output
- findings
- verifier execution evidence
- spawn-decision records
- Gemini raw and normalized reports
- latest authoritative attempt marker

## Architectural Summary

The current system is best described as:

```text
Specs define the contract
Schema and sequences define the protocol
Skills orchestrate execution
Agents perform review or maintenance
Repository-index provides governed review context
Checkpoint JSON files provide state and authority
```

Or, more compactly:

```text
Schema defines rules
Skills drive flow
Agents execute roles
Repo-index supplies review context
Checkpoint evidence files hold state
```

## What This Architecture Is Optimizing For

- independent review instead of self-review
- auditable checkpoint closure
- bounded rerun semantics
- governed review surfaces with explicit required coverage
- optional reusable repository knowledge without giving the reviewer inherited
  implementation memory

## What It Is Not Yet

- not a single centralized review engine
- not a global knowledge graph
- not a unified database-backed orchestration system
- not a minimal system; it is protocol-heavy and evidence-file-driven
