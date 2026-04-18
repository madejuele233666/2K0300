## Context

The current `new/` runtime has already crossed the “can build / can deploy / can direct-match start” threshold, but the Phase A documents still describe IMU direct-match startup as unresolved. In parallel, the public actuator contract still exposes `servo_pwm` even though the vehicle has no independent steering servo and all steering is produced by left/right wheel differentials. The current control-loop diagnostics also compress multiple gating causes into a single `control.veto` marker, which makes Phase A closure noisy: startup proof, steady-state sensor validity, perception freshness, and actuator-application outcome are all difficult to separate in evidence.

This change is intentionally narrow. It does not add a new driving behavior, semantic capability, or hardware subsystem. It makes the project-owned runtime boundary cleaner and makes the remaining Phase A work reviewable without leaking vendor concerns into legacy logic.

## Goals / Non-Goals

**Goals:**

- Remove the servo-shaped public contract from the actuator path so the owned interface matches the real differential-drive platform.
- Keep `legacy` focused on differential-drive mixing and control math, not runtime gating explanation or vendor-facing diagnostics.
- Introduce a project-owned control-gating/observability boundary that explains why output is vetoed, why actuators are armed/disarmed, and whether a requested command was actually applied.
- Align Phase A roadmap/progress documents with the accepted direct-match board evidence bundle at `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.log`, `runtime-smoke-retry-2026-04-15.exit`, and `hardware-discovery-retry-2026-04-15.log`, with `runtime-smoke-execution-evidence.md` naming how that retry supersedes earlier 2026-04-12 direct-match artifacts.
- Keep later Phase A evidence work straightforward: IMU sample validation, encoder direction checks, motor-output checks, and control unlock checks should all be able to read the same project-owned signals.

**Non-Goals:**

- Reworking the true vendor bridge layout or introducing a new hardware abstraction model.
- Implementing Phase B low-speed vehicle motion, semantic perception, or `old_2` scene-state migration.
- Solving IMU zero-bias calibration, encoder calibration, or motor-output validation in this change itself.
- Replacing the existing `legacy` control algorithm or renaming all historical variables in one pass.

## Decisions

### Decision: The public actuator contract becomes differential-drive-only

- Problem being solved:
  The current `port::ActuatorCommand` exposes `servo_pwm` even though the real platform has no steering servo. That field misstates the actuator topology and encourages future code or docs to treat steering as a hidden servo-angle path.
- Alternatives considered:
  Keep `servo_pwm` as a permanently ignored compatibility field; keep it temporarily but rename/comment it harder; remove it entirely from the public contract.
- Why this option was chosen:
  Removal is the only option that makes the owned interface match the real platform. Because `legacy` already mixes `mean_pwm` and `turn_output` into left/right wheel output, the field is not needed for current behavior and only adds conceptual debt.
- Stack Equivalent:
  Historical steering intent remains `mean_pwm + differential adjustment -> left/right PWM`; the stack-equivalent public form is a two-wheel command, not a servo-bearing command.
- Named Deliverables:
  `new/code/port/control_types.hpp`, `new/code/legacy/motor_logic.*`, any runtime state or diagnostics code that stores actuator commands, and Phase A / roadmap docs that describe actuator topology.
- Failure Semantics:
  Build-time breakage is acceptable and desired if any code still depends on `servo_pwm`; no silent compatibility shim is introduced.
- Boundary Examples:
  `legacy` may still compute a scalar `turn_output`, but the only published command becomes `left_pwm`, `right_pwm`, and `emergency_stop`.
- Contrast Structure:
  Before: project-owned actuator contract implied “differential + maybe servo”.
  After: project-owned actuator contract states “differential only”.
- Verification Hook:
  Search the `new/` tree for `servo_pwm`; the only acceptable result after implementation is no public runtime/legacy dependency on that field.

### Decision: Control gating and observability move into a runtime-owned decision boundary

- Problem being solved:
  `control_loop.cpp` currently computes veto from several inputs at once and emits only a coarse `control.veto` marker. That is enough for emergency suppression, but not enough for elegant review or for Phase A closure because it does not distinguish stale perception, perception veto, invalid IMU, invalid encoder, low voltage, or motor-application failure.
- Alternatives considered:
  Add more ad hoc markers directly in `Tick()`; log raw booleans inline without a shared model; introduce a project-owned runtime helper that evaluates gating inputs and returns a normalized decision snapshot.
- Why this option was chosen:
  A runtime-owned helper keeps the boundary clean: `legacy` stays unaware of runtime diagnostics, vendor bridges stay unaware of control semantics, and `control_loop` becomes an orchestrator that consumes one normalized project-owned decision.
- Stack Equivalent:
  The stack equivalent is `raw runtime inputs -> control gate decision -> command request -> actuator apply result -> observable snapshot`.
- Named Deliverables:
  A new runtime-owned control-gating/observability helper or type, targeted updates in `new/code/runtime/control_loop.*`, possibly `runtime_state.hpp`, and documentation/verification markers that reference normalized reasons rather than vendor-specific details.
