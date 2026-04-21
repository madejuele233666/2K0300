## Context

`new/` 当前已经有三条清晰边界：

- `new/code/platform/true_ls2k0300/*` 持有 vendor 头文件、TCP free function 和设备节点。
- `new/code/runtime/*` 持有生命周期、控制循环、前台 sidecar 服务和共享运行时状态。
- `new/code/legacy/*` 持有保留算法语义，但不直接拥有 transport、文件 I/O 或 host workflow。

本变更需要把现有“只读 assistant sidecar + 双轮速度观测”推进成“可做现场速度调参”的工程面，但不能破坏既有 Phase B 主线：

1. `assistant_bridge` 目前已经能收 TCP 字节，但入站流量会被 drain 并标记 ignored。
2. `control_loop` 当前从 lifecycle-owned `running_speed_target` 生成 wheel targets，再计算左右轮 PWM。
3. `motion_supervisor` 已经是唯一允许发车、停车、fail-safe latch 和 re-arm 的生命周期 owner。
4. `old_2/project/code/save.cpp` 与 `old_2/project/code/key.cpp` 证明“现场调参必须快”，但旧的 `settings.txt` 持久化和按键菜单并不是 `new/` 应直接复制的 runtime contract。

### Reference Inventory

Primary implementation references:

- `new/code/platform/assistant_link.hpp`
- `new/code/platform/assistant_link.cpp`
- `new/code/platform/true_ls2k0300/assistant_bridge.hpp`
- `new/code/platform/true_ls2k0300/assistant_bridge.cpp`
- `new/code/runtime/assistant_service.hpp`
- `new/code/runtime/assistant_service.cpp`
- `new/code/runtime/control_loop.hpp`
- `new/code/runtime/control_loop.cpp`
- `new/code/runtime/motion_supervisor.hpp`
- `new/code/runtime/motion_supervisor.cpp`
- `new/code/runtime/runtime_state.hpp`
- `new/code/runtime/control_debug_snapshot.hpp`
- `new/code/legacy/wheel_target_mixer.hpp`
- `new/code/legacy/wheel_target_mixer.cpp`
- `new/user/debug.sh`

Workflow reference inputs that inform, but do not define, the new contract:

- `old_2/project/code/motor.cpp`
- `old_2/project/code/save.cpp`
- `old_2/project/code/key.cpp`
- `new/docs/race-finish-series.zh-CN/04-phase-d-track-operations-and-tuning.md`

### Alignment Mapping

| Reference Module | Target Module | Action | Notes |
|---|---|---|---|
| `new/code/platform/true_ls2k0300/assistant_bridge.*` | `new/code/platform/true_ls2k0300/assistant_bridge.*` + new command-transport helpers | Adapt | Keep TCP connect/send/recv ownership in the bridge, but stop treating all receive bytes as ignorable. |
| `new/code/platform/assistant_link.*` | `new/code/platform/assistant_link.*` + project-owned command ingress boundary | Adapt | Preserve platform-side wrapper ownership while adding typed command polling instead of leaking raw socket parsing upward. |
| `new/code/runtime/assistant_service.*` | `new/code/runtime/assistant_service.*` | Adapt | Keep assistant work in the foreground loop, extend it to consume command events and publish structured ACK/state telemetry. |
| `new/code/runtime/control_loop.*` | `new/code/runtime/control_loop.*` + new tuning-state snapshot read | Adapt | Preserve control-loop ownership of command generation; inject only a runtime-owned speed/tuning snapshot, never raw TCP state. |
| `new/code/runtime/motion_supervisor.*` | `new/code/runtime/motion_supervisor.*` | Adapt | Keep lifecycle ownership here; remote start/stop still becomes motion intent rather than bypassing phases. |
| `new/code/runtime/runtime_state.hpp` | `new/code/runtime/runtime_state.hpp` + `runtime/tuning_state.*` | Adapt | Add a distinct runtime tuning state instead of mutating startup-loaded params in place. |
| `old_2/project/code/motor.cpp` | `new/code/legacy/*` and host tuning workflow | Hybrid | Reuse the idea of repeated target-speed tuning runs, but do not reuse old TCP snippets, globals, or PID state layout. |
| `old_2/project/code/save.cpp` + `key.cpp` | host-side tune script + existing param files | Adapt | Reuse only the “fast tuning workflow” goal; keep persistence on JSON params and host tooling rather than onboard menu state. |

