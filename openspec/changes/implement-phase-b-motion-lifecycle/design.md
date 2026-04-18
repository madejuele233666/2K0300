## Context

Phase B is now the active project stage in `new/docs/race-finish-series.zh-CN/02-phase-b-low-speed-vehicle-motion.md`, but the current runtime still has only two practical layers:

- process/resource lifecycle: `RunStartup()` / `RunShutdown()`
- control gate plus direct control law execution: `ControlLoop::Tick()`

That shape was sufficient for Phase A startup, direct-match readiness, and control-unveto evidence, but it is not sufficient for low-speed real-vehicle motion because it cannot describe these contracts explicitly:

- operator wants to start vs system is allowed to start
- system is healthy vs system is intentionally held disarmed
- spinup vs steady running vs controlled stop
- transient fail-safe veto vs latched post-fault recovery boundary
- bounded smoke automation vs long-lived product semantics

### Reference Alignment Inventory

Goal: `adapt`

Primary references:

- `old_2/project/user/main.cpp`
- `old_2/project/code/isr.cpp`
- `old_2/project/code/camera.h`
- `new/code/runtime/control_loop.cpp`
- `new/user/main.cpp`
- `new/code/runtime/runtime_state.hpp`
- `new/code/legacy/pid_control.*`
- `new/code/legacy/attitude_logic.*`

Extracted contracts from the reference system:

- `old_2/project/user/main.cpp` owns start intent, state reset, and handoff into a start phase before the car is considered "running".
- `old_2/project/code/isr.cpp` treats `run_stop`, `run_start`, `run_brushless_start`, and `start_over` as control semantics, not UI decoration.
- `old_2/project/code/isr.cpp` separates motion shaping concerns:
  - start ramp / accel timing
  - speed scheduling after startup
  - stop semantics
  - fault-like interruption paths
- `old_2/project/code/camera.h` exposes the state-table vocabulary that made those lifecycle phases explicit.
- `new/code/runtime/control_loop.cpp` currently combines "gate is clear" and "vehicle may drive" into one branch.
- `new/user/main.cpp` currently combines "request stop" and "terminate process" into one branch.

### Alignment Mapping

| Reference Module | Target Module | Action | Notes |
|---|---|---|---|
| `old_2/project/user/main.cpp` start/reset handoff | `new/user/main.cpp` + `new/code/runtime/runtime_state.hpp` | Adapt | Keep product-owned start/stop intent, but do not reintroduce board-key UI semantics into mainline runtime contracts |
| `old_2/project/code/isr.cpp` start/stop lifecycle | `new/code/runtime/motion_supervisor.*` | Adapt | Preserve explicit motion phases, not old BLDC-specific implementation |
| `old_2/project/code/isr.cpp` startup ramp and speed scheduling | `new/code/runtime/motion_supervisor.*` + `new/code/runtime/control_loop.cpp` | Adapt | Replace `temp_pwm`/`run_brushless_start` with phase-aware speed target and command shaping |
| `old_2/project/code/camera.h` lifecycle vocabulary | `new/code/runtime/motion_types.hpp` | Adapt | Keep explicit phase naming in project-owned types |
| `new/code/runtime/control_loop.cpp` gate/effectors path | `new/code/runtime/control_loop.cpp` + `new/code/runtime/control_decision.*` | Adapt | Split safety gate from motion-lifecycle decision without leaking vendor semantics |
| `new/user/main.cpp` bounded-loop exit | `new/user/main.cpp` + `new/user/run_remote_smoke.sh` | Adapt | Keep bounded automation as test harness only; product semantics are controlled stop before exit |

### Alignment Coverage

- ✅ Explicit start/stop/fail-safe lifecycle phases can be carried into project-owned runtime types.
- ✅ Phase-aware shaping can be implemented using current left/right PWM contract without restoring a separate servo or BLDC subsystem.
- ⚠️ `old_2` uses board key/switch state as direct operator input; `new/` should adapt that into generic motion intent instead of replicating hardware UI assumptions.
- ⚠️ `old_2` start phases are tied to BLDC-specific ramping; `new/` must translate that to differential-drive output shaping because the accepted public actuator contract is only `left_pwm/right_pwm/emergency_stop`.
- ❌ Current `new/` runtime has no explicit motion-lifecycle layer, no hold-disarmed apply outcome, and no latched fail-safe re-arm contract.

Unresolved alignment risks before implementation:

