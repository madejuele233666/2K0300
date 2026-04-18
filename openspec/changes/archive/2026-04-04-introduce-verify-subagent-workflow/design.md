## Context

`ai-enforced-workflow` currently spreads verification behavior across `schema.yaml`, schema templates, `openspec-artifact-verify`, `openspec-verify-change`, and platform-specific Gemini helper assumptions. The policy requires a Codex-first review and a Gemini second opinion, but the execution model is duplicated and still assumes a Windows-first helper path in the schema templates while this repository already uses a Linux shell wrapper in practice.

The workflow also conflates two separate roles:

- the implementer agent that writes or edits files
- the reviewer that judges artifacts or implementation quality

That coupling creates reviewer bias and context pollution because the same agent context that made the implementation also performs the local review. The design goal is to preserve explicit workflow gates while splitting execution into separate roles and a reusable shared orchestration path.

Current vs target:

```text
Current
implementer Codex
  -> local self-review
  -> Gemini CLI
  -> optional repair routing

Target
implementer Codex
  -> explicit read-only verifier subagent
  -> Gemini CLI
  -> structured repair loop / repair routing
```

## Goals / Non-Goals

**Goals:**
- Introduce a project-scoped verifier agent that is reusable from schema-driven checkpoints and may also be manually invoked when convenient.
- Make the verifier agent read-only and isolate it from the full parent conversation by passing a minimal verification bundle.
- Collapse duplicated artifact and implementation verification flow into one shared orchestration model.
- Enforce stateless verify/fix iterations so each rerun uses a fresh verifier instance.
- Define deterministic risk-tier verifier model/reasoning profiles with fallback ordering.
- Replace hard-coded platform-specific verifier commands in workflow policy with a stable runner contract and platform adapters.
- Define bounded automatic repair behavior for implementation-layer findings.

**Non-Goals:**
- Replacing Gemini as the independent verifier.
- Making Gemini mandatory for every verification checkpoint regardless of risk tier.
- Making the verifier subagent perform writes or auto-repair directly.
- Requiring manual verifier invocation as a mandatory exercise artifact.
- Generalizing a full multi-agent framework beyond verification.
- Changing non-verify workflow stages that do not depend on verification gates.

## Decisions

### Decision: Introduce a project-scoped read-only verifier agent

Problem being solved:
Current local review is described as a Codex-first stage, but in practice it is still the same implementer context reviewing its own work. That weakens independence and repeats the same review instructions in multiple places.

Stack equivalent:
- Project-scoped Codex agent definition under `.codex/agents/`
- Shared invocation contract consumed by verify-related skills and schema checkpoints

Alternatives considered:
- Keep implementer self-review and only refactor the Gemini prompt generation.
- Replace both local review and Gemini with one verifier agent.
- Create separate artifact and implementation verifier agents with duplicated logic.

Why this option was chosen:
A single project-scoped verifier agent gives the workflow a reusable review role without multiplying entry points. Making it read-only preserves the separation of duties: review produces findings; repair stays with the implementer agent. Manual invocation remains a convenience, not a separately enforced acceptance target.

Named deliverables:
- `.codex/agents/verify-reviewer.toml`
- Shared verification invocation contract in `openspec/schemas/ai-enforced-workflow/schema.yaml`
- Thin entry-point updates in `openspec-artifact-verify` and `openspec-verify-change`

Failure semantics:
- If the verifier agent cannot be spawned, the verification step is blocked before any configured second-opinion step runs.
- If the verifier agent returns malformed findings, the orchestration layer blocks and reports contract failure.
- If the verifier agent reports no findings, the workflow may still proceed to Gemini when risk tier or checkpoint policy requires a second opinion.

Boundary examples:
- Caller: `openspec-verify-change`
- Payload: change name, mode `implementation`, risk tier, evidence paths, findings contract, retry budget
- Forbidden leak: full parent conversation transcript or implementer chain-of-thought
- Return form: normalized local review JSON that already satisfies the standard findings object

Contrast structure:
- Allow: explicit verifier invocation with minimal verification bundle
- Forbid: implicit reuse of the full implementer conversation as verifier context

Verification hook:
Schema and task templates can be reviewed for explicit verifier-agent invocation requirements, read-only configuration, and minimal-context bundle fields.

### Decision: Enforce project-agnostic stateless verifier loop with explicit runtime profile source