### Alignment Coverage

- ✅ Assistant TCP transport ownership stays in the existing bridge boundary.
- ✅ Lifecycle ownership stays in `motion_supervisor`.
- ✅ Wheel-target and PWM ownership stays in `control_loop`.
- ⚠️ Existing assistant contract says first release is read-only; this change must explicitly supersede that spec area with a scoped bidirectional command contract.
- ⚠️ Existing observability channels expose wheel data, but not tuning-mode state, raw-vs-applied turn, ACK/state feedback, or host-oriented structured telemetry.
- ❌ There is no current runtime-owned tuning state or host-side live plotting workflow; both must be introduced as new deliverables.

## Goals / Non-Goals

**Goals:**

- Introduce a bidirectional assistant command surface that remains project-owned above the TCP bridge.
- Add runtime-owned target-speed override and tuning-mode state that stays separate from `default_params.json`.
- Allow remote `start` / `stop` requests without bypassing the accepted Phase B lifecycle.
- Add a tuning-only turn-suppression mechanism that zeros only the applied turn output while preserving raw turn computation for observability.
- Publish enough structured telemetry for a minimal Python + `matplotlib` host tuning tool to plot left/right target-vs-measured speed curves and save CSV evidence.
- Preserve normal-run behavior when tuning mode is disabled, the host is absent, or the assistant link disconnects.

**Non-Goals:**

- No online hot-write path for PID parameters in the first release; PID updates still go through parameter files plus restart.
- No new startup-critical assistant subsystem.
- No change to the public `ActuatorCommand` contract.
- No reuse of `old_2` `settings.txt`, TFT menus, or old global-variable runtime model.
- No attempt to auto-tune PID gains on-device.
- No requirement that the official Seekfree UI consume the new structured telemetry directly; the project-owned host tool may run alongside the TCP sidecar contract.

## Decisions

### Decision: Keep TCP Transport Ownership In The Existing Bridge, But Add A Separate Project-Owned Command Layer

**Problem being solved**

The current assistant bridge already owns TCP connection state, send, and receive, but it collapses all inbound traffic into an “ignored” bucket. If command parsing is bolted directly into `control_loop` or `assistant_service`, transport semantics and control semantics will become inseparable.

**Alternatives considered**

1. Parse JSON commands directly in `control_loop`.
2. Extend `assistant_bridge` until it owns both sockets and business-level command semantics.
3. Keep `assistant_bridge` as the TCP owner and add a project-owned command boundary above it that produces typed commands and typed status.

**Why this option was chosen**

Option 3 preserves the current layering:

- `assistant_bridge` continues to own connect/poll/read/write and vendor bindings.
- New command parsing lives in a project-owned module that can decode JSON lines, use `seq` for request correlation, honor override TTL, and emit typed commands.
- `runtime` only consumes typed command results and never sees raw socket buffers.

**Stack Equivalent**

- Transport owner: `platform/true_ls2k0300/assistant_bridge.*`
- Project-owned command ingress: `platform/assistant_link.*` plus new command parsing helper(s)
- Runtime consumer: `runtime/assistant_service.*`

**Named Deliverables**

- Project-owned inbound message buffer and JSON-line decoder
- Typed command model for `start`, `stop`, tuning mode, turn suppression, and target-speed override
- Typed ACK/state feedback model
- Updated assistant link API that exposes command polling separately from waveform/image publication

**Failure Semantics**

- Malformed JSON or unsupported command: reject with reason string; do not mutate runtime state.
- TCP disconnect: transport recovers per existing reconnect behavior; command layer clears tuning state through runtime policy.

**Boundary Examples**

- Allowed: bridge returns raw received byte chunks to a project-owned parser.
- Allowed: parser returns `SetTargetSpeed{value, ttl_ms, seq}`.
- Forbidden: `control_loop` performing line-buffer management or JSON parsing.
- Forbidden: vendor bridge writing directly into `RuntimeState`.

**Contrast Structure**