- Failure Semantics:
  Unknown or invalid inputs still fail safe. The helper does not weaken veto behavior; it makes veto causes explicit. If command application fails, the runtime records that outcome and forces the armed state back to false.
- Boundary Examples:
  Inputs: `perception.fresh`, `perception.emergency_veto`, `imu.valid`, `encoder.valid`, `low_voltage`.
  Outputs: `veto_reason`, `requested_command_nonzero`, `apply_result`, `actuators_armed`.
  Non-example: exposing `/dev/zf_*` paths or bridge error structs in the control decision model.
- Contrast Structure:
  Before: gating logic and marker emission are interleaved in one timer callback.
  After: gating evaluation is one project-owned step, command application is a second step, and diagnostics consume the normalized result.
- Verification Hook:
  Phase A logs can show which veto reason dominated a cycle and whether a non-empty command was requested/applied without reading vendor bridge internals.

### Decision: Phase A documents distinguish startup proof from control-unlock proof

- Problem being solved:
  The docs currently blur two different statements:
  1. “default direct-match startup now works on-board”
  2. “the vehicle has stable control unlock and trustworthy sensor samples”
  Those are not the same.
- Alternatives considered:
  Keep the older wording and treat the difference as reviewer context; update only `07-current-progress`; align the roadmap, Phase A task list, and current progress docs together.
- Why this option was chosen:
  The work is phase-gated. If the docs keep calling startup-grade evidence “still unknown”, implementation order will stay wrong. If they overstate startup proof as full closure, later work will skip needed IMU/encoder/motor evidence.
- Stack Equivalent:
  Startup proof becomes one explicit artifact class; control-unlock proof becomes a later explicit artifact class.
- Named Deliverables:
  `new/docs/race-finish-series.zh-CN/00-master-roadmap.md`, `01-phase-a-hardware-and-closure.md`, `07-current-progress.md`, and any Phase A references to actuator topology or `turn_output`.
- Failure Semantics:
  Documentation must not claim Phase A is passed unless control unlock, actuator output, and sample quality evidence also exist.
- Boundary Examples:
  Acceptable wording: “direct-match startup is confirmed; IMU sample quality and control unlock remain open”.
  Unacceptable wording: “IMU direct-match is unresolved” or “control is closed-loop ready” without new evidence.
- Contrast Structure:
  Before: one ambiguous “IMU not closed” statement covered both startup and sample-quality questions.
  After: startup status and closure gaps are split explicitly.
- Verification Hook:
  Reviewers can compare docs against the accepted direct-match evidence bundle at `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.{log,exit}` plus `hardware-discovery-retry-2026-04-15.log`, and use `runtime-smoke-execution-evidence.md` to confirm that this retry is the superseding baseline for later Phase A wording.

## Independent Verification Plan (STANDARD/STRICT)

Document verification using shared sequence `verify-sequence/default` from:
`openspec/schemas/ai-enforced-workflow/verification-sequence.md`
and shared JSON verification contract:
`openspec/schemas/modules/review-loop/contracts/review-loop-core-v1.json`
and OpenSpec subject adapter:
`openspec/schemas/modules/review-loop/contracts/review-loop-openspec-adapter-v1.json`

Stage A flow:
- Artifact review:
  - docs are the primary surface
  - passing artifact review allows implementation entry
  - blocking artifact findings stop implementation entry
- Implementation review:
  - code, tests, and directly impacted code are the primary surface
  - approved artifacts are reference material
  - repository-index support is optional cache help only
  - same-session reruns stay in the active working reviewer session
  - challenger pass is the only implementation closure authority

Runtime profile policy:
- Use verifier runtime profile from `.codex/agents/verify-reviewer.toml`.
- Use index-maintainer runtime profile from
  `.codex/agents/index-maintainer.toml` when cache refresh is useful.

Loop rule:
- Same-session implementation reruns MAY continue inside the active working
  reviewer session after repair.
- Same-session zero findings are convergence-only.
- Challenger pass MUST run in a newly spawned verifier session.
- If a challenger pass returns findings, that session becomes the new active
  working baseline.
- Only an implementation challenger pass with zero findings may close the
  checkpoint.
- Only the main orchestrator may authorize challenger entry or closure, and it
  must not substitute its own judgment for the prior working sub-agent output.

### Artifact Verification

- Sequence reference: `verify-sequence/default`
- Mode: `artifact`
- Review phase: `artifact`
- Review pass type: `working`
- Verifier agent path: `.codex/agents/verify-reviewer.toml`
- Invocation template id: `verify-reviewer-inline-v2`
- Primary review surface: changed `proposal.md`, `design.md`, `tasks.md`, and change-local `specs/**`
- Default cache mode: `bypassed`
- Authoritative verifier-subagent findings JSON path:
  `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/artifact-working-findings.json`
- Verifier execution evidence JSON path:
  `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/artifact-working-evidence.json`
- Acceptance rule: blocking findings stop implementation entry
- Skill entry point: `openspec-artifact-verify`

### Implementation Verification