Problem being solved:
If verifier reruns can reuse prior verifier memory, review quality becomes non-deterministic across projects and repair loops.

Stack equivalent:
- Shared sequence contract in `openspec/schemas/ai-enforced-workflow/verification-sequence.md`
- Runtime profile contract in `.codex/agents/verify-reviewer.toml`
- Apply/verify/repair skill entrypoints consuming the shared runtime-profile source

Alternatives considered:
- Keep runtime profile completely implicit with no documented source.
- Keep one long-lived verifier instance through repair loops.
- Require a documented runtime profile source and fresh verifier instances for every loop iteration.

Why this option was chosen:
Fresh verifier instances prevent cross-iteration contamination after auto-fix edits. A shared runtime-profile source avoids contract drift while allowing projects to override when needed. Applying these rules at shared-sequence level keeps behavior consistent for any project using `ai-enforced-workflow`.

Named deliverables:
- Runtime profile source:
  - default: `.codex/agents/verify-reviewer.toml`
  - optional per-checkpoint override documented in design/tasks when used
- Stateless loop rule:
  - verify -> findings -> eligible auto-fix -> fresh verifier rerun

Failure semantics:
- If runtime profile override is invalid, verification is blocked.
- If rerun reuses prior verifier memory, the iteration is invalid and must be rerun with a fresh verifier instance.

Boundary examples:
- Caller: `openspec-apply-change`
- Payload: minimal verification bundle + retry counters
- Forbidden leak: previous verifier iteration memory
- Return form: normalized findings + routing decision

Contrast structure:
- Allow: fresh verifier per iteration with explicit runtime-profile source
- Forbid: sticky verifier memory or undocumented runtime-profile changes

Verification hook:
Inspect schema/templates/skills and checkpoint evidence for runtime-profile source consistency and fresh-instance rerun semantics.

### Decision: Gemini is a configurable second opinion, not a universal gate

Problem being solved:
The earlier draft drifted toward making Gemini mandatory for every verify checkpoint. That would turn a second-opinion mechanism into a universal hard gate and exceed the intended workflow boundary.

Stack equivalent:
- Risk-tier-aware checkpoint policy in `ai-enforced-workflow`
- Shared verification sequence that can stop after verifier-subagent review or escalate to Gemini

Alternatives considered:
- Make Gemini mandatory for every checkpoint.
- Remove Gemini from the workflow entirely.
- Make Gemini a tier-gated or explicitly enabled second opinion.

Why this option was chosen:
The verifier subagent is the primary local reviewer. Gemini remains valuable as an independent second opinion, but it should be required only when the risk tier or checkpoint policy explicitly needs dual verification. This keeps the workflow centered on isolated Codex review while preserving a stronger gate for stricter changes.

Named deliverables:
- Risk-tier guidance for when Gemini is optional versus mandatory
- Shared verification sequence rules for invoking Gemini only when enabled
- Updated skill/task wording that treats Gemini as a second-opinion phase rather than the universal default

Failure semantics:
- If Gemini is not enabled for a checkpoint, verifier-subagent review is sufficient to complete the local gate.
- If Gemini is enabled and fails after recovery, that checkpoint remains blocked.
- Enabling Gemini for one flow does not imply it is mandatory for all flows.

Boundary examples:
- STRICT artifact checkpoint: verifier-subagent review -> Gemini second opinion required
- STANDARD implementation checkpoint without explicit dual gate: verifier-subagent review only
- STANDARD checkpoint with explicit dual gate: verifier-subagent review -> Gemini second opinion required

Contrast structure:
- Allow: tier-gated or explicitly enabled Gemini second opinion
- Forbid: treating Gemini as a universal mandatory stage for every verify path

Verification hook:
Review schema/tasks/skills for explicit language that Gemini is mandatory only for STRICT or explicitly dual-gated checkpoints.

### Decision: Keep verification policy in schema, move execution flow into shared orchestration

Problem being solved:
The repository currently duplicates the same verification stages in schema instructions, task templates, and two verify skills. Removing policy from schema would make change artifacts less auditable, but leaving execution details duplicated continues drift.

Stack equivalent:
- Policy layer: schema instructions and templates
- Execution layer: shared verify orchestration used by `openspec-artifact-verify` and `openspec-verify-change`

Alternatives considered:
- Push all verify semantics into the verifier agent and leave schema generic.
- Leave schema and skills unchanged and accept duplicated flow.
- Create two separate orchestration paths for artifact and implementation verification.