- Chosen: `transport -> decoder -> typed command`.
- Not chosen: `transport + parser + runtime mutation` in one module.

**Verification Hook**

- Source review verifies raw socket/buffer handling stays below the command boundary.
- Docs/spec review verifies malformed inbound traffic cannot directly alter lifecycle or params.

### Decision: Store Live Tuning State In A Dedicated Runtime Boundary, Not In Startup-Loaded Params

**Problem being solved**

The runtime needs temporary target-speed overrides, request-correlation bookkeeping, and tuning-only flags. Reusing `RuntimeParameters` or rewriting `default_params.json` during a run would blur static config with ephemeral control intent and make rollback behavior hard to reason about.

**Alternatives considered**

1. Mutate `RuntimeParameters::Speed_base` in place at runtime.
2. Write target-speed changes back to the JSON params file.
3. Introduce a distinct runtime tuning state snapshot that expires and clears independently of startup-loaded params.

**Why this option was chosen**

Option 3 keeps static and dynamic concerns separate:

- Static JSON params remain the authoritative persisted PID and baseline speed surface.
- Runtime overrides are explicit, volatile, and timeout-aware.
- Disconnect or explicit tuning-mode exit can clear the tuning state without touching persisted config.

**Stack Equivalent**

- Static configuration: `port::RuntimeParameters`
- Volatile operator/tuning state: new `runtime/tuning_state.*`
- Shared read model: tuning snapshot copied inside `control_loop` and assistant status reporting

**Named Deliverables**

- `RuntimeTuningState` or equivalent project-owned state type
- Snapshot API with override TTL handling and optional session-local `seq` bookkeeping
- Explicit clear paths on disconnect, timeout, and tuning-mode disable

**Failure Semantics**

- Expired target-speed override: clear override and revert to static `Speed_base`.
- Disconnect while tuning: clear the full runtime tuning snapshot, including target-speed override, tuning-mode enablement, and turn-suppression state, then emit marker/state feedback.
- Accepted tuning-run stop: preserves the tuning snapshot until a later explicit `disable_tuning_mode` or disconnect clears it.
- Invalid override value: reject without altering active tuning snapshot.

**Boundary Examples**

- Allowed: `target_speed_override_value` with `override_expire_at_ms`.
- Allowed: `tuning_mode_enabled`, `suppress_turn_output`, and optional session-local `last_seq` bookkeeping.
- Forbidden: background writer mutating `params_.Speed_base`.
- Forbidden: command layer editing JSON files on behalf of runtime tuning.

**Contrast Structure**

- Chosen: runtime-owned volatile tuning snapshot.
- Not chosen: “temporary” edits to persisted params or shared config objects.

**Verification Hook**

- Spec/source review verifies PID params still come from JSON + restart.
- Runtime verification proves timeout/disconnect returns target speed to the static baseline.

### Decision: Remote Start/Stop Must Enter Through Motion Intent And Existing Lifecycle Guards

**Problem being solved**

Host tuning needs to start and stop runs repeatedly. A naive remote-control path could bypass `motion_supervisor` and directly arm or zero actuators, which would fracture the lifecycle contract.

**Alternatives considered**

1. Let remote commands drive actuators directly.
2. Add a parallel “assistant lifecycle” outside `motion_supervisor`.
3. Translate remote `start` and `stop` into the existing project-owned motion intent boundary.

**Why this option was chosen**

Option 3 preserves the accepted lifecycle semantics:

- `motion_supervisor` remains the only owner of `DISARMED/START_REQUESTED/SPINUP/RUNNING/STOPPING/FAIL_SAFE_LATCHED`.
- Remote commands become another operator-intent source, not a new lifecycle.
- Existing gate checks, stop completion rules, and fail-safe latch semantics remain authoritative.

**Stack Equivalent**

- Command ingress: runtime command handler
- Intent boundary: `RuntimeState.motion_intent`
- Lifecycle owner: `runtime/motion_supervisor.*`

**Named Deliverables**

- Mapping from remote `start`/`stop` command to motion intent writes
- Diagnostics that distinguish remote operator intent from process exit or harness stop logic
- Spec delta for remote operator intent semantics

**Failure Semantics**