- If motion lifecycle is implemented inside adapters or inside `LegacyPidControl`, the Phase B contract will become vendor-coupled or controller-coupled and later semantic extensions will be harder to verify.
- If bounded automation remains embedded as a permanent product semantic in `main.cpp`, Phase B verification helpers will pollute long-lived runtime behavior.

## Goals / Non-Goals

**Goals:**

- Add a project-owned motion lifecycle that cleanly separates process lifecycle, safety gate, motion intent, and actuator output policy.
- Make Phase B start, spinup, running, stop, fail-safe latch, and re-arm behavior explicit and reviewable.
- Preserve the current differential-drive public contract and vendor isolation boundaries.
- Keep `control.veto.*` reserved for real safety blockers while adding separate observability for intentionally held-disarmed motion states.
- Support bounded Phase B fault injection and evidence capture without adding a second product runtime entrypoint.
- Update Phase B docs and smoke-wrapper behavior so the phase document is implementable as written.

**Non-Goals:**

- Reintroduce `old_2` key/switch menus, board UI state, or BLDC-specific lifecycle names as first-class product contracts.
- Add a new actuator topology, servo path, or vendor-leaking runtime surface.
- Replace the existing `legacy` PID or camera logic with a new controller architecture.
- Make auto-start/auto-stop/fault-reset mandatory long-term product behavior; those remain optional test harness capabilities only.
- Solve later Phase C semantic-speed scheduling beyond what Phase B needs for low-speed start, hold, stop, and recoverability.

## Decisions

### Decision: Add a Dedicated Motion Supervisor Between Safety Gate And Control Law

Problem:
Phase B needs the runtime to decide not only whether the system is safe, but also whether the vehicle is in a start, spinup, running, stop, or post-fault recovery phase. The current `ControlLoop::Tick()` only has a gate branch and a drive branch.

Alternatives considered:

- Option A: Keep lifecycle logic inside `ControlLoop::Tick()` by adding more booleans and branches.
  - Strengths: fewer files, lower immediate edit count.
  - Weaknesses: safety gate, motion lifecycle, and control law stay entangled; reset rules and evidence markers become harder to audit.
  - Migration cost: low initial cost, high long-term complexity.
  - Verification impact: poor; hard to distinguish whether a bad outcome is a gate bug, lifecycle bug, or controller bug.
- Option B: Move lifecycle logic into platform adapters.
  - Strengths: could localize some device-specific transitions.
  - Weaknesses: violates project-owned contract boundary and leaks motion semantics into vendor-facing code.
  - Migration cost: medium.
  - Verification impact: poor; lifecycle behavior becomes adapter-specific and harder to reason about.
- Option C: Add `motion_types.hpp` and `motion_supervisor.hpp/.cpp` in `new/code/runtime/`.
  - Strengths: keeps lifecycle project-owned, testable, and separate from PID and adapters.
  - Weaknesses: adds one more runtime layer and shared-state surface.
  - Migration cost: medium.
  - Verification impact: strong; motion-phase transitions can be inspected directly.

Chosen approach:
Option C. The runtime will evaluate safety gate first, then motion supervisor, then control law/application. This preserves the current `startup -> control_loop -> perception -> shutdown` ownership while introducing a project-owned lifecycle layer.

Stack Equivalent:

- abstract intention "Phase B state machine" -> `MotionSupervisor::Evaluate(...)`
- lifecycle vocabulary -> `enum class MotionPhase`
- operator/start/stop intent -> `MotionIntent`
- lifecycle output policy -> `MotionDecision`

Named Deliverables:

- `new/code/runtime/motion_types.hpp`
- `new/code/runtime/motion_supervisor.hpp`
- `new/code/runtime/motion_supervisor.cpp`
- `new/code/runtime/runtime_state.hpp` updates
- `new/code/runtime/control_loop.cpp` integration points

Failure Semantics:

- invalid gate input: motion supervisor returns hold-disarmed or fail-safe-latched output, never "best effort drive"
- invalid phase transition request: stay in current safe phase and emit project-owned diagnostics
- missing supervisor integration: change is incomplete and Phase B verification remains blocked

Boundary Examples:

- `startup_complete=true` and `gate.veto_active=false` does not imply `RUNNING`
- `motion_intent.start_requested=true` and `gate.veto_active=true` yields `START_REQUESTED` plus `motion.start.blocked`, not drive application
- `RUNNING` plus low voltage transition yields `FAIL_SAFE_LATCHED`

