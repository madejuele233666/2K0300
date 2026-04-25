## 0. Verification Contract

- Shared sequence:
  - `openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`
- Shared JSON verification contract:
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`
- Shared field groups:
  - `invocation_common_required`
  - `output_paths_required`
  - `verifier_evidence_required`
  - `valid_pass_requirements`
  - `partial_scope_rule`
- Routing target for blocking findings:
  - `openspec-repair-change`
- Supported continuation overrides:
  - `verify-only`
  - `dry-run`
  - `manual_pause`
- Artifact-completion gate ownership:
  - when this task list completes the schema's `applyRequires` set under
    `ai-enforced-workflow`, the active artifact-creation caller
    (`openspec-propose` or `openspec-continue-change`)
    runs docs-first review before implementation entry
  - `openspec-apply-change` does not own that docs-first artifact gate

## 1. <!-- Task Group Name -->

- [ ] 1.1 <!-- Task description -->
- [ ] 1.2 <!-- Task description -->

## 2. <!-- Task Group Name -->

- [ ] 2.1 <!-- Task description -->
- [ ] 2.2 <!-- For STANDARD/STRICT work: document verification checkpoints, active/non_active semantics, any optional `.index/` reference material as non-authoritative background only, authoritative evidence paths, and current-state-only `agent-table.json`. State that usable active agents are continued first, that callers prefer `send_input` while the same active agent is still open, that `send_input` returning `agent not found` still routes to `resume`, not spawn, that callers use `continuation_probe` to distinguish resume from recovery spawn, that new agents are spawned only when no usable active agent exists, that `no_usable_active_agent` means only that `agent-table.json` literally contains no `active` agent, that recovery spawns keep distinct reason codes such as `active_agent_missing` or `active_agent_not_resumable`, that `spawn_active` decisions expose machine-readable `spawn_reason_code`, that only `block -> pass` marks an agent `non_active`, and that termination depends on a valid active pass. -->
- [ ] 2.3 [Checkpoint] Run verifier-subagent review for <!-- schema diff / skill output / verification report --> using `verify-sequence/default`. Use the verification contract above for field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Require `review_goal=implementation_correctness`. Write authoritative findings JSON and verifier evidence JSON; the caller/orchestrator reconciles and writes `agent-table.json`. Follow `cycle_rules` for agent lifecycle. Require fields from `verifier_evidence_required`, enforce valid-pass requirements, and require explicit `scope` for any partial verification.

## 3. <!-- Task Group Name -->

- [ ] 3.1 <!-- Task description -->
- [ ] 3.2 <!-- Task description -->