Why this option was chosen:
Schema artifacts must still state what verification is required so proposals, designs, and tasks remain reviewable. The duplicated "how" should move into one orchestration path so artifact and implementation verification reuse the same local-review, optional-or-mandatory Gemini second opinion, recovery, and routing machinery.

Named deliverables:
- Shared verify orchestration section in schema guidance
- A single shared verification-sequence reference consumed by `openspec-artifact-verify` and `openspec-verify-change`
- Updated task template wording that references shared sequence IDs and contract fields instead of repeating full execution bodies
- Refactored skill instructions that differ only by evidence selection and report paths

Failure semantics:
- If a task or design omits the required verification contract fields, artifact verification is blocking for this `STRICT` workflow change.
- If orchestration cannot map a finding to a redirect layer, repair routing is blocked.

Boundary examples:
- Caller: schema checkpoint task
- Payload: "invoke verifier agent with this evidence bundle, then run shared verification sequence `verify-sequence/default`"
- Returned form: verifier report paths, optional Gemini raw/report paths, final assessment

Contrast structure:
- Current: artifact and implementation verify skills each restate the whole workflow
- Target: skills remain as entry points, while shared orchestration owns the repeated flow

Verification hook:
Review the updated skill docs and schema instructions for a single shared verification sequence and consistent routing language.

### Decision: Define a platform-neutral verifier runner contract with platform adapters

Problem being solved:
The current schema encodes a Windows PowerShell command as workflow policy, but the repository already executes Gemini through a Linux shell helper. This is a platform-porting artifact, not a real policy requirement.

Stack equivalent:
- Contract fields in schema/design/tasks: prompt, raw path, report path, retry count, require-json flag, recovery mode
- Adapter implementations: `.ps1` for Windows and `.sh` for Linux

Alternatives considered:
- Standardize exclusively on PowerShell across all platforms.
- Standardize exclusively on shell scripts across all platforms.
- Leave platform-specific commands embedded in each design and task artifact.

Why this option was chosen:
The workflow needs one stable contract and multiple platform resolvers. That preserves portability without rewriting policy every time the execution environment changes, and it keeps task text from silently becoming the de facto implementation.

Named deliverables:
- Platform-neutral runner contract wording in schema and templates
- Resolver notes for Windows and Linux helper implementations
- Execution-log examples that record the active resolved command without turning it into policy text

Failure semantics:
- Primary runner failure triggers one retry through the shared orchestration layer.
- If the raw envelope exists but the normalized report is missing, the recovery path attempts normalization from raw.
- Verification becomes blocking only after both primary execution and recovery fail on the active platform.

Boundary examples:
- Caller: shared verification sequence
- Payload: logical runner contract plus prompt/raw/report inputs
- Returned form: raw Gemini envelope plus normalized report JSON when Gemini is enabled

Contrast structure:
- Current: workflow policy equals one PowerShell command line
- Target: workflow policy equals a logical runner contract, with platform-specific command resolution underneath

Verification hook:
Inspect schema/templates to confirm they specify runner contract fields and equivalent behavior for both Windows and Linux helper implementations.

### Decision: Bound automatic repair to implementation-layer findings

Problem being solved:
The workflow should be able to consume verifier findings and repair straightforward implementation defects automatically, but it must not silently mutate proposal/spec/design/task layers or loop forever on unresolved blockers.

Stack equivalent:
- Structured finding fields such as `redirect_layer`, `blocking`, and `auto_fixable`
- Repair-loop state tracked by the apply workflow

Alternatives considered:
- Never auto-fix; always require manual routing.
- Auto-fix every finding regardless of layer.
- Route all blocking findings directly to `openspec-repair-change`.

Why this option was chosen:
Implementation-layer fixes are often local and testable, while artifact-layer findings change the contract and must remain explicit. A bounded loop gives the workflow momentum without turning repair into an opaque infinite cycle.

Named deliverables:
- Findings contract extensions or clarifications for `redirect_layer` and `auto_fixable`
- Apply-time repair-loop rules
- Stop-condition guidance for repeated findings and retry budgets

Failure semantics:
- Findings with `redirect_layer != implementation` block automatic implementation repair.
- Repeated blocking findings beyond the configured limit stop the loop and require explicit repair routing.
- Warnings may be recorded without forcing automatic edits unless policy declares otherwise.

