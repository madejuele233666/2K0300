## ADDED Requirements

### Requirement: Project-Scoped Verifier Agent
`ai-enforced-workflow` SHALL define a project-scoped verifier agent for workflow-driven verification checkpoints. The verifier agent MUST be configured as read-only and MUST be treated as a reviewer, not as an implementation worker. Manual invocation MAY be exposed as a convenience entry point, but it is not required as a workflow acceptance gate.

#### Scenario: Manual invocation stays optional
- **WHEN** a project chooses to expose the verifier agent for ad hoc manual use
- **THEN** the same verifier contract may be reused
- **AND** the workflow does not require a separate manual-invocation exercise to pass checkpoint acceptance

#### Scenario: Verifier agent is isolated from writes
- **WHEN** the verifier agent is invoked for artifact or implementation review
- **THEN** the agent runs under a read-only sandbox policy
- **AND** any repair or code editing remains the responsibility of the main implementer agent

#### Scenario: Verifier invocation uses shell command entry
- **WHEN** a workflow checkpoint invokes the verifier agent
- **THEN** the invocation MUST be executed through a shell command entry in the active workspace
- **AND** the command organization follows shell template `verify-reviewer-shell-v1`
- **AND** execution evidence records the invoked shell command or command reference
- **AND** orchestrator-only direct invocation is not treated as the required invocation path

### Requirement: Minimal Verification Context
The verifier agent MUST receive a minimal verification bundle instead of the full parent conversation. The bundle SHALL include exactly these fields unless the schema extends them explicitly:
- `change`
- `mode`
- `risk_tier`
- `evidence_paths_or_diff_scope`
- `findings_contract`
- `retry_policy`

The verifier agent MUST NOT require the full implementation conversation transcript in order to produce a valid review, and schema, tasks, and skills MUST reference this same bundle contract instead of redefining near-duplicates.

#### Scenario: Schema-triggered verification avoids parent-context pollution
- **WHEN** a checkpoint in `ai-enforced-workflow` invokes the verifier agent after implementation or artifact changes
- **THEN** the invocation payload includes only the declared verification bundle fields
- **AND** the workflow does not pass the full parent conversation by default

### Requirement: Stateless Verifier Iteration
`ai-enforced-workflow` SHALL execute verifier passes as stateless iterations. Each verification pass MUST spawn a fresh verifier instance and MUST NOT inherit memory from prior verifier iterations in the same repair loop.

#### Scenario: Re-verify after auto-fix uses fresh verifier
- **WHEN** the main implementer applies an eligible automatic implementation fix
- **THEN** the next verification pass invokes a fresh verifier instance
- **AND** the verifier context is rebuilt from the current minimal bundle, not from prior verifier memory

#### Scenario: Project domain does not alter loop contract
- **WHEN** different projects use `ai-enforced-workflow`
- **THEN** the same stateless verifier iteration rules apply
- **AND** project-specific domain details do not relax memory-isolation requirements

### Requirement: Verifier Runtime Profile Source Is Explicit
`ai-enforced-workflow` SHALL define verifier runtime-profile source explicitly.
The default source is `.codex/agents/verify-reviewer.toml`. A checkpoint MAY
document an explicit override, but risk-tier model mapping is optional and not
required by this capability.

#### Scenario: Default runtime profile comes from verifier agent definition
- **WHEN** a verification step does not declare a checkpoint-specific override
- **THEN** verifier runtime configuration comes from `.codex/agents/verify-reviewer.toml`
- **AND** the workflow still enforces the same stateless rerun semantics

#### Scenario: Runtime profile override is auditable
- **WHEN** a checkpoint declares a verifier runtime-profile override
- **THEN** execution evidence records the active verifier model/effort
- **AND** missing or invalid override data blocks the checkpoint

### Requirement: Verifier-First Review With Tier-Gated Gemini Second Opinion
The verifier subagent SHALL be the primary local reviewer for verification checkpoints. Gemini MAY run as a second opinion, but it MUST be mandatory only when the active risk tier or checkpoint policy explicitly requires dual verification.

#### Scenario: STRICT checkpoints require Gemini second opinion
- **WHEN** a change or checkpoint is marked `STRICT`
- **THEN** the shared verification sequence invokes the verifier subagent first
- **AND** Gemini runs afterward as a required second opinion