- `start` while gate is blocked: stays in `START_REQUESTED` and emits blocked-start evidence.
- `stop` during drive: enters `STOPPING`, does not hard-cut lifecycle rules.
- Remote `start` or `stop` during `FAIL_SAFE_LATCHED`: rejected with reason, does not clear the latch, and does not change lifecycle phase.

**Boundary Examples**

- Allowed: `start` toggles `motion_intent.start_requested`.
- Allowed: `stop` toggles `motion_intent.stop_requested`.
- Forbidden: remote command directly setting `motion_state.phase`.
- Forbidden: remote command clearing fail-safe latch without the documented reset path.

**Contrast Structure**

- Chosen: remote operator intent feeds lifecycle.
- Not chosen: remote operator intent replaces lifecycle.

**Verification Hook**

- Source review verifies no command path writes lifecycle phase directly.
- Runtime logs show `motion.start.requested` / `motion.stop.requested` semantics preserved under remote control.

### Decision: Implement Tuning Mode As A Narrow Control Profile That Suppresses Only Applied Turn Output

**Problem being solved**

Straight-line wheel PID tuning needs turn output removed from the final wheel-target path, but the existing turn computation is still useful diagnostic context and should remain intact for normal runs.

**Alternatives considered**

1. Disable turn PID computation entirely during tuning.
2. Add special branches inside legacy turn algorithms.
3. Compute raw turn as usual, then apply a tuning-only clamp that forces the applied turn output to zero before wheel-target generation.

**Why this option was chosen**

Option 3 is the cleanest isolation point:

- Raw turn computation remains unchanged and observable.
- Tuning mode only changes the value that enters wheel-target mixing.
- Normal mode continues to use the same raw value without any special casing.

**Stack Equivalent**

- Raw turn producer: existing `pid_` + attitude path in `control_loop`
- Tuning gate: small project-owned control-profile branch inside `control_loop`
- Observability: debug snapshot records both raw and applied turn

**Named Deliverables**

- Tuning mode flag in runtime tuning state
- Applied-turn suppression branch in control-loop command generation
- Debug snapshot / telemetry fields for `raw_turn_output`, `applied_turn_output`, and tuning flags

**Failure Semantics**

- Tuning mode disabled: applied turn equals raw turn.
- Tuning mode enabled with suppression off: raw/applied turn remain equal but tuning flags still publish.
- Tuning mode enabled with suppression on: applied turn is zeroed, but raw turn remains available in telemetry.
- Disconnect or explicit tuning-mode disable after an accepted tuning run: applied-turn suppression is cleared before any subsequent normal run.

**Boundary Examples**

- Allowed: `raw_turn_output=-1200`, `applied_turn_output=0` during tuning.
- Forbidden: legacy turn PID short-circuited by transport or host state.
- Forbidden: tuning suppression persisting after disconnect or tuning-mode disable.

**Contrast Structure**

- Chosen: preserve raw computation, zero only applied turn.
- Not chosen: rewrite turn algorithm or hide raw-turn evidence.

**Verification Hook**

- Runtime telemetry proves raw/applied turn divergence only in tuning mode.
- Normal runs show identical raw/applied turn when tuning mode is off.

### Decision: Use One Fixed JSON-Line Host Session Contract For Commands, ACK/State, And Structured Telemetry

**Problem being solved**

The existing assistant waveform channel set is useful but too narrow for a robust host tuning workflow. PID tuning also needs ACK/state feedback and easy CSV capture. The previous draft left the wire-level topology unresolved, which would let implementation choose a framing model ad hoc and break host-tool interoperability.

**Alternatives considered**

1. Use assistant waveforms as the only tuning evidence surface.
2. Add all tuning data only to logs and require offline parsing.
3. Freeze a single project-owned newline-delimited JSON host session contract on the accepted assistant TCP connection for commands, ACK/state, and structured telemetry, while keeping assistant-compatible waveform/image publication as optional support evidence.

**Why this option was chosen**

Option 3 freezes one first-release host contract and keeps the sidecar useful without making vendor waveform slots the only tuning API:

