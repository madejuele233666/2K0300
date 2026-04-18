# verify-subagent-orchestration Specification

## Purpose
Define the canonical verifier-subagent orchestration contract for
`ai-enforced-workflow`: built-in verifier invocation, a unified
agent-state-based verification cycle across review surfaces, valid-pass
termination, normalized findings, and bounded automatic repair loops.

## Requirements
### Requirement: Project-Scoped Verifier Agent
`ai-enforced-workflow` SHALL define a project-scoped verifier agent for
workflow-driven verification checkpoints. The verifier agent MUST be configured
as read-only and MUST be treated as a reviewer, not as an implementation
worker. Manual invocation MAY be exposed as a convenience entry point, but it
is not required as a workflow acceptance gate.

#### Scenario: Manual invocation stays optional
- **WHEN** a project chooses to expose the verifier agent for ad hoc manual use
- **THEN** the same verifier contract may be reused
- **AND** the workflow does not require a separate manual-invocation exercise
  to pass checkpoint acceptance

#### Scenario: Verifier agent is isolated from writes
- **WHEN** the verifier agent is invoked for any review checkpoint
- **THEN** the agent runs under a read-only sandbox policy
- **AND** any repair or code editing remains the responsibility of the main
  implementer agent

#### Scenario: Verifier invocation uses built-in subagent API
- **WHEN** a workflow checkpoint invokes the verifier agent
- **THEN** the invocation MUST use a built-in subagent API path in the main
  process
- **AND** the invocation follows template `verify-reviewer-inline-v3`
- **AND** execution evidence records invocation metadata (`agent_id`,
  `start_at`, `end_at`, `final_state`)
- **AND** shell-based codex invocation is fallback-only, not the required path

### Requirement: Minimal Verification Context
The verifier agent MUST receive a minimal verification bundle instead of the
full parent conversation. The bundle SHALL include exactly these fields unless
the schema extends them explicitly:

- `change` or `target_ref`
- `review_goal`
- `review_phase`
- `risk_tier`
- `evidence_paths_or_diff_scope`
- `findings_contract`

The shared field groups for Stage A SHALL be defined in shared JSON contract
files rather than restated across prompts:

- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`

#### Scenario: Schema-triggered verification avoids parent-context pollution
- **WHEN** a checkpoint in `ai-enforced-workflow` invokes the verifier agent
  after documentation or code changes
- **THEN** the invocation payload includes only the declared verification bundle
  fields
- **AND** the workflow does not pass the full parent conversation by default

### Requirement: Agent-State Verification Cycle
`ai-enforced-workflow` SHALL use the shared verification-cycle state model:

- `active`
- `non_active`
- `closed`

The caller MUST maintain `agent-table.json` and MUST use the shared
verification-cycle orchestrator to decide resume, spawn, repair, state
transitions, and termination.

#### Scenario: Active verifier is resumed first
- **WHEN** a usable `active` verifier exists
- **THEN** the caller continues that agent before considering a fresh spawn
- **AND** the caller uses `send_input` while that `active` agent is still open
- **AND** the caller uses `resume` only when that same `active` agent was
  closed and must be restored

#### Scenario: Send-input lookup failure still resumes the same active verifier
- **WHEN** the caller targets the current `active` verifier with `send_input`
  and the subagent layer returns `agent not found`
- **THEN** the caller MUST try `resume` for that same `active` verifier next
- **AND** the caller MUST NOT reinterpret that lookup failure as
  `no_usable_active_agent`

#### Scenario: Fresh verifier is spawned only when no usable active agent exists
- **WHEN** no usable `active` verifier exists
- **THEN** the caller may spawn a new verifier and record it as `active`
- **AND** the caller records the spawn reason in `agent-spawn-decision-v1`
- **AND** `no_usable_active_agent` may describe initial bootstrap or the next
  normal spawn after a prior agent became `non_active` or merely `closed`
- **AND** `no_usable_active_agent` means only that `agent-table.json`
  literally contains no `active` verifier entry
- **AND** recovery spawn cases such as `active_agent_missing` or
  `active_agent_not_resumable` remain distinct and MUST NOT be rewritten as
  `no_usable_active_agent`

#### Scenario: Blocking repair stays with the same active agent
- **WHEN** the `active` verifier returns `verdict=block`
- **THEN** the main implementer repairs the issue locally
- **AND** the next verification pass reuses that same agent while it remains
  usable
- **AND** the workflow does not invent a parallel legacy dual-session split

#### Scenario: Only block to pass marks non-active
- **WHEN** an `active` verifier transitions from `block` to a valid `pass`
- **THEN** that same agent may be marked `non_active`
- **AND** `close`, `exit`, timeout, or observer-only completion does not imply
  `non_active`

#### Scenario: Termination depends only on a valid active pass
- **WHEN** the current `active` verifier returns `verdict=pass`
- **THEN** termination is allowed only if
  `review_coverage.coverage_status=complete` and
  `review_coverage.exhaustive=true`
- **AND** a `closed` or non-exhaustive pass cannot substitute for termination

#### Scenario: Partial verification must declare scope
- **WHEN** a verifier attempt records `review_coverage.coverage_status=partial`
- **THEN** `review_scope.scope` MUST be explicit and non-empty
- **AND** the caller must continue the cycle rather than treating that pass as
  terminal

### Requirement: Spawn Decision Schema Uses Agent-State Semantics
`ai-enforced-workflow` SHALL use `agent-spawn-decision-v1` only as a spawn
record, not as termination authority. The schema MUST use agent-state semantics
instead of legacy dual-session roles.

#### Scenario: Spawn decision records active-agent absence or unusability
- **WHEN** the caller records a verifier spawn decision
- **THEN** `reason_code` is limited to active-agent availability or recovery
  reasons such as `no_usable_active_agent`, `active_agent_missing`, or
  `active_agent_not_resumable`
- **AND** `no_usable_active_agent` remains valid when the previous recorded
  agent is `none`, `non_active`, or `closed`
- **AND** the record includes prior-agent state plus `agent_table_path`

### Requirement: Verifier Runtime Profile Source Is Fixed
`ai-enforced-workflow` SHALL use verifier runtime configuration from
`.codex/agents/verify-reviewer.toml` for workflow checkpoints.

#### Scenario: Default runtime profile comes from verifier agent definition
- **WHEN** a verification step runs under `ai-enforced-workflow`
- **THEN** verifier runtime configuration comes from
  `.codex/agents/verify-reviewer.toml`
- **AND** the workflow still enforces the same verification-cycle semantics

### Requirement: Verifier-First Review With Optional Gemini Second Opinion
The verifier subagent SHALL be the primary local reviewer for verification
checkpoints. Gemini MAY run as a second opinion, but Stage A MUST NOT make
Gemini a mandatory workflow gate by default.

#### Scenario: Repository policy opts into Gemini dual review
- **WHEN** a repository or checkpoint explicitly enables dual verification
- **THEN** the shared verification sequence invokes the verifier subagent first
- **AND** Gemini may run afterward as a secondary check

#### Scenario: Default Stage A checkpoints rely on verifier review
- **WHEN** a change or checkpoint follows the default Stage A workflow
- **THEN** the verifier subagent remains the primary review gate
- **AND** Gemini runs only if the schema or checkpoint explicitly enables dual
  verification

### Requirement: Structured Findings Contract
The verifier agent SHALL emit machine-consumable findings that identify the
review dimension, evidence, severity, blocking state, and whether the issue is
eligible for automatic repair within the active verification cycle.

The normalized verifier findings contract MUST contain:

- `change` or `target_ref`
- `verdict`: `block` or `pass`
- `findings`: array of finding objects

Each finding object MUST contain:

- `id`: unique stable identifier string
- `severity`: `CRITICAL`, `WARNING`, or `SUGGESTION`
- `dimension`: `Completeness`, `Correctness`, or `Coherence`
- `problem`: non-empty string
- `evidence`: non-empty string with file-path or diff-based support
- `recommendation`: non-empty string
- `blocking`: boolean
- `auto_fixable`: boolean

The routing and repair semantics MUST be:

- `auto_fixable=true` is valid only when `blocking=true`
- `blocking=true` means the current phase cannot pass until the finding is
  resolved, downgraded with evidence, or explicitly rerouted by policy
- `SUGGESTION` findings MUST NOT set `blocking=true`

#### Scenario: Auto-fix eligibility is explicit
- **WHEN** the verifier agent detects a blocking issue that the active repair
  loop may safely fix automatically
- **THEN** the finding sets `auto_fixable: true`
- **AND** the workflow may continue with the automatic repair loop

#### Scenario: Manual repair remains explicit
- **WHEN** a blocking finding is not safe for automatic repair
- **THEN** the finding sets `auto_fixable: false`
- **AND** the workflow routes to explicit manual repair or caller-local policy

### Requirement: Automatic Repair Loop Boundaries
`ai-enforced-workflow` SHALL allow the main implementer agent to consume
verifier findings, apply automatic repairs for eligible issues, and rerun
verification. Blocking findings with `auto_fixable=false` MUST stop the
automatic repair loop.

#### Scenario: Repair loop reruns after implementation fixes
- **WHEN** the verifier report contains auto-fixable implementation findings
  within the configured retry budget
- **THEN** the main implementer agent applies repairs
- **AND** the workflow reruns verification inside the same usable `active`
  verifier session before declaring convergence

#### Scenario: Repair loop stops on non-auto-fixable blockers
- **WHEN** the verifier report returns a blocking finding with
  `auto_fixable: false`
- **THEN** the workflow stops the automatic repair loop
- **AND** the next action is an explicit blocked result or repair-routing
  handoff

### Requirement: Implementation Evidence Includes Inline Scope And Coverage
Stage A verification evidence SHALL keep the main path additive and inline. The
workflow MUST NOT require a standalone planner artifact, planner schema, or
planner agent to begin or finish review.

Implementation review evidence MUST include:

- `review_scope`
- `review_coverage`

`review_scope` MUST summarize changed code, changed tests, impacted interfaces,
mandatory deep-scan paths, optional cache inputs, and explicit `scope` when the
review is partial.

`review_coverage` MUST summarize `coverage_status` and `exhaustive`.

Path- and axis-level execution evidence MUST be carried as top-level verifier
evidence fields:

- `reviewed_paths`
- `skipped_paths`
- `reviewed_axes`
- `unreviewed_axes`

Persisted `verifier-evidence.json` MUST also carry the same subject key as the
matching findings payload (`change` or `target_ref`) plus constrained
execution metadata:

- `template_id=verify-reviewer-inline-v3`
- `final_state` is one of `completed|completed_pass`
- `start_at` and `end_at` are valid date-time strings
- `end_at >= start_at`

#### Scenario: Partial coverage blocks termination without creating a new gate
- **WHEN** a review pass leaves mandatory deep-scan paths uncovered or
  otherwise records `coverage_status=partial`
- **THEN** the workflow does not declare the active pass terminal
- **AND** it does not create a separate planner checkpoint to express that
  state

#### Scenario: Scope summary stays inline
- **WHEN** the verifier prepares implementation evidence
- **THEN** `review_scope` and `review_coverage` are embedded in
  `verifier-evidence.json`
- **AND** the workflow does not require `review-plan.json` or an equivalent
  standalone planner artifact
