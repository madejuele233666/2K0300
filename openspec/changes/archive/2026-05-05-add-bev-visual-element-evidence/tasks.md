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
- Artifact-completion gate ownership:
  - artifact creation owns docs-first review before implementation entry
  - implementation owns source-first review before archive

## 1. OpenSpec Artifacts

- [x] 1.1 Create proposal, design, spec deltas, and tasks for `add-bev-visual-element-evidence`.
- [x] 1.2 Validate the change with `openspec validate add-bev-visual-element-evidence --strict`.
- [x] 1.3 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for proposal/specs/design/tasks. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON and verifier evidence JSON, and require caller/orchestrator-maintained `agent-table.json`.

## 2. Runtime Element Evidence

- [x] 2.1 Add visual element evidence port types and default-off `BEV_ELEMENT.CROSS_EXIT_TAKEOVER_ENABLED` runtime parameter.
- [x] 2.2 Extend sparse row scan facts with row sample support statistics without changing line reference selection.
- [x] 2.3 Implement `cross_exit` evidence detection and disabled-by-default candidate construction.
- [x] 2.4 Wire perception frontend so enabled element candidates may enter orchestration only when the takeover parameter is true.

## 3. Observability And Tooling

- [x] 3.1 Add `element_evidence.cross_exit` to `PerceptionResult`, `ControlDebugSnapshot`, assistant telemetry, and steering media image-frame headers.
- [x] 3.2 Update scene overlay probe to print cross evidence facts and keep selected reference path drawing semantics unchanged.
- [x] 3.3 Update config snapshot and parameter docs for `BEV_ELEMENT`.

## 4. Verification

- [x] 4.1 Add visual element evidence unit tests covering present, absent, weak support, disabled takeover, enabled takeover, and gap-safe candidate behavior.
- [x] 4.2 Update existing parameter, perception, assistant telemetry, steering media, and residual checks.
- [x] 4.3 Run targeted regression commands listed in the proposal plan, including no-upload build and `git diff --check`.
- [x] 4.4 Include a board no-motion smoke/capture task for runtime protocol changes: build/upload, start normal no-motion, capture assistant/media, and inspect `element_evidence.cross_exit`.
- [x] 4.5 [Checkpoint] Run source-first verifier-subagent review using `verify-sequence/default` for changed code/tests/config/docs. Reference field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Follow `cycle_rules` for agent lifecycle. Require authoritative findings JSON and verifier evidence JSON, and require caller/orchestrator-maintained `agent-table.json`.