- Commands, ACK/state, and structured telemetry all use the same newline-delimited JSON framing on the accepted TCP session.
- Assistant-compatible waveform/image support remains optional support evidence rather than the primary host-tool contract.
- The Python host tool can stay minimal: send JSON-line commands, receive JSON-line ACK/state/telemetry, plot curves, save CSV, and install `matplotlib` when missing.
- Malformed or unsupported inbound payloads use one frozen rejection model: standalone `state` with `event="input_rejected"` and a human-readable `reason`.

**Stack Equivalent**

- Accepted host session: one TCP connection using newline-delimited JSON command/feedback/telemetry frames
- Optional support evidence: existing assistant waveform/image publication
- Host consumer: new `new/user/tune_speed.py`

**Named Deliverables**

- Frozen first-release JSON-line schema for commands, ACK/state, and structured telemetry
- ACK/state publication path on the accepted host session
- Minimal Python tuning tool with CSV save and live plotting
- Host-side dependency bootstrap/install fallback for `matplotlib`

**First-Release Frame Contract**

All accepted host-facing frames use newline-delimited JSON on the accepted assistant TCP session.

Command frames:

- Common required fields:
  - `type=\"command\"`
  - `cmd`
  - `seq`
- `seq` semantics for first release:
  - used for request/ACK correlation only
  - does not freeze a standalone stale/duplicate/out-of-order rejection state machine
- `set_target_speed`:
  - required: `value`, `ttl_ms`
  - `value` validity for first release: JSON number, finite, `>= 0`, `<=` startup-loaded `Speed_base`
  - semantics: rejected when `tuning_mode_enabled=false`
  - invalid-value semantics: invalid `value` is rejected with `outcome="rejected"` and a `reason` string describing the violation; the active tuning snapshot is unchanged
- `set_turn_suppressed`:
  - required: `value`
  - semantics: rejected when `tuning_mode_enabled=false`
- `enable_tuning_mode`:
  - no extra required fields
- `disable_tuning_mode`:
  - no extra required fields
- `start`:
  - no extra required fields
- `stop`:
  - no extra required fields

ACK/state frames:

- Common fields:
  - `type` in `{\"ack\",\"state\"}`
- `ack` frames:
  - required: `seq`, `outcome`
  - accepted `outcome` family for first release:
    - `accepted`
    - `rejected`
  - `reason`: required when `outcome="rejected"`, a human-readable string describing the rejection cause
- `state` frames:
  - required:
    - `event`
    - `reason`
    - `tuning_mode_enabled`
    - `turn_suppressed`
    - `target_speed_override_enabled`
    - `target_speed_override_value`
    - `effective_speed_target`
  - accepted first-release `event` family:
    - `input_rejected`: malformed JSON, unsupported command name, or other inbound payload outside the accepted decoded command surface
    - `override_cleared`: TTL expiry cleared the active target-speed override while tuning mode may remain enabled
    - `snapshot_cleared`: disconnect or explicit `disable_tuning_mode` cleared the full volatile tuning snapshot
  - `reason`: a human-readable string identifying the event cause such as `"malformed json"`, `"unsupported command"`, `"override TTL expired"`, `"disconnect"`, or `"tuning disabled by command"`
  - `target_speed_override_value`:
    - active override value as JSON number when `target_speed_override_enabled=true`
    - `null` when `target_speed_override_enabled=false`
  - `effective_speed_target`:
    - current lifecycle-owned effective speed target in `SPINUP`, `RUNNING`, or `STOPPING`
    - `0` in `DISARMED`, `START_REQUESTED`, or `FAIL_SAFE_LATCHED`
    - post-clear value under the same phase rule immediately after `override_cleared` or `snapshot_cleared`
- required disabled-mode rejection case:
  - `set_target_speed` and `set_turn_suppressed` return `outcome=\"rejected\"` with reason when `tuning_mode_enabled=false`

Telemetry frames:

- Common required fields:
  - `type=\"telemetry\"`
  - `motion_phase`
  - `tuning_mode_enabled`
  - `turn_suppressed`
  - `target_speed_override_enabled`
  - `target_speed_override_value`
  - `effective_speed_target`
  - `left_speed_target`
  - `right_speed_target`
  - `left_measured_speed`
  - `right_measured_speed`
  - `left_pwm_command`
  - `right_pwm_command`
  - `raw_turn_output`
  - `applied_turn_output`