Contrast Structure:

- chosen: dedicated runtime layer
- not chosen: bigger `Tick()` branch tree or adapter-owned lifecycle

Verification Hook:

- `motion.phase.transition`
- `motion.start.blocked`
- `motion.failsafe.latched`
- design/code review must be able to identify a single owner for phase transitions

### Decision: Keep Safety Gate Semantics And Motion Hold-Disarmed Semantics On Separate Axes

Problem:
Phase B needs to distinguish "unsafe, must fail-safe" from "safe, but not yet allowed to drive". Today the runtime tends to collapse both into zero/empty commands or emergency-stop-like behavior.

Alternatives considered:

- Option A: reuse `control.veto` for both not-started and fault-stopped states.
  - Strengths: minimal new concepts.
  - Weaknesses: destroys the meaning of `control.veto.*`; logs cannot explain whether the car was blocked by safety or by lifecycle policy.
  - Migration cost: low.
  - Verification impact: unacceptable for Phase B.
- Option B: keep `control.veto.*` only for safety blockers and add `hold_disarmed` plus a new apply outcome.
  - Strengths: preserves existing safety evidence semantics and makes pre-start idle behavior reviewable.
  - Weaknesses: requires observation and diagnostics changes.
  - Migration cost: medium.
  - Verification impact: strong.

Chosen approach:
Option B. Safety gate remains authoritative for fail-safe causes; motion supervisor adds a separate hold-disarmed decision for states where the gate is clear but the vehicle should not yet drive.

Stack Equivalent:

- abstract "not started yet" -> `MotionDecision.hold_disarmed`
- abstract "true fail-safe" -> `ControlGateDecision.veto_active` plus `MotionPhase::kFailSafeLatched` when appropriate
- apply-path distinction -> `ControlApplyOutcome::kHeldDisarmed`

Named Deliverables:

- `new/code/runtime/control_decision.hpp/.cpp` updates
- `new/code/runtime/control_loop.cpp` apply-path split
- `new/code/runtime/runtime_state.hpp` published decision state

Failure Semantics:

- gate veto active: apply emergency stop / fail-safe path
- gate clear + hold-disarmed: disable outputs without claiming fail-safe
- diagnostics-only motor profile: still suppress outputs, but preserve phase/gate explanation

Boundary Examples:

- `START_REQUESTED` while waiting for clean gate cycles -> `control.apply.hold_disarmed`
- `DISARMED` with no start request -> `control.apply.hold_disarmed`
- `RUNNING` with perception stale -> `control.apply.emergency_stop`

Contrast Structure:

- chosen: two-axis model (`gate` + `motion`)
- not chosen: overloading `control.veto` to mean "anything that prevents movement"

Verification Hook:

- logs must show both `control.veto.*` and `control.apply.hold_disarmed`
- `B-1/B-3/B-5` evidence must distinguish "not started" from "fault stopped"

### Decision: Implement Phase-Aware Speed/Turn Shaping In Runtime, Not In PID Or Vendor Bridges

Problem:
Phase B needs smooth startup and stop behavior. The current runtime jumps directly from zero output to full `Speed_base`-derived controller demand when the gate clears. `old_2` solved this with lifecycle-specific ramping, but its mechanism is tied to BLDC-specific variables.

Alternatives considered:

- Option A: recreate `temp_pwm` / `run_brushless_start` literally.
  - Strengths: close to the old code.
  - Weaknesses: copies old hardware assumptions that the new public actuator contract does not expose.
  - Migration cost: medium.
  - Verification impact: misleading because the contract shape changed.
- Option B: ramp only `Speed_base`.
  - Strengths: simple.
  - Weaknesses: turn demand can still jump abruptly.
  - Migration cost: low.
  - Verification impact: partial only.
- Option C: use supervisor-owned effective target plus command shaping:
  - ramp effective speed target
  - limit turn authority during spinup
  - limit per-cycle PWM deltas after mix
  - reset controller state on phase transitions
  - Strengths: fits current actuator contract and keeps smoothing project-owned.
  - Weaknesses: more moving parts.
  - Migration cost: medium.
  - Verification impact: strong.

Chosen approach:
Option C.

Stack Equivalent:

- start ramp -> `MotionDecision.effective_speed_target`
- spinup turn clamp -> `MotionDecision.effective_turn_limit`
- command slew limiting -> runtime-owned shaping helper in `control_loop.cpp` or `motion_supervisor.cpp`
- re-entry cleanup -> `LegacyPidControl::Reset()` and `LegacyAttitudeLogic::Reset()`

Named Deliverables:

- `new/code/port/control_types.hpp` parameter additions
- `new/code/platform/param_store.cpp` optional parse additions
- `new/config/default_params.json` new motion parameters
- `new/code/legacy/pid_control.*` reset support
- `new/code/legacy/attitude_logic.*` reset support
- control-loop shaping helper implementation

Failure Semantics:

- invalid or missing optional motion parameters: use documented defaults, not silent undefined behavior
- reset not triggered on phase boundary: block Phase B acceptance because restart behavior becomes non-repeatable
- slew limit too tight or too loose: Phase B remains open; adjust parameters, do not bypass shaping

Boundary Examples:

- `SPINUP`: `effective_speed_target` ramps from 0 toward `Speed_base`
- `SPINUP`: `turn_output` is clamped below full running authority
- `STOPPING`: command deltas collapse toward zero without a step jump

Contrast Structure:

- chosen: project-owned shaping after gate and before apply
- not chosen: PID-only smoothing or vendor-bridge special cases

Verification Hook:

- `motion.spinup.enter`
- `motion.spinup.complete`
- `motion.stop.complete`
- Phase B half-load and straight-run evidence must show non-step start behavior

### Decision: Keep Product Stop Semantics In `main.cpp`, But Move Automation Policy To Test Harnesses

Problem:
`main.cpp` currently treats signal/frame-limit as immediate loop exit. Phase B needs controlled stop-before-exit, but the project does not want long-term product behavior polluted by smoke-only automation.

Alternatives considered:

- Option A: keep current immediate-exit behavior.
  - Strengths: simple.
  - Weaknesses: loses real stop evidence and can bypass lifecycle semantics.
  - Migration cost: none.
  - Verification impact: weak.
- Option B: embed auto-start/auto-stop as permanent runtime semantics.
  - Strengths: convenient for repeatable board tests.
  - Weaknesses: confuses product behavior with test behavior.
  - Migration cost: medium.
  - Verification impact: medium, but product semantics become dirty.
- Option C: product runtime only guarantees "request stop -> wait for `DISARMED` -> exit", while wrapper/env automation remains optional and removable.
  - Strengths: clean boundary between product and test harness.
  - Weaknesses: wrapper must explicitly manage bounded runs.
  - Migration cost: medium.
  - Verification impact: strong.

Chosen approach:
Option C.

Stack Equivalent:

- product exit contract -> `motion_intent.stop_requested` vs `runtime_state.stop_requested`
- smoke-only bounded run -> env injection plus harness-owned bounded frame-limit policy from `run_remote_smoke.sh`
- optional automation -> harness policy, not runtime invariant

Named Deliverables:

- `new/user/main.cpp` runtime loop changes for product-owned stop-before-exit behavior
- `new/user/run_remote_smoke.sh` env pass-through/automation shaping and remote-exit verdict preservation
- Phase B docs clarifying test-only automation

Failure Semantics:

- stop signal arrives: request controlled stop first
- bounded frame limit or smoke-only auto-stop expires: harness requests controlled stop and waits for `DISARMED` instead of treating the limit itself as a product-runtime exit condition
- stop does not complete within bounded harness timeout: wrapper returns failure and log evidence remains authoritative
- remote runtime exits non-zero: wrapper still retrieves logs when possible, then returns the same failing verdict locally instead of masking it with shell success
- automation env absent: product runtime still functions without auto-start/auto-stop

Boundary Examples:

- product path: signal -> `motion.stop.requested` -> `motion.stop.complete` -> process exit
- test path: wrapper injects bounded-run env for evidence capture without redefining long-term runtime semantics
- harness-owned frame limit path: bounded-run budget expires -> harness requests controlled stop -> runtime reaches `DISARMED` -> wrapper decides success/failure from runtime outcome plus timeout status

Contrast Structure:

- chosen: thin product stop contract + wrapper-owned automation
- not chosen: product runtime permanently owns board-smoke choreography

Verification Hook:

- `motion.stop.requested`
- `motion.stop.complete`
- wrapper logs show bounded automation inputs separately from runtime markers
- smoke verdict preserves the remote runtime exit code after log retrieval

### Decision: Fix Transition Guards In The Spec Instead Of Leaving Them As Tuning-Time Interpretation