- Sequence reference: `verify-sequence/default`
- Cache-helper sequence reference: `index-sequence/default`
- Review goal: `implementation_correctness`
- Review pass types:
  - `working`
  - `challenger`
- Verifier agent path: `.codex/agents/verify-reviewer.toml`
- Index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- Invocation template id: `verify-reviewer-inline-v2`
- Primary review surface: changed code, changed tests, directly impacted runtime/legacy/docs files
- Optional cache-helper report path:
  `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/index-preflight.json`
- Authoritative verifier-subagent findings JSON path:
  `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/implementation-<pass>-findings.json`
- Verifier execution evidence JSON path:
  `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/implementation-<pass>-evidence.json`
- Inline implementation evidence:
  - `review_scope`
  - `review_coverage`
- Loop behavior:
  - same-session reruns handle convergence
  - challenger pass starts only after the orchestrator validates the previous
    working agent's recorded outputs
- Continuation target on pass:
  archive or continue to the next Phase A implementation checkpoint
- Skill entry point: `openspec-verify-change`

### Optional Gemini Dual Review

Use only when repository or checkpoint policy explicitly enables dual review.

- Runner contract: `gemini-capture`
- Raw report path:
  `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/gemini-raw.json`
- Normalized report path:
  `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/gemini-report.json`
- Maximum attempts:
  1 per checkpoint unless repository policy says otherwise
- Recovery behavior:
  - `input_raw_path -> report_path`
- Resolved command goes in execution evidence, not workflow policy prose

## Repository Index Cache Plan (When Useful)

- Index contract id: `repo-index-v1`
- Canonical repository-index root:
  `.ai/repository-index`
- Shared cache-helper sequence:
  `index-sequence/default`
- Optional refresh scoping hints:
  runtime control path, port contracts, Phase A docs, and verification artifacts only
- Fallback policy (`refresh|bypass`):
  `bypass`
- Verifier invocation template:
  `verify-reviewer-inline-v2`
- Index-maintainer agent path:
  `.codex/agents/index-maintainer.toml`
- Index skill entry points:
  - `openspec-index-preflight`
  - `openspec-index-maintain`
- Cache-helper evidence path convention:
  `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/index-*.json`
- Findings path convention:
  `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/*-findings.json`
- Verifier execution evidence path convention:
  `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/*-evidence.json`
- Cache handoff fields:
  - `index_context.contract`
  - `index_context.manifest_path`
  - `index_context.manifest_present`
  - `index_context.preflight_report_path`
  - `index_context.cache_mode`
  - `index_context.fallback_policy`

Shared field groups from `review-loop-core-v1.json` and
`review-loop-openspec-adapter-v1.json`:
- `bundle_required`
- `index_context_optional`
- `output_paths_required`
- `output_paths_optional`
- `verifier_evidence_required`
- `implementation_evidence_required`
- `challenger_entry.source_review_required`

Review completion contract:
- execution evidence MUST record:
  - `review_goal`
  - `review_pass_type`
  - `cache_mode`
  - `closure_authority`
  - `verifier_output_path`
  - `reviewed_paths`
  - `skipped_paths`
  - `reviewed_axes`
  - `unreviewed_axes`
  - `coverage_status`
  - `saturation_status`
  - optional `early_stop_reason`
  - optional `skip_reasons`
- implementation review MUST additionally record inline:
  - `review_scope`
  - `review_coverage`

## Migration Plan

1. Correct the documentation baseline first so Phase A status, actuator topology, and direct-match proof are no longer self-contradictory.
2. Remove `servo_pwm` from the public actuator contract and update all compile-time consumers to express differential-drive-only output.
3. Introduce a runtime-owned control-gating/observability helper or equivalent normalized decision path, then update `control_loop` to consume it.
4. Emit project-owned diagnostics for veto cause, arm/disarm transitions, and command-application outcome without exposing vendor bridge internals.
5. Refresh Phase A evidence naming and task wording so the remaining work is explicitly IMU sample quality, encoder direction/scale, motor-output proof, and control unlock proof.

Rollback is straightforward at the code level because the change is local to project-owned contracts and docs. If needed, implementation can stop after the documentation and contract cleanup step without altering any vendor bridge.

## Open Questions

- Should the code symbol `turn_output` remain for historical continuity while only its documentation is tightened, or should implementation rename it in the same change if that can be done without increasing blast radius?
- Should control observability emit one structured project-owned marker per dominant veto reason, or keep `control.veto` as the umbrella marker and add reason-specific companion markers/messages?

## Risks / Trade-offs

- Removing `servo_pwm` is intentionally breaking for any hidden dependency; that is useful, but it can widen the edit set beyond the obvious files.
- Adding fine-grained control observability can become noisy if implemented as raw marker spam; rate-limited, dominant-reason reporting is preferred.
- A new runtime-owned helper improves cleanliness, but over-structuring it would be unnecessary if the implementation only needs a small normalized decision model.
- Documentation alignment reduces confusion, but it also removes the crutch of treating degraded-path evidence as if it were enough for Phase A closure.