- field encoding alignment:
  - `target_speed_override_value` follows the same number-or-`null` rule as `state`
  - `effective_speed_target` follows the same phase-based rule as `state`

Unit policy:

- speed values stay in the runtime's existing internal speed units
- `ttl_ms` uses milliseconds
- booleans use JSON booleans

Optional assistant-compatible waveform/image publication:

- may coexist as support evidence
- does not redefine the accepted host contract
- does not create a second accepted command or structured-telemetry topology

**Failure Semantics**

- Missing `matplotlib`: install it before plotting; if install still fails, the tool may fall back to CSV/log capture without changing the board protocol.
- Assistant waveform backpressure: may drop support evidence but must not block control or the JSON-line host session contract.
- Host script exit or disconnect: board clears the full runtime tuning snapshot under the documented timeout/disconnect rules.
- `set_target_speed` or `set_turn_suppressed` while tuning mode is off: reject with reason string; do not silently apply or defer the command.
- invalid `set_target_speed.value`: reject with reason string describing the violation; do not clamp, saturate, or silently replace the requested value.
- `FAIL_SAFE_LATCHED` remote `start` or `stop`: reject with reason string; do not clear the latch or bypass the documented reset path.
- malformed or unsupported inbound payload: reject through standalone `state` with `event=\"input_rejected\"`; do not silently ignore it.
- `stop` during a tuning run: follows lifecycle stop shaping but does not itself emit `snapshot_cleared`; that clear belongs to later `disable_tuning_mode` or disconnect.

**Boundary Examples**

- Allowed: host sends `{\"cmd\":\"set_target_speed\",\"seq\":12,...}` and receives newline-delimited JSON ACK/state/telemetry on the same accepted TCP session.
- Allowed: host sends malformed input and receives a standalone `state` rejection with `event=\"input_rejected\"` and a human-readable reason.
- Allowed: host receives a standalone `state` frame that explicitly reports `snapshot_cleared` with the current cleared tuning flags after disconnect or explicit `disable_tuning_mode`.
- Allowed: host receives `target_speed_override_value=null` when no override is active and `effective_speed_target=0` while the lifecycle is in a non-driving phase.
- Allowed: host sends `enable_tuning_mode`, then `start`, then `set_target_speed` steps, then `stop`, then `disable_tuning_mode` as the accepted end-to-end tuning sequence.
- Allowed: assistant waves still publish support evidence when enabled, but host interoperability does not depend on parsing vendor waveform frames.
- Forbidden: leaving command/feedback framing or the existence of a second side channel unresolved for the first release.
- Forbidden: accepting `set_target_speed` while tuning mode is off without an explicit rejected ACK.
- Forbidden: clamping or silently accepting invalid `set_target_speed.value` inputs.
- Forbidden: treating first-release `seq` as a hidden requirement for duplicate/replay rejection when that behavior is not frozen by the contract.
- Forbidden: leaving malformed/unsupported inbound payload behavior as an implementation-time choice between ignore and reject.
- Forbidden: treating `stop` alone as a first-release synonym for full tuning-snapshot clear.
- Forbidden: tuning workflow requiring manual parsing of ad hoc board logs as its only accepted path.

**Contrast Structure**

- Chosen: one fixed JSON-line host contract plus optional assistant-compatible support evidence.
- Not chosen: assistant-only tuning protocol, host-only polling through logs, or deferring framing choice to implementation.

**Verification Hook**

- Spec/tasks require a minimal host script that can start, stop, set speed, plot, and save CSV through the fixed JSON-line contract.
- Spec/tasks require the mandatory end-to-end tuning sequence to exercise explicit tuning-mode enablement before `set_target_speed`, `stop` before return to non-driving, and explicit `disable_tuning_mode` for the full-snapshot clear.
- Spec/tasks require invalid target-speed rejection coverage and standalone `state` frame parsing for `input_rejected`, `override_cleared`, and `snapshot_cleared` events.
- Spec/tasks require explicit host-visible rejection behavior for remote commands blocked by `FAIL_SAFE_LATCHED`.
- Runtime verification captures both host structured telemetry evidence and optional assistant-compatible support evidence.