#### Scenario: Non-STRICT checkpoints can stop at verifier review unless policy opts in
- **WHEN** a change or checkpoint is `LIGHT` or `STANDARD`
- **THEN** the verifier subagent remains the primary review gate
- **AND** Gemini runs only if the schema or checkpoint explicitly enables dual verification

### Requirement: Structured Findings and Routing Contract
The verifier agent SHALL emit machine-consumable findings that identify the review dimension, evidence, severity, redirect layer, blocking state, and whether the issue is eligible for automatic implementation repair. The contract MUST support routing findings either back to implementation or to artifact-layer repair workflows.

The normalized verifier findings contract MUST contain:
- `change`: string
- `mode`: `artifact` or `implementation`
- `final_assessment`: `pass`, `pass_with_warnings`, or `blocked`
- `findings`: array of finding objects

Each finding object MUST contain:
- `id`: unique stable identifier string
- `severity`: `CRITICAL`, `WARNING`, or `SUGGESTION`
- `dimension`: `Completeness`, `Correctness`, or `Coherence`
- `artifact`: `proposal`, `specs`, `design`, `tasks`, or `implementation`
- `problem`: non-empty string
- `evidence`: non-empty string with file-path or diff-based support
- `recommendation`: non-empty string
- `redirect_layer`: `proposal`, `specs`, `design`, `tasks`, or `implementation`
- `blocking`: boolean
- `auto_fixable`: boolean

The routing and repair semantics MUST be:
- `redirect_layer=implementation` is the only layer eligible for automatic implementation repair
- `auto_fixable=true` is valid only when `redirect_layer=implementation`
- all findings with `redirect_layer` other than `implementation` MUST set `auto_fixable=false`
- `blocking=true` means the current phase cannot pass until the finding is resolved, downgraded with evidence, or explicitly rerouted by policy
- `SUGGESTION` findings MUST NOT set `blocking=true`

#### Scenario: Implementation issues are marked for local repair
- **WHEN** the verifier agent detects a blocking issue caused by implementation behavior
- **THEN** the finding includes `redirect_layer: implementation`
- **AND** the report explicitly states whether the issue is auto-fixable by the main implementer agent

#### Scenario: Artifact issues are routed away from implementation auto-fix
- **WHEN** the verifier agent detects a problem caused by proposal, specs, design, or tasks
- **THEN** the finding names the corresponding non-implementation redirect layer
- **AND** the workflow can route the issue to `openspec-repair-change` instead of attempting an implementation-only fix

#### Scenario: Non-implementation layers cannot claim auto-fix eligibility
- **WHEN** a finding is routed to `proposal`, `specs`, `design`, or `tasks`
- **THEN** the finding sets `auto_fixable: false`
- **AND** the workflow does not treat it as an implementation auto-repair candidate

### Requirement: Automatic Repair Loop Boundaries
`ai-enforced-workflow` SHALL allow the main implementer agent to consume verifier findings, apply automatic repairs for implementation-layer issues, and rerun verification. The workflow MUST define stop conditions, including a maximum rerun count and a rule for repeated unresolved findings. Non-implementation findings MUST block automatic implementation repair.

#### Scenario: Repair loop reruns after implementation fixes
- **WHEN** the verifier report contains auto-fixable implementation findings within the configured retry budget
- **THEN** the main implementer agent applies repairs
- **AND** the workflow reruns verifier-agent review before declaring success

#### Scenario: Repair loop stops on repeated or non-implementation blockers
- **WHEN** the verifier report repeats the same blocking finding beyond the configured limit or returns a blocking non-implementation redirect layer
- **THEN** the workflow stops the automatic repair loop
- **AND** the next action is an explicit blocked result or repair-routing handoff

#### Scenario: Auto-fix requires both implementation routing and explicit eligibility
- **WHEN** the verifier report contains an implementation finding with `redirect_layer: implementation`
- **AND** `auto_fixable: true`
- **THEN** the main implementer agent may enter the automatic repair loop
- **AND** implementation findings without `auto_fixable: true` still block or warn without implicit auto-fix
