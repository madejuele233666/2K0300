## 0. Verification Contract

- Shared sequence:
  - `openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`
- Shared JSON verification contract:
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`
- Cache-helper sequence:
  - `openspec/schemas/ai-enforced-workflow/index-sequence.md#index-sequence/default`
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
    (`openspec-propose`, `openspec-ff-change`, or `openspec-continue-change`)
    runs docs-first review before implementation entry
  - `openspec-apply-change` does not own that docs-first artifact gate

## 1. <!-- Task Group Name -->

- [ ] 1.1 <!-- Task description -->
- [ ] 1.2 <!-- Task description -->

## 2. <!-- Task Group Name -->

- [ ] 2.1 <!-- Task description -->
- [ ] 2.2 <!-- For STANDARD/STRICT work: document verification checkpoints, active/non_active semantics, optional cache-helper use, authoritative evidence paths, and `agent-table.json`. State that usable active agents are continued first, that callers prefer `send_input` while the same active agent is still open, that `send_input` returning `agent not found` still routes to `resume`, not spawn, that callers use `resume` only when that same active agent was closed and must be restored, that new agents are spawned only when no usable active agent exists, that `no_usable_active_agent` means only that `agent-table.json` literally contains no `active` agent, that recovery spawns keep distinct reason codes such as `active_agent_missing` or `active_agent_not_resumable`, that `spawn_active` decisions expose machine-readable `spawn_reason_code`, that only `block -> pass` marks an agent `non_active`, and that termination depends on a valid active pass. -->
- [ ] 2.3 [Checkpoint] Run verifier-subagent review for <!-- schema diff / skill output / verification report --> using `verify-sequence/default`. Use repository-index helper `index-sequence/default` only when cache discovery or refresh is useful; do not require cache authority before review. Use the verification contract above for the shared field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`, the routing target, and any caller-local guardrails. Require checkpoints to use `review_goal=implementation_correctness`. Write authoritative verifier-subagent findings JSON, verifier execution evidence JSON, and `agent-table.json`. Require checkpoints to include fields from `verifier_evidence_required`, enforce the valid-pass requirements, and require explicit `scope` for any partial verification. State that usable active agents are continued first, that callers prefer `send_input` while the same active agent is still open, that `send_input` returning `agent not found` still routes to `resume`, not spawn, that callers use `resume` only when that same active agent was closed and must be restored, that new agents are spawned only when no usable active agent exists, that `no_usable_active_agent` means only that `agent-table.json` literally contains no `active` agent, that recovery spawns keep distinct reason codes such as `active_agent_missing` or `active_agent_not_resumable`, that `spawn_active` decisions expose machine-readable `spawn_reason_code`, that only `block -> pass` marks an agent `non_active`, and that termination depends on a valid active pass. If the checkpoint explicitly enables Gemini dual review, run logical runner contract `gemini-capture`, write both raw and normalized reports, require JSON-normalized output, and use recovery (`input_raw_path -> report_path`) before blocking.

## 3. <!-- Task Group Name -->

- [ ] 3.1 <!-- Task description -->
- [ ] 3.2 <!-- Task description -->