Problem:
Phase B needs repeatable start, stop, and post-fault recovery rules. If transition guards are left as tuning-time interpretation, multiple materially different motion supervisors would satisfy the artifact bundle while producing different real-vehicle behavior.

Alternatives considered:

- Option A: leave guard semantics implicit in design prose and tuning notes.
  - Strengths: fewer hard requirements.
  - Weaknesses: spec does not constrain when movement may begin or when a stop/re-arm is complete.
  - Migration cost: low.
  - Verification impact: unacceptable.
- Option B: define normative guard semantics in the `phase-b-motion-lifecycle` spec and mirror them in design/tasks.
  - Strengths: implementation and evidence targets become deterministic.
  - Weaknesses: reduces freedom for alternate lifecycle implementations.
  - Migration cost: low-medium.
  - Verification impact: strong.

Chosen approach:
Option B.

Normative guard choices:

- `START_REQUESTED -> SPINUP`
  - requires `motion_intent.start_requested=true`
  - requires the safety gate to remain clear for `motion_unveto_confirm_cycles` consecutive control cycles
  - if the gate blocks during this window, stay in `START_REQUESTED` and emit blocked-start diagnostics rather than beginning spinup
- `SPINUP -> RUNNING`
  - requires elapsed spinup time to satisfy `motion_spinup_ms`
  - requires the lifecycle to have ramped effective speed authority from the spinup floor to the configured running target envelope
  - requires turn/output shaping to switch from spinup limits to running limits only after that timer boundary completes
- `STOPPING -> DISARMED`
  - requires elapsed stop time to satisfy `motion_stop_ms`
  - requires the shaped drive command to have returned to zero
  - requires the absolute mean encoder speed to be at or below `motion_stop_encoder_threshold`
- `FAIL_SAFE_LATCHED -> DISARMED`
  - requires the safety gate to have returned clear
  - requires `motion_fault_rearm_hold_ms` to elapse
  - requires an explicit baseline re-arm trigger (`reset_fault_requested`)
  - test harness automation may synthesize that trigger only when `LS2K_AUTO_RESET_FAULT=1` is explicitly enabled

Stack Equivalent:

- clean gate confirmation -> `motion_unveto_confirm_cycles`
- spinup completion timer -> `motion_spinup_ms`
- spinup turn clamp -> `motion_turn_limit_spinup`
- per-cycle command delta clamp -> `motion_pwm_step_limit`
- stop completion threshold -> `motion_stop_ms` + `motion_stop_encoder_threshold`
- post-fault baseline re-arm -> `MotionIntent.reset_fault_requested`
- test-only auto-reset override -> harness-owned env interpreted by `main.cpp` or runtime harness glue

Named Deliverables:

- updated `specs/phase-b-motion-lifecycle/spec.md`
- aligned `design.md` transition semantics
- unchanged-but-now-constrained `tasks.md` parameter list

Failure Semantics:

- blocked start: remain in `START_REQUESTED`, do not leak into `SPINUP`
- incomplete stop: remain in `STOPPING`
- uncleared or unreset fault: remain in `FAIL_SAFE_LATCHED`

Boundary Examples:

- gate clear for only `N-1` cycles -> still `START_REQUESTED`
- spinup timer not yet elapsed -> still `SPINUP`
- spinup timer elapsed -> enter `RUNNING` and release spinup-specific turn/output clamps
- zero command but encoder mean above threshold -> still `STOPPING`
- gate clear after fault but no reset request -> still `FAIL_SAFE_LATCHED`

Contrast Structure:

- chosen: explicit spec-level transition guards
- not chosen: post-hoc tuning interpretation

Verification Hook:

- `motion.start.blocked`
- `motion.spinup.enter`
- `motion.stop.complete`
- `motion.failsafe.reset_ready`

### Decision: Provide Whitelist-Based Fault Injection At Runtime/Adapter Boundaries

Problem:
Phase B B-5 requires repeatable drop-frame, IMU invalid, encoder invalid, and low-voltage exercises. The project already has `LS2K_FORCE_LOW_VOLTAGE`, but the other fault paths are not injectable in a controlled, reviewable way.

Alternatives considered:

- Option A: add a separate test executable.
  - Strengths: isolates test logic.
  - Weaknesses: creates a second runtime path that Phase B docs do not authorize.
  - Migration cost: medium-high.
  - Verification impact: poor because product path and test path diverge.
