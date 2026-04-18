## 0. Verification Contract

- Shared sequence:
  - `openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`
- Shared JSON verification contract:
  - `openspec/schemas/modules/review-loop/contracts/review-loop-core-v1.json`
  - `openspec/schemas/modules/review-loop/contracts/review-loop-openspec-adapter-v1.json`
- Cache-helper sequence:
  - `openspec/schemas/ai-enforced-workflow/index-sequence.md#index-sequence/default`
- Shared field groups:
  - `bundle_required`
  - `index_context_optional`
  - `output_paths_required`
  - `output_paths_optional`
  - `verifier_evidence_required`
  - `implementation_evidence_required`
  - `challenger_entry.source_review_required`
- Routing target for blocking findings:
  - `openspec-repair-change`
- Supported continuation overrides:
  - `verify-only`
  - `dry-run`
  - `manual_pause`

## 1. Actuator Contract And Document Alignment

- [x] 1.1 Update `new/docs/race-finish-series.zh-CN/00-master-roadmap.md`, `01-phase-a-hardware-and-closure.md`, `07-current-progress.md`, and directly related phase references so they reflect the accepted direct-match startup evidence bundle at `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.log`, `runtime-smoke-retry-2026-04-15.exit`, and `hardware-discovery-retry-2026-04-15.log`, use `runtime-smoke-execution-evidence.md` as the supersession note for earlier direct-match artifacts, describe the platform as pure differential steering, and define `turn_output` as a differential steering adjustment.
- [x] 1.2 Remove `servo_pwm` from `new/code/port/control_types.hpp` and update all compile-time consumers under `new/code/legacy/*`, `new/code/runtime/*`, and any documentation/examples so the public actuator contract becomes `left_pwm + right_pwm + emergency_stop` only.
- [x] 1.3 Rebuild through `new/user/build.sh` and run a targeted contract audit (for example `rg "servo_pwm|servo" new/code new/docs/race-finish-series.zh-CN`) to verify no public runtime/legacy path still depends on a nonexistent servo topology.

## 2. Runtime-Owned Control Gate Observability

- [x] 2.1 Add a project-owned runtime helper or equivalent normalized decision model under `new/code/runtime/` that evaluates control-gating inputs and produces explicit project-owned veto / arming / apply-outcome state without exposing vendor bridge details.
- [x] 2.2 Update `new/code/runtime/control_loop.*`, any directly related runtime-state definitions, and affected diagnostics so Phase A evidence can distinguish stale perception, perception emergency veto, low voltage, invalid IMU, invalid encoder, requested non-empty output, actuator arming transitions, and motor-apply failure while keeping `legacy` focused on control math only.
- [x] 2.3 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for actuator-contract cleanup and runtime control observability outputs. Use repository-index cache helper `index-sequence/default` only when cache discovery or refresh is useful; do not require cache authority before review. Reference the shared field groups in `openspec/schemas/modules/review-loop/contracts/review-loop-core-v1.json` and `openspec/schemas/modules/review-loop/contracts/review-loop-openspec-adapter-v1.json` for `bundle_required`, `index_context_optional`, `output_paths_required`, `output_paths_optional`, `verifier_evidence_required`, and `implementation_evidence_required`, plus the routing target and caller-local guardrail “review only project-owned files under `new/code/` and change-local docs; do not treat board unavailability as a code-pass.” Require authoritative verifier findings JSON at `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/implementation-working-findings.json` plus verifier execution evidence JSON at `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/implementation-working-evidence.json`. State that same-session implementation reruns are convergence-only, challenger is the only implementation closure authority, and challenger entry requires the orchestrator to validate the previous working agent's recorded outputs using `challenger_entry.source_review_required`. If the checkpoint explicitly enables Gemini dual review, require logical runner contract `gemini-capture`, raw plus normalized output, and recovery via `input_raw_path -> report_path`.

## 3. Phase A Evidence And Closure Readiness

- [x] 3.1 Refresh Phase A verification notes, evidence-path references, and any adjacent guidance so the accepted default direct-match baseline is explicitly pinned to `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.{log,exit}` plus `hardware-discovery-retry-2026-04-15.log`, with `runtime-smoke-execution-evidence.md` identifying superseded artifacts, and so the remaining board-only work is explicitly framed as IMU sample quality, encoder direction/scale, motor-output proof, and control unlock proof instead of unresolved startup bring-up.
- [x] 3.2 Run document/code verification for this change with `openspec-artifact-verify` and `openspec-verify-change` using `verify-sequence/default`; write authoritative artifact-review findings/evidence under `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/artifact-working-*.json` and implementation-review findings/evidence under `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/implementation-<pass>-*.json`; require the shared field groups from `review-loop-core-v1.json` and `review-loop-openspec-adapter-v1.json`, keep same-session working reruns for convergence only, and allow challenger entry only after the orchestrator validates the previous working agent outputs.
- [x] 3.3 If the target board is reachable during implementation, rerun the standard `new/user/run_remote_smoke.sh` path and store caller-visible evidence notes that connect the new observability markers to the existing direct-match baseline; if the board is not reachable, record that as an evidence gap rather than weakening the implementation acceptance rule.