Boundary examples:
- Caller: apply workflow after verifier-agent and Gemini results are merged
- Payload: normalized findings, current retry count, repeated-finding history
- Returned form: `auto_fix`, `route_to_repair_change`, or `stop_blocked`

Contrast structure:
- Allow: auto-fix for implementation findings within retry budget
- Forbid: silent artifact edits or unbounded re-verify loops

Verification hook:
Review repair-loop tasks and schema instructions for explicit stop conditions, implementation-only auto-fix scope, and repair-change routing.

### Decision: The verifier subagent writes normalized findings JSON directly

Problem being solved:
The earlier draft left open whether the verifier subagent should emit a final normalized report directly or rely on a second wrapper step. That ambiguity makes the findings contract non-deterministic for a `STRICT` change.

Stack equivalent:
- `verify-reviewer` output artifact under the change `verification/` directory
- Shared findings JSON contract consumed by Gemini prompts and repair routing

Alternatives considered:
- Require the subagent to emit normalized JSON directly.
- Allow arbitrary subagent prose and normalize it later in a wrapper.

Why this option was chosen:
Direct normalized JSON keeps the local-review stage deterministic, auditable, and reusable across artifact and implementation verification. A thin validator may still reject malformed output, but it does not invent structure that the subagent failed to provide.

Named deliverables:
- Normalized subagent review files such as `artifact-subagent-review.json` and `implementation-subagent-review.json`
- Explicit findings-field requirements in delta specs and schema guidance

Failure semantics:
- If the subagent does not produce valid normalized JSON, the local-review stage is blocked.
- Validator logic may check presence and values, but may not silently synthesize missing findings fields.

Boundary examples:
- Caller: shared verify orchestration
- Payload: minimal verification bundle
- Returned form: normalized JSON with `change`, `mode`, `final_assessment`, and `findings`
- Forbidden return: markdown-only summary that requires a wrapper to infer routing fields

Contrast structure:
- Allow: direct normalized findings JSON plus validation
- Forbid: ambiguous free-form output followed by hidden normalization

Verification hook:
Review the verifier-agent spec and tasks for direct JSON output requirements and validator-only fallback wording.

### Decision: Auto-fix eligibility is explicit and conjunctive

Problem being solved:
The earlier draft left open whether `auto_fixable` should be derived from `redirect_layer` or encoded separately. Inferring too much from routing would cause unsafe auto-repair decisions.

Stack equivalent:
- Findings fields `redirect_layer` and `auto_fixable`
- Apply-time repair-loop guard conditions

Alternatives considered:
- Infer auto-fix solely from `redirect_layer=implementation`.
- Encode only `auto_fixable` and let routing remain implicit.
- Require both routing and explicit eligibility.

Why this option was chosen:
`redirect_layer` answers where the issue belongs; `auto_fixable` answers whether the implementer agent may safely repair it automatically. Requiring both avoids accidental auto-fix of implementation findings that still need human judgment.

Named deliverables:
- Findings schema documentation for `redirect_layer` and `auto_fixable`
- Apply-time logic that requires both fields for automatic repair

Failure semantics:
- `redirect_layer != implementation` forces `auto_fixable=false`.
- `redirect_layer=implementation` with `auto_fixable=false` remains repairable only through explicit manual or guided implementation work, not the automatic loop.

Boundary examples:
- Allowed auto-fix: `redirect_layer=implementation`, `blocking=true`, `auto_fixable=true`
- Forbidden auto-fix: `redirect_layer=implementation`, `blocking=true`, `auto_fixable=false`
- Forbidden auto-fix: any finding routed to `proposal`, `specs`, `design`, or `tasks`

Contrast structure:
- Allow: explicit conjunctive rule
- Forbid: implicit inference that every implementation finding can be auto-fixed

Verification hook:
Inspect specs and apply instructions for the explicit two-field gate before auto-repair.

### Decision: Artifact and implementation loops use separate retry budgets

Problem being solved:
The earlier draft left one retry budget open for both artifact and implementation reruns, but the workflows have different correction semantics and risk profiles.

Stack equivalent:
- Artifact verification rerun policy
- Implementation auto-repair rerun policy

Alternatives considered:
- Use one shared retry budget across all verification flows.
- Disable retry budgets entirely and rely on manual reruns.
- Use separate budgets per flow.

Why this option was chosen:
Implementation auto-fix loops can make several bounded local corrections, while artifact repairs should remain explicit and are not eligible for automatic implementation editing. Separate budgets prevent one flow from consuming the other's headroom.