- Option B: add bounded env hooks to the existing runtime/adapter boundaries.
  - Strengths: keeps a single runtime entrypoint and explicit fault controls.
  - Weaknesses: requires careful whitelisting so test hooks do not sprawl.
  - Migration cost: medium.
  - Verification impact: strong.

Chosen approach:
Option B.

Stack Equivalent:

- drop-frame injection -> `PerceptionFrontend`
- IMU invalid injection -> `ImuAdapter::Read`
- encoder invalid injection -> `EncoderAdapter::ReadDelta`
- low-voltage injection -> existing `PowerMonitorAdapter::SampleLowVoltage`

Named Deliverables:

- `new/code/runtime/perception_frontend.cpp`
- `new/code/platform/imu_adapter.cpp`
- `new/code/platform/encoder_adapter.cpp`
- `new/user/run_remote_smoke.sh` env pass-through updates
- Phase B docs and verification instructions

Failure Semantics:

- unsupported env value: ignore safely and emit diagnosable warning or stay on normal path
- injected invalid sample: runtime returns to fail-safe/latched path instead of continuing to drive
- fault recovery automation disabled: operator/manual reset remains required

Boundary Examples:

- `LS2K_FAULT_INJECT_DROP_FRAME_EVERY_N=5`
- `LS2K_FAULT_INJECT_IMU_INVALID_EVERY_N=10`
- `LS2K_FAULT_INJECT_ENCODER_INVALID_EVERY_N=7`
- `LS2K_FORCE_LOW_VOLTAGE=true`

Contrast Structure:

- chosen: bounded hooks in the existing runtime boundary
- not chosen: dedicated alternate runtime binary

Verification Hook:

- `control.veto.*`
- `motion.failsafe.latched`
- `motion.failsafe.reset_ready`
- `startup.low_voltage.force`

## Independent Verification Plan (STANDARD/STRICT)

This change uses the shared sequence `verify-sequence/default` from `openspec/schemas/ai-enforced-workflow/verification-sequence.md`, the shared JSON verification contract `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`, and the OpenSpec subject adapter `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`.

Stage A flow:

- checkpoints use the same `active/non_active/closed` verification cycle
- docs-first checkpoints review changed `proposal/specs/design/tasks` first
- source-first checkpoints later review changed runtime code, tests, and directly impacted files
- approved docs remain reference material for source-first review
- repository-index support is optional cache help only
- usable active agents are resumed first
- new agents are spawned only when no usable active agent exists
- termination depends on a valid active pass

Runtime profile policy:

- Use verifier runtime profile from `.codex/agents/verify-reviewer.toml`.
- Use index-maintainer runtime profile from `.codex/agents/index-maintainer.toml` only if cache refresh becomes useful.

Loop rule:

- An active agent that reports `block` remains authoritative until that same agent returns `pass`.
- Only `block -> pass` may mark an agent `non_active`.
- `close` or `exit` does not imply `non_active`.
- A pass is valid only when coverage is complete and exhaustive.
- Partial verification requires explicit scope.
- Only the main orchestrator may authorize resume, spawn, repair, or termination, and it must not substitute its own judgment for verifier output.

## Repository Index Cache Plan (When Useful)

- Index contract id: `repo-index-v1`
- Canonical repository-index root: `review/repository-index/`
- Shared cache-helper sequence: `index-sequence/default`
- Optional refresh scoping hints: restrict to `new/code/`, `new/user/`, `new/docs/race-finish-series.zh-CN/`, and the active change directory
- Fallback policy (`refresh|bypass`): `bypass`
- Verifier invocation template: `verify-reviewer-inline-v3`
- Index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- Index skill entry points:
  - `openspec-index-preflight`
  - `openspec-index-maintain`
- Cache-helper evidence path convention: `openspec/changes/implement-phase-b-motion-lifecycle/verification/index/<checkpoint>/preflight.json`
- Findings path convention: `openspec/changes/implement-phase-b-motion-lifecycle/verification/<checkpoint>/<pass>/findings.json`
- Verifier execution evidence path convention: `openspec/changes/implement-phase-b-motion-lifecycle/verification/<checkpoint>/<pass>/verifier-evidence.json`
- Cache handoff fields:
  - `index_context.contract`
  - `index_context.manifest_path`
  - `index_context.manifest_present`
  - `index_context.preflight_report_path`
  - `index_context.cache_mode`
  - `index_context.fallback_policy`