## Independent Verification Plan (STANDARD/STRICT)

Verification uses shared sequence `verify-sequence/default` from:
`openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`

Shared contracts:

- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`

Invocation policy:

- verifier agent path: `.codex/agents/verify-reviewer.toml`
- index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- index skill entry points: `openspec-index-preflight`, `openspec-index-maintain`
- verifier invocation method: built-in subagent API in the main process with `fork_context=false`
- verifier invocation template: `verify-reviewer-inline-v3`
- required shared field groups:
  - `invocation_common_required`
  - `output_paths_required`
  - `verifier_evidence_required`
  - `subject_required_any_of`
  - `findings_required`
  - `finding_object_required`
  - `finding_semantics`
  - `repair_routing_rules`
  - `valid_pass_requirements`
  - `partial_scope_rule`

Session semantics:

- checkpoints use the same `active/non_active/closed` cycle
- callers continue a usable `active` verifier first
- callers prefer `send_input` while that same `active` agent is still open
- `send_input` returning `agent not found` routes to `resume` for that same `active` agent
- callers use `resume` only when that same `active` agent was closed and must be restored
- new verifier agents are spawned only when `agent-table.json` literally has no usable `active` agent, or when the orchestrator returns a dedicated recovery spawn reason such as `active_agent_missing` or `active_agent_not_resumable`
- `spawn_active` decisions must expose machine-readable `spawn_reason_code`
- only `block -> pass` marks an agent `non_active`
- termination depends on a valid `active` pass with `review_coverage.coverage_status=complete` and `review_coverage.exhaustive=true`

Transition ownership:

- only the main orchestrator may authorize resume, spawn, repair, or termination
- the caller must not substitute its own judgment for verifier output

Checkpoint plan:

- artifact-completion docs-first review
  - primary surface: changed `proposal/specs/design/tasks`
  - authoritative findings path: `openspec/changes/add-bidirectional-assistant-speed-tuning/verification/artifact-completion/attempt-<n>/findings.json`
  - verifier execution evidence path: `openspec/changes/add-bidirectional-assistant-speed-tuning/verification/artifact-completion/attempt-<n>/verifier-evidence.json`
  - agent table path: `openspec/changes/add-bidirectional-assistant-speed-tuning/verification/artifact-completion/agent-table.json`
  - continuation on pass: implementation entry via `openspec-apply-change`

- source-first checkpoints after implementation
  - checkpoint-1: command ingress and tuning-state isolation
  - checkpoint-2: lifecycle + control-loop integration
  - checkpoint-3: structured telemetry + host tool workflow
  - checkpoint-4: verification evidence bundle / runtime smoke completeness

## Repository Index Cache Plan (When Useful)

- Index contract id: `repo-index-v1`
- Canonical repository-index root: `docs/repo-index/`
- Shared cache-helper sequence: `openspec/schemas/ai-enforced-workflow/index-sequence.md#index-sequence/default`
- Optional refresh scoping hints:
  - `new/code/platform/**`
  - `new/code/runtime/**`
  - `new/code/legacy/**`
  - `new/user/**`
  - `openspec/specs/**`
  - `openspec/changes/add-bidirectional-assistant-speed-tuning/**`
- Fallback policy: `bypass`
- Verifier invocation template: `verify-reviewer-inline-v3`
- Index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- Index skill entry points:
  - `openspec-index-preflight`
  - `openspec-index-maintain`
- Cache-helper evidence path convention: `openspec/changes/add-bidirectional-assistant-speed-tuning/verification/<checkpoint>/index-context/`
- Findings path convention: `openspec/changes/add-bidirectional-assistant-speed-tuning/verification/<checkpoint>/attempt-<n>/findings.json`
- Verifier execution evidence path convention: `openspec/changes/add-bidirectional-assistant-speed-tuning/verification/<checkpoint>/attempt-<n>/verifier-evidence.json`

### Review Checkpoints