Named deliverables:
- Artifact rerun policy in schema/design/task wording
- Implementation auto-repair rerun policy in apply/repair guidance

Failure semantics:
- Artifact verification may rerun after explicit artifact repair but does not spend implementation auto-fix budget.
- Implementation auto-repair stops when its own retry limit is exhausted or when repeated findings exceed policy.

Boundary examples:
- Artifact path: blocked design finding -> explicit artifact edit -> rerun artifact verification
- Implementation path: auto-fixable code finding -> automatic repair -> rerun implementation verification until budget exhausted

Contrast structure:
- Allow: separate counters by flow
- Forbid: one shared retry counter that hides which loop failed

Verification hook:
Review tasks and repair guidance for distinct artifact-rerun wording versus implementation auto-fix wording.

### Decision: Tasks reference the logical runner contract, while execution evidence records the resolved command

Problem being solved:
The draft left unresolved whether checkpoint artifacts should mention only the logical runner contract or also the active platform command. Inlining the full resolved command in every task reintroduces platform-specific drift, while omitting it entirely weakens auditability.

Stack equivalent:
- Task and design checkpoint text
- Runner contract fields plus resolved command example

Alternatives considered:
- Record only the logical runner contract.
- Record only the active platform command.
- Put the logical contract in tasks and the resolved command in execution evidence.

Why this option was chosen:
The logical contract keeps policy platform-neutral. Recording the resolved command in execution evidence preserves operational auditability and reproducibility for the active environment without forcing tasks to duplicate implementation detail.

Named deliverables:
- Updated task wording that references the contract, report paths, and shared sequence
- Updated schema/template guidance to separate policy from adapter resolution
- Execution-log or checkpoint-evidence conventions for recording the resolved command

Failure semantics:
- Missing logical contract details means the checkpoint is under-specified.
- Missing resolved command details in execution evidence means the checkpoint is not operationally auditable, but it does not force tasks to inline shell-specific bodies.

Boundary examples:
- Contract: `gemini-capture` runner with prompt/raw/report/recovery parameters
- Execution evidence: Linux resolved command `./openspec/bin/gemini_capture.sh ...`
- Execution evidence: Windows resolved command `powershell -NoProfile -ExecutionPolicy Bypass -File $env:APPDATA\openspec\bin\gemini_capture.ps1 ...`

Contrast structure:
- Allow: task policy plus separately recorded platform-resolved command
- Forbid: encoding platform command as the task policy itself

Verification hook:
Inspect task/template text for logical contract references and execution evidence conventions, not duplicated shell bodies.

## Independent Verification Plan (STANDARD/STRICT)

This change modifies verification itself, so the plan documents both the current enforcement path and the target behavior it will introduce. In this repository, the active helper path is Linux-first through `./openspec/bin/gemini_capture.sh`, while the target schema wording will be rewritten around a platform-neutral runner contract.

Document verification as:
- Stage 1: implementer agent completes artifact or code changes
- Stage 2: explicit read-only verifier subagent performs isolated local review
- Stage 3: Gemini CLI provides a second opinion only when the risk tier or checkpoint policy enables dual verification

The verifier subagent is the primary local review stage. Gemini is tier-gated second-opinion coverage, not a universal mandatory stage.

### Artifact Verification

- Local reviewer: project-scoped read-only verifier subagent `verify-reviewer`
- Local review scope: `openspec/changes/introduce-verify-subagent-workflow/proposal.md`, `openspec/changes/introduce-verify-subagent-workflow/design.md`, `openspec/changes/introduce-verify-subagent-workflow/tasks.md`, `openspec/changes/introduce-verify-subagent-workflow/specs/**/*.md`, and updated verify-related schema/template/skill files named by the task under review
- Local review output: structured findings or explicit `no findings` written to `openspec/changes/introduce-verify-subagent-workflow/verification/artifact-subagent-review.json`
- Verifier runtime profile source: `.codex/agents/verify-reviewer.toml` by default; optional checkpoint-level override when explicitly documented
- Second-opinion source: `Gemini CLI` via logical runner contract `gemini-capture`
- Second-opinion policy: mandatory for `STRICT` artifact checkpoints and optional otherwise
- Prompt inputs:
  - `openspec/changes/introduce-verify-subagent-workflow/proposal.md`
  - `openspec/changes/introduce-verify-subagent-workflow/design.md`
  - `openspec/changes/introduce-verify-subagent-workflow/tasks.md`
  - `openspec/changes/introduce-verify-subagent-workflow/specs/**/*.md`
  - `openspec/changes/introduce-verify-subagent-workflow/verification/artifact-subagent-review.json`