Shared field groups from `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`:

- `invocation_common_required`
- `output_paths_required`
- `verifier_evidence_required`
- `valid_pass_requirements`
- `partial_scope_rule`

Review completion contract:

- execution evidence MUST record:
  - `review_goal`
  - `review_phase`
  - `review_scope`
  - `review_coverage`
  - `reviewed_paths`
  - `skipped_paths`
  - `reviewed_axes`
  - `unreviewed_axes`
  - optional `early_stop_reason`
  - optional `skip_reasons`
- each review checkpoint MUST additionally maintain `agent-table.json`

### Review Checkpoints

- Shared sequence reference: `verify-sequence/default`
- Cache-helper sequence reference: `index-sequence/default`
- Review goal: `implementation_correctness`
- Verifier agent path: `.codex/agents/verify-reviewer.toml`
- Index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- Invocation template id: `verify-reviewer-inline-v3`
- Default loop behavior:
  - resume usable `active` first
  - spawn when no usable `active` agent exists
  - only `block -> pass` marks `non_active`
  - final termination requires a valid `active` pass
- Authoritative verifier-subagent findings JSON path: `openspec/changes/implement-phase-b-motion-lifecycle/verification/artifact-completion/<pass>/findings.json`
- Verifier execution evidence JSON path: `openspec/changes/implement-phase-b-motion-lifecycle/verification/artifact-completion/<pass>/verifier-evidence.json`
- Agent table path: `openspec/changes/implement-phase-b-motion-lifecycle/verification/artifact-completion/agent-table.json`
- Optional cache-helper report path: `openspec/changes/implement-phase-b-motion-lifecycle/verification/index/artifact-completion/preflight.json`
- Continuation target on pass: `openspec-apply-change`

Checkpoint-specific primary surfaces:

- artifact-completion docs-first review: changed `proposal/specs/design/tasks`
- active-change source-first review: changed code, changed tests, directly impacted code

### Optional Gemini Dual Review

Use only when repository or checkpoint policy explicitly enables dual review.

- Runner contract: `gemini-capture`
- Raw report path: `openspec/changes/implement-phase-b-motion-lifecycle/verification/<checkpoint>/<pass>/gemini-raw.json`
- Normalized report path: `openspec/changes/implement-phase-b-motion-lifecycle/verification/<checkpoint>/<pass>/gemini-report.json`
- Maximum attempts: `2`
- Recovery behavior:
  - `input_raw_path -> report_path`
- Resolved command goes in execution evidence, not workflow policy prose

## Migration Plan

1. Add project-owned motion types and supervisor implementation without changing adapter boundaries.
2. Extend runtime parameters/state and add controller reset support.
3. Integrate gate -> motion decision -> shaping -> apply sequencing in `ControlLoop::Tick()`.
4. Update `main.cpp` to split stop request from process exit while keeping automation optional.
5. Add bounded fault-injection hooks and wrapper pass-through.
6. Update Phase B docs, verification commands, and expected evidence markers.
7. Run docs-first artifact review, then implement source changes, then run source-first verification and Phase B board evidence capture.

Rollback posture:

- If the new motion lifecycle prevents clean startup or stop behavior, revert to Phase A-safe behavior by holding actuators disarmed and blocking Phase B claims.
- If fault-injection hooks or wrapper automation create ambiguity, remove the hook from product runtime semantics and keep the change blocked until the harness boundary is clean again.

## Open Questions

- Do we need a dedicated `motion.start.ready` marker in addition to `motion.spinup.enter`, or is the latter sufficient once `START_REQUESTED` blocked-start diagnostics are in place?
- Should Phase B straight-run and turn-run evidence notes record parameter snapshots inline in each note, or may they reference a shared Phase B parameter manifest as long as the manifest version is fixed in the note?

## Risks / Trade-offs

- Adding a new runtime layer increases code volume, but keeping lifecycle logic inside `Tick()` would make Phase B safety reasoning much harder.
- Reusing the existing runtime entrypoint keeps verification honest, but bounded env hooks must stay tightly scoped so they do not become an accidental second configuration system.
- Preserving `control.veto.*` semantics limits how much lifecycle state can be smuggled into old diagnostics, but that constraint is valuable because it keeps safety evidence intelligible.
- Phase-aware shaping and controller reset improve repeatability, but they introduce new parameters that will require board tuning and explicit documentation.
