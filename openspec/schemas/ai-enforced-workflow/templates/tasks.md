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

## 1. Vertical Slice: <!-- Observable outcome -->

- [ ] 1.1 Identify the smallest end-to-end path and behavior-level feedback loop for this slice.
- [ ] 1.2 Modify concrete deliverables for this slice only.
- [ ] 1.3 Run the focused feedback loop and record the evidence path or command output.

## 2. Verification and Review

- [ ] 2.1 Confirm domain language and ADR constraints used by this change, if those files exist.
- [ ] 2.2 <!-- For STANDARD/STRICT work: document verification checkpoints, authoritative evidence paths, and current-state-only `agent-table.json`. Reference `verify-sequence/default` for active/non_active semantics instead of restating the full state machine. -->
- [ ] 2.3 [Checkpoint] Run verifier-subagent review for <!-- schema diff / skill output / verification report --> using `verify-sequence/default`. Use the verification contract above for field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Require `review_goal=implementation_correctness`. Write authoritative findings JSON and verifier evidence JSON; the caller/orchestrator reconciles and writes `agent-table.json`. Follow `cycle_rules` for agent lifecycle. Require fields from `verifier_evidence_required`, enforce valid-pass requirements, and require explicit `scope` for any partial verification.

## 3. Cleanup and Decision Capture

- [ ] 3.1 Delete or absorb any prototype created to answer a design question.
- [ ] 3.2 Capture durable decisions in design.md, specs, ADRs, or task evidence as appropriate.