- Second-opinion outputs when enabled: raw Gemini envelope plus normalized report JSON at the checkpoint-defined paths
- Fallback behavior: shared runner recovery sequence retries once and then attempts raw-envelope normalization before blocking
- Loop behavior: each rerun must spawn a fresh verifier instance with no inherited verifier memory
- Skill entry point: `openspec-artifact-verify`

### Implementation Verification

- Local reviewer: project-scoped read-only verifier subagent `verify-reviewer`
- Local review scope: all changed verification workflow files under `openspec/schemas/ai-enforced-workflow/`, `.codex/skills/openspec-artifact-verify/`, `.codex/skills/openspec-verify-change/`, `.codex/skills/openspec-repair-change/`, `.codex/agents/`, and any runner-resolution helpers touched by this change, plus the final `tasks.md` and any generated verification reports
- Local review output: structured findings or explicit `no findings` written to `openspec/changes/introduce-verify-subagent-workflow/verification/implementation-subagent-review.json`
- Verifier runtime profile source: `.codex/agents/verify-reviewer.toml` by default; optional checkpoint-level override when explicitly documented
- Second-opinion source: `Gemini CLI` via logical runner contract `gemini-capture`
- Second-opinion policy: mandatory only for `STRICT` or explicitly dual-gated implementation checkpoints
- Prompt inputs:
  - `openspec/changes/introduce-verify-subagent-workflow/design.md`
  - `openspec/changes/introduce-verify-subagent-workflow/specs/**/*.md`
  - `openspec/changes/introduce-verify-subagent-workflow/tasks.md`
  - `openspec/changes/introduce-verify-subagent-workflow/verification/implementation-subagent-review.json`
  - touched verification workflow code and agent-definition paths
- Second-opinion outputs when enabled: raw Gemini envelope plus normalized report JSON at the checkpoint-defined paths
- Fallback behavior: shared runner recovery sequence retries once and then attempts raw-envelope normalization before blocking
- Loop behavior: after eligible implementation auto-fix, rerun with a fresh verifier instance and no inherited verifier memory
- Skill entry point: `openspec-verify-change`

If repair reruns are required, `openspec-repair-change` consumes the latest normalized findings plus the raw/report paths it supersedes.

## Migration Plan

1. Add the new change specs and design for verifier-agent orchestration and platform-neutral runner behavior.
2. Introduce the project-scoped read-only verifier agent definition and document its invocation contract.
3. Refactor `ai-enforced-workflow` schema instructions and templates so verification steps require explicit verifier-subagent invocation, minimal verification bundles, implementation-only auto-fix rules, and platform-neutral runner wording.
4. Update `openspec-artifact-verify` and `openspec-verify-change` so they become thin entry points over shared orchestration rather than duplicating the whole local-review/Gemini flow.
5. Update `openspec-repair-change` and apply-time instructions to honor the new findings contract, auto-fix scope, retry budget, and stop conditions.
6. Preserve Windows and Linux helper parity by documenting the shared runner contract and ensuring each platform helper satisfies the same raw/report/recovery semantics.
7. Add explicit verifier runtime-profile source and optional override rules to shared contracts and skills so behavior is project-agnostic.
8. Run artifact verification against the schema/design/tasks output, then implement and rerun implementation verification on the changed workflow files, using fresh verifier instances on every rerun and Gemini second-opinion reruns only where checkpoint policy enables them.

## Open Questions

- Should runtime verification evidence for Windows command-resolution be captured in this change now, or in a follow-up hardening change?

## Risks / Trade-offs

- Adding a verifier agent improves independence but introduces one more moving part in the workflow runtime.
- Minimal-context review reduces contamination, but the evidence bundle must be complete enough to avoid false negatives.
- Platform-neutral policy reduces Windows-port baggage, but the adapter contract must be strict or implementations will drift again.
- Auto-repair improves momentum for local implementation defects, but weak stop conditions would create loops or hide deeper specification problems.
- Keeping verify policy in schema preserves auditability, but it means the wording must stay synchronized with orchestration code and agent contracts.