- Shared sequence reference: `verify-sequence/default`
- Cache-helper sequence reference: `index-sequence/default`
- Review goal: `implementation_correctness`
- Verifier agent path: `.codex/agents/verify-reviewer.toml`
- Index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- Invocation template id: `verify-reviewer-inline-v3`
- Default loop behavior:
  - resume `active` first
  - prefer `send_input` while that same `active` agent is still open
  - use `resume` only when that same `active` agent was closed and must be restored
  - spawn when no usable `active` agent exists
  - repair follows `block`
  - only `block -> pass` marks `non_active`
  - final termination requires a valid `active` pass
- Authoritative verifier-subagent findings JSON path: `openspec/changes/add-bidirectional-assistant-speed-tuning/verification/<checkpoint>/attempt-<n>/findings.json`
- Verifier execution evidence JSON path: `openspec/changes/add-bidirectional-assistant-speed-tuning/verification/<checkpoint>/attempt-<n>/verifier-evidence.json`
- Agent table path: `openspec/changes/add-bidirectional-assistant-speed-tuning/verification/<checkpoint>/agent-table.json`
- Optional cache-helper report path: `openspec/changes/add-bidirectional-assistant-speed-tuning/verification/<checkpoint>/index-context/report.json`
- Continuation target on pass: next artifact or implementation checkpoint in the same orchestrated sequence

Checkpoint-specific primary surfaces:

- artifact-completion docs-first review: changed `proposal/specs/design/tasks`
- active-change source-first review: changed code, changed tests, directly impacted code


## Migration Plan

1. Extend the assistant transport boundary so receive traffic can be surfaced to a project-owned command decoder without disturbing existing waveform/image publication.
2. Add runtime tuning state and command-handling modules; wire them into `assistant_service` and shared runtime state without changing static parameter loading.
3. Add control-loop consumption of the tuning snapshot:
   - remote `start/stop` map to motion intent
   - target-speed override feeds the lifecycle-owned running-speed input
   - tuning-only turn suppression zeroes only the applied turn path
4. Extend debug snapshot / telemetry export so assistant and host tooling can observe tuning state, raw/applied turn, and wheel-level target/measured/PWM data.
5. Add the host tuning tool and dependency bootstrap for `matplotlib`.
6. Update `debug.sh` / runbooks / verification docs so tuning runs and rollback paths are explicit.

Rollback strategy:

- Disabling tuning mode or removing the host tool leaves the baseline runtime on the existing static-parameter + lifecycle path.
- If bidirectional command handling regresses, the change can be rolled back to assistant-disabled or assistant-read-only behavior without altering actuator API shape.

## Future Work

This change establishes the data-acquisition and remote-control infrastructure required for AI-assisted PID tuning but does not implement the tuning intelligence itself. The expected AI-assisted PID workflow reuses the existing static-params-plus-restart contract rather than adding a runtime hot-write command surface:

1. **Tuning data collection**: use the host tool delivered by this change to run `enable_tuning_mode → start → set_target_speed steps → stop` sequences and capture structured telemetry CSV evidence.
2. **AI analysis**: host-side AI consumes CSV data, analyzes target-versus-measured speed convergence curves, and generates PID parameter recommendations.
3. **Parameter update**: AI writes recommended PID values directly to `default_params.json` on the board filesystem.
4. **Process restart**: AI stops and restarts the board process so the updated params are loaded through the existing startup path.
5. **Iteration**: repeat steps 1–4 until convergence criteria are met, then the operator confirms the final parameter set.

This approach requires no new board-side command surface for PID modification and preserves the current `RuntimeParameters` immutability contract within a running process.

## Open Questions

- No blocking product questions remain for artifact completion.

## Risks / Trade-offs

- Adding remote `start/stop` increases control-path sensitivity; the design contains this by routing all operator requests through motion intent rather than actuator writes.
- A single TCP connection now carries more semantic weight; transport faults therefore need explicit tuning-state expiry/clear semantics.
- Tuning-mode turn suppression improves straight-line wheel tuning but can mislead operators if they forget it is enabled; explicit telemetry and auto-clear-on-exit/disconnect are required.
- Supporting both assistant-compatible evidence and project-owned structured telemetry costs extra implementation surface, but it prevents vendor waveform slots from becoming the only tuning contract.
- Installing `matplotlib` on the host improves usability but adds a dependency step; the workflow mitigates this by allowing CSV capture even if plotting install fails.
