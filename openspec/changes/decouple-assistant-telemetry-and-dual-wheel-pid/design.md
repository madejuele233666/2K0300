## Context

`new/` 已经建立了清晰的 Phase B 主线边界：

- `new/code/runtime/*` 负责生命周期、前台感知、周期控制和退出。
- `new/code/platform/*` 负责 project-owned adapter。
- `new/code/platform/true_ls2k0300/*` 负责唯一允许触碰 vendor 头文件、设备节点和 free function 的 bridge。
- `new/code/legacy/*` 负责从 `old/` 与 `old_2/` 迁移来的算法语义。

当前实现的优点是生命周期和 actuator contract 已经 project-owned；当前实现的不足也很明确：

1. `new/code/runtime/control_loop.cpp` 仍使用 `ComputeMeanSpeedPwm(...) + Mix(mean_pwm, turn_output)`，内部仍是“均速 PID + PWM 差速混控”。
2. `new/` 当前没有逐飞助手运行时集成。即使 `new/user/CMakeLists.txt` 已经包含 `seekfree_assistant` 头路径，实际 target 里也没有把 `seekfree_assistant.cpp` / `seekfree_assistant_interface.cpp` 编入主程序。
3. 如果直接把逐飞助手塞进 `ControlLoop::Tick()`，或者直接在 `legacy` / `runtime` 中引用 vendor 协议接口，会破坏 `new/` 现有最重要的隔离成果。

本变更要解决的是：在不破坏 Phase B 生命周期主线的前提下，同时补一条优雅的逐飞助手遥测/图传 sidecar 和一条优雅的双轮分别 PID 主控制路径。

## Goals / Non-Goals

**Goals:**

- 为 `new/` 增加一条 project-owned、可选启用的逐飞助手遥测/图传侧链。
- 为 `new/` 控制主线增加左右轮独立 PID 闭环，并把左右轮目标生成与最终执行解耦。
- 保持对外 actuator contract 仍然只有 `left_pwm` / `right_pwm` / `emergency_stop`。
- 保持 vendor 隔离边界：逐飞助手 vendor API、TCP free function 和协议数据结构只能出现在 owning bridge。
- 为 Phase B 提供更直接的左右轮可观测性，方便板端与实车验证。

**Non-Goals:**

- 不把逐飞助手升级成新的 startup-critical subsystem。
- 不改变 `motion_supervisor` 的 Phase B 生命周期语义。
- 不把 `hardware_profile.json` 扩展成必须声明 `assistant` block 的强制输入。
- 不在本变更中引入新的图片识别算法、距离传感器或新的比赛策略层。
- 不把 `old_2/` 的运行时组织或 vendor-like 全局变量整体搬入 `new/`。

## Decisions

### Decision: Treat Seekfree Assistant As An Optional Foreground Sidecar

**Problem being solved**

`new/` 需要逐飞助手能力来做波形观测与 E07_04 风格图传，但该能力不属于 Phase B 的 safety-critical 传感器/执行器主链。如果把它做成新的关键 adapter 或放进 `ControlLoop::Tick()`，一旦链路卡顿、断链或协议异常，就会污染控制周期和 fail-closed 启动判定。

**Alternatives considered**

1. 把逐飞助手做成新的 `PlatformBundle` 关键 adapter，并纳入 `RunStartup()`。
2. 在 `runtime/control_loop.cpp` 周期回调里直接调用 `seekfree_assistant_*`。
3. 在 `main` 前台循环中挂一个 project-owned sidecar service，由它桥接逐飞助手。

**Why this option was chosen**

选项 3 保留了现有主线的边界纪律：

- `ControlLoop::Tick()` 仍只做“传感器快照 -> gate -> motion -> command -> apply”。
- `main` 前台线程本来就负责信号、感知前台循环、harness 和退出；在这里轮询 assistant sidecar 不会污染 5ms 控制回调。
- 逐飞助手连接失败只影响调试，不影响 `startup.complete` 或 `control.start`。
- transport 边界可以为未来扩展保留抽象，但本变更只接受 TCP 实现，避免把未实现 transport 带进验收面。
- 首版 sidecar 只发布只读波形和图像，不开放在线写参，避免把调参语义提前耦进主控制路径。

**Stack Equivalent**

- Product loop: `main` foreground loop
- Sidecar owner: `runtime/assistant_service.*`
- Project-owned transport boundary: `platform/assistant_link.*`
- Vendor-facing bridge: `platform/true_ls2k0300/assistant_bridge.*`

**Named Deliverables**

- `new/code/runtime/assistant_service.hpp/.cpp`
- `new/code/platform/assistant_link.hpp/.cpp`
- `new/code/platform/true_ls2k0300/assistant_bridge.cpp`
- `new/code/runtime/control_debug_snapshot.hpp` or equivalent project-owned snapshot type
- `new/code/runtime/camera_frame_snapshot.hpp` or equivalent project-owned frame handoff type
- `new/user/main.cpp` integration
- `new/user/CMakeLists.txt` build graph updates

**Failure Semantics**

- Assistant disabled or initialization failure: log warning/info and continue runtime without sidecar.
- Assistant send/read failure: drop or rate-limit diagnostics; never veto motion by itself.
- Assistant receive payload malformed or unsupported: ignore payload and emit project-owned warning/info without mutating runtime parameters.

**Boundary Examples**

- Allowed: `assistant_service` reads project-owned control/image snapshots and publishes wave/image channels.
- Allowed: `assistant_bridge` owns `seekfree_assistant_interface_init(tcp_client_send_data, tcp_client_read_data)`.
- Forbidden: `runtime/control_loop.cpp` directly includes `seekfree_assistant.h`.
- Forbidden: `legacy/*` directly reads assistant parameter arrays or TCP buffers.

**Contrast Structure**

- Chosen: optional sidecar, foreground-polled, failure-isolated.
- Not chosen: startup-critical subsystem, timer-callback protocol handling, vendor API leakage into runtime.

**Verification Hook**

- Source-first review confirms `seekfree_assistant*` and `tcp_client_*` only appear in owning bridge files.
- Runtime verification shows `control.start` and motion lifecycle still succeed when assistant sidecar is disabled or unavailable.
- Phase B evidence can show assistant-published wave/image channels without introducing new `control.veto.*` causes.

### Decision: Publish A Project-Owned ControlDebugSnapshot Instead Of Letting Sidecars Read Raw Runtime Internals

**Problem being solved**

逐飞助手、diagnostics、结构化验证导出和后续扩展点都需要看到控制内部量。如果每条辅助链路都直接拼装 `RuntimeState`、PID 内部状态和局部计算结果，就会形成第二套非正式 API，降低可维护性。

**Alternatives considered**

1. 让 assistant sidecar 直接读取 `RuntimeState` 与各控制器对象。
2. 仅依赖当前文本 diagnostics marker，不建立结构化快照。
3. 增加 project-owned `ControlDebugSnapshot`，由控制主线统一写入，辅助侧链和验证导出统一消费。

**Why this option was chosen**

选项 3 最符合 `new/` 当前架构：

- 主控制路径拥有计算结果，最适合统一产出结构化快照。
- 辅助链路只读快照，不再依赖控制器实现细节。
- diagnostics 仍保留文本 marker，但波形、图传映射和验证导出都可以复用结构化字段。

**Stack Equivalent**

- Producer: `runtime/control_loop.cpp`
- Shared carrier: `runtime/runtime_state.hpp` or dedicated snapshot header
- Consumers: diagnostics helpers, structured control evidence reporter, assistant sidecar

**Named Deliverables**

- `new/code/runtime/control_debug_snapshot.hpp` or equivalent
- `RuntimeState` snapshot field
- `ControlLoop::Tick()` snapshot publishing
- structured diagnostics or harness export mapping code
- assistant channel mapping code

**Failure Semantics**

- Snapshot missing or stale: assistant sidecar and evidence exporter report zero/no publish for that cycle; it does not affect actuator output.
- Snapshot schema evolution: controlled through project-owned type changes, not ad hoc globals.

**Boundary Examples**

- Allowed fields: motion phase, veto reason, effective speed target, left/right target, left/right measured speed, turn correction, left/right PWM command.
- Forbidden fields: raw vendor device-node paths, vendor struct pointers, assistant protocol buffers.

**Contrast Structure**

- Chosen: one project-owned snapshot API.
- Not chosen: direct multi-reader access to PID internals, or “every consumer自己计算一遍”。

**Verification Hook**

- Source review finds sidecar and diagnostics consume a project-owned snapshot type instead of control-loop locals.
- Docs-first review confirms Phase B evidence plan names left/right target/feedback/output channels explicitly and that at least one non-assistant export path exists.

### Decision: Pair The Snapshot With A Project-Owned Non-Assistant Evidence Surface

**Problem being solved**

逐飞助手 sidecar 是可选能力，因此 Phase B 不能把 wheel-level observability 的主验收面押在 assistant 是否连通上。需要一个 project-owned、assistant-disabled 也成立的证据出口。

**Alternatives considered**

1. 只把 assistant 波形当成唯一的 wheel-level 证据面。
2. 继续依赖零散文本 marker，由 reviewer 自行拼接左右轮行为。
3. 建立 project-owned 的结构化 control evidence exporter，把 `ControlDebugSnapshot` 以 diagnostics marker、结构化日志或 harness 可见导出的形式输出。

**Why this option was chosen**

选项 3 同时满足解耦和可验证性：

- 主控制路径只负责写 snapshot，不感知 assistant 是否存在。
- assistant-disabled 板端验证仍能拿到左右轮目标、反馈和 PWM 的同一份 project-owned 证据。
- verifier 可以把 structure-first 的 snapshot export 当主证据，把 assistant wave/image 当辅助证据。

**Stack Equivalent**

- Producer: `runtime/control_loop.cpp`
- Primary evidence carrier: `runtime/control_debug_snapshot.*`
- Non-assistant exporter: `runtime/control_debug_reporter.*` or equivalent diagnostics/harness helper
- Optional support exporter: `runtime/assistant_service.*`

**Named Deliverables**

- `new/code/runtime/control_debug_reporter.hpp/.cpp` or equivalent
- structured diagnostics marker schema or harness-visible export of `ControlDebugSnapshot`
- verification notes that name the non-assistant evidence surface explicitly

**Failure Semantics**

- Reporter unavailable or rate-limited: assistant-disabled verification may lose convenience detail, but control semantics remain unchanged.
- Assistant absent: reporter output remains the primary wheel-level evidence surface.

**Boundary Examples**

- Allowed: diagnostics helper emits structured `control.snapshot` markers with left/right target, feedback, and PWM.
- Allowed: harness or verification script reads exported control snapshots without depending on assistant connectivity.
- Forbidden: acceptance requires assistant TCP connection in order to observe wheel-level behavior.

**Contrast Structure**

- Chosen: project-owned primary evidence surface plus optional assistant support evidence.
- Not chosen: assistant-only observability, or ad hoc log scraping without a declared schema.

**Verification Hook**

- Docs-first review confirms assistant-disabled evidence is explicit in specs/tasks/docs.
- Source-first review confirms the reporter/exporter consumes `ControlDebugSnapshot` rather than recomputing wheel quantities.

### Decision: Replace Mean-Speed PWM Mixing With WheelTargetMixer Plus Left/Right WheelPidController

**Problem being solved**

当前 `new/` 的速度控制路径仍把平均编码器速度映射成 `mean_pwm`，再把 `turn_output` 作为 PWM 偏置混到左右轮。这会把目标生成、速度闭环和差速执行耦合在一起，使左右轮无法拥有独立积分状态，也不利于解释 Phase B 实车行为。

**Alternatives considered**

1. 保留 `ComputeMeanSpeedPwm + Mix()`，只在观测层补左右轮数据。
2. 在 `platform/motor_adapter` 或 `true_ls2k0300/motor_bridge` 层补左右轮 PID。
3. 在 `legacy + runtime` 内部重构为 `WheelTargetMixer + LeftWheelPid + RightWheelPid`。

**Why this option was chosen**

选项 3 保持外部 contract 不变，同时真正解耦内部控制语义：

- `runtime` 仍只向 `motor_adapter` 发送 `ActuatorCommand`。
- `platform` 继续只负责“执行命令”，不参与控制器状态。
- 左右轮 PID 独立拥有自己的误差、积分和导数状态。
- 转向调节从“PWM 偏置”转成“左右轮目标差”，更适合解释和调参。

**Stack Equivalent**

- Base target source: `motion_supervisor` effective speed target
- Turn source: preserved `ComputeTurnTarget + ComputeGyroTurn`
- Target shaping boundary: `legacy/wheel_target_mixer.*`
- Wheel control boundary: `legacy/wheel_pid.*`
- Final command ownership: `runtime/control_loop.cpp`

**Named Deliverables**

- `new/code/legacy/wheel_target_mixer.hpp/.cpp`
- `new/code/legacy/wheel_pid.hpp/.cpp`
- `new/code/legacy/pid_control.*` speed-path simplification
- `new/code/runtime/control_loop.cpp` command-generation rewrite
- `new/config/default_params.json` explicit left/right wheel PID parameters with copied initial baseline values

**Failure Semantics**

- Encoder invalid: control gate remains vetoed before entering wheel PID apply path.
- Lifecycle hold-disarmed or stopping: wheel controllers are bypassed or reset according to existing lifecycle semantics.
- Parameter missing/malformed: param store falls back per existing fail-closed/default rules; dual-wheel left/right fields must be validated explicitly as independent parameters.

**Boundary Examples**

- Allowed: `control_loop` computes `left_speed_target` / `right_speed_target` from `effective_speed_target` and bounded turn correction.
- Allowed: each wheel PID consumes one logical wheel target and one normalized encoder value.
- Forbidden: `motor_bridge` deciding left/right PID behavior.
- Forbidden: wheel PID reading raw `/dev/zf_*` nodes or vendor encoder symbols.

**Contrast Structure**

- Chosen: target generation and wheel execution are split.
- Not chosen: “均速 PID 继续保留，只是多记几条波形”，或“把 PID 放到 adapter/bridge 内部”。

**Verification Hook**

- Source-first review shows `ComputeMeanSpeedPwm` no longer owns the main wheel-speed path.
- Runtime diagnostics or assistant channels expose left/right target, measured speed, and PWM separately.
- Phase B bench/half-load evidence can explain wheel-direction, wheel-balance and low-speed behavior without靠均值速度推断。

### Decision: Keep The Public Actuator Contract And Lifecycle Contract Unchanged

**Problem being solved**

双轮 PID 重构和逐飞助手 sidecar 都会增加内部模块；如果顺便修改 `ActuatorCommand` 或 `motion_supervisor` 语义，会把当前已经闭环的 Phase B 主线重新打开。

**Alternatives considered**

1. 扩展 `ActuatorCommand` 增加 debug/tuning/servo-like 字段。
2. 把 wheel targets 或 assistant signals 加进 lifecycle state machine。
3. 保持现有 public actuator contract 和 lifecycle contract 不变，只重构内部 command-generation 和 sidecar。

**Why this option was chosen**

选项 3 风险最小，也最符合已有规格：

- `ActuatorCommand` 继续只有 `left_pwm/right_pwm/emergency_stop`。
- `motion_supervisor` 继续是唯一生命周期 owner。
- 逐飞助手和双轮 PID 都是围绕主线的内部优化，而不是另起产品语义。

**Stack Equivalent**

- Public contract: `port::ActuatorCommand`
- Lifecycle owner: `runtime/motion_supervisor.*`
- Internal refactor zone: `runtime/control_loop.cpp` + `legacy/*`

**Named Deliverables**

- No new public actuator fields
- No new lifecycle phase names
- Updated docs and specs clarifying unchanged external contract

**Failure Semantics**

- If sidecar is absent, runtime behavior is identical to assistant-disabled baseline.
- If dual-wheel controller path is not eligible because gate/lifecycle blocks output, fail-safe behavior remains identical to current baseline.

**Boundary Examples**

- Allowed: internal `ControlDebugSnapshot` includes left/right targets.
- Forbidden: public `ActuatorCommand` gains `left_target`, `right_target`, `servo_pwm`, or assistant-specific payloads.

**Contrast Structure**

- Chosen: stable public contract, evolving internal control path.
- Not chosen: use refactor as an excuse to reshape the platform-facing API.

**Verification Hook**

- Spec delta and code review confirm `port::ActuatorCommand` shape remains unchanged.
- Existing motion-lifecycle verification remains reusable after refactor.

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
- verifier invocation method: built-in subagent API with `fork_context=false`
- invocation template: `verify-reviewer-inline-v3`
- cache-helper sequence: `openspec/schemas/ai-enforced-workflow/index-sequence.md#index-sequence/default`
- index skill entry points: `openspec-index-preflight`, `openspec-index-maintain`

Session semantics:

- checkpoints use the same `active/non_active/closed` cycle
- callers continue a usable `active` verifier first
- callers prefer `send_input` while the same `active` agent is still open
- `send_input` returning `agent not found` routes to `resume` for that same `active` agent
- callers use `resume` only when that same `active` agent was closed and must be restored
- spawn is allowed only when `agent-table.json` literally contains no usable `active` agent, or when the orchestrator returns a dedicated recovery spawn reason such as `active_agent_missing`
- only `block -> pass` marks an agent `non_active`
- termination depends on a valid `active` pass with `review_coverage.coverage_status=complete` and `review_coverage.exhaustive=true`

Transition ownership:

- only the main orchestrator may authorize resume, spawn, repair, or termination
- the caller must not substitute its own judgment for verifier output

Gemini policy:

- dual review is optional only when repository/checkpoint policy explicitly enables it
- Stage A default gate remains verifier-first

Runner contract details when Gemini is enabled:

- runner contract id: `gemini-capture`
- raw report path, normalized report path, retry and recovery semantics are recorded in checkpoint execution evidence rather than hard-coded into workflow prose

Review checkpoints for this change:

- checkpoint-1: docs-first artifact gate
  - primary surface: `proposal.md`, `design.md`, `tasks.md`, and all change-local `specs/**/*.md`
  - authoritative outputs:
    - `openspec/changes/decouple-assistant-telemetry-and-dual-wheel-pid/verification/checkpoint-1/attempt-<n>/findings.json`
    - `openspec/changes/decouple-assistant-telemetry-and-dual-wheel-pid/verification/checkpoint-1/attempt-<n>/verifier-evidence.json`
    - `openspec/changes/decouple-assistant-telemetry-and-dual-wheel-pid/verification/checkpoint-1/agent-table.json`
- checkpoint-2: source-first assistant sidecar and control snapshot slice
- checkpoint-3: source-first dual-wheel control slice
- checkpoint-4: source-first parameter, docs, and verification-surface slice
- checkpoint-5: final full-change source and evidence bundle

Shared field groups consumed from the contract files:

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

## Repository Index Cache Plan (When Useful)

- Index contract id: `repo-index-v1`
- Canonical repository-index root: repository root scoped to `/home/madejuele/projects/2K0300`
- Shared cache-helper sequence: `index-sequence/default`
- Optional refresh scoping hints: `new/code/runtime/**`, `new/code/legacy/**`, `new/code/platform/**`, `new/user/**`, `new/config/**`, and change-local docs/spec artifacts
- Fallback policy: `bypass` when cache is unavailable; repository-index is helper-only, never a gate
- Verifier invocation template: `verify-reviewer-inline-v3`
- Index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- Index skill entry points:
  - `openspec-index-preflight`
  - `openspec-index-maintain`
- Cache-helper evidence path convention:
  - `openspec/changes/decouple-assistant-telemetry-and-dual-wheel-pid/verification/checkpoint-<n>/attempt-<n>/index-context.json`
- Findings path convention:
  - `openspec/changes/decouple-assistant-telemetry-and-dual-wheel-pid/verification/checkpoint-<n>/attempt-<n>/findings.json`
- Verifier execution evidence path convention:
  - `openspec/changes/decouple-assistant-telemetry-and-dual-wheel-pid/verification/checkpoint-<n>/attempt-<n>/verifier-evidence.json`

Review completion contract:

- execution evidence MUST record `review_goal`, `review_phase`, `review_scope`, `review_coverage`, `reviewed_paths`, `skipped_paths`, `reviewed_axes`, and `unreviewed_axes`
- each checkpoint MUST maintain `agent-table.json`

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
- Authoritative verifier-subagent findings JSON path:
  - `openspec/changes/decouple-assistant-telemetry-and-dual-wheel-pid/verification/checkpoint-<n>/attempt-<n>/findings.json`
- Verifier execution evidence JSON path:
  - `openspec/changes/decouple-assistant-telemetry-and-dual-wheel-pid/verification/checkpoint-<n>/attempt-<n>/verifier-evidence.json`
- Agent table path:
  - `openspec/changes/decouple-assistant-telemetry-and-dual-wheel-pid/verification/checkpoint-<n>/agent-table.json`
- Optional cache-helper report path:
  - `openspec/changes/decouple-assistant-telemetry-and-dual-wheel-pid/verification/checkpoint-<n>/attempt-<n>/index-context.json`
- Continuation target on pass:
  - docs-first pass -> implementation entry
  - checkpoint-2/3/4 pass -> next implementation slice
  - checkpoint-5 pass -> change verification completion

### Optional Gemini Dual Review

- Runner contract: `gemini-capture`
- Raw report path:
  - `openspec/changes/decouple-assistant-telemetry-and-dual-wheel-pid/verification/checkpoint-<n>/attempt-<n>/gemini-raw.json`
- Normalized report path:
  - `openspec/changes/decouple-assistant-telemetry-and-dual-wheel-pid/verification/checkpoint-<n>/attempt-<n>/gemini-report.json`
- Maximum attempts:
  - 2
- Recovery behavior:
  - `input_raw_path -> report_path`
- Resolved command goes in execution evidence, not workflow policy prose

## Migration Plan

1. Introduce the project-owned snapshot boundary, non-assistant evidence exporter, and assistant sidecar files without changing control semantics.
2. Wire `new/user/CMakeLists.txt` and `new/user/main.cpp` so the assistant sidecar can be enabled or disabled independently from startup-critical adapters, with TCP-only waveform + image publication.
3. Introduce `wheel_target_mixer` and `wheel_pid` modules, then switch `control_loop.cpp` from mean-speed mixing to left/right target + left/right independent PID generation.
4. Extend runtime parameters with explicit left/right dual-wheel fields and assistant-sidecar enablement fields through `param_store.cpp` and `default_params.json`.
5. Update diagnostics and Phase B evidence expectations to use left/right target/feedback/output channels and assistant-disabled structured evidence export.
6. Preserve rollback path by keeping public actuator contract and motion lifecycle unchanged; if the refactor is reverted, only the internal command-generation slice and sidecar integration need to be removed.

## Resolved First-Release Contract

- 逐飞助手首版只提供只读波形和 E07_04 风格图传，不开放在线写参。
- transport 边界预留未来扩展点，但本变更只实现 TCP。
- 双轮 PID 的左右轮参数从第一版起独立存在，只允许初始默认值复制自同一份 baseline。
- Phase B 的主证据面必须是 project-owned 非助手结构化观测出口；assistant 波形和图传只作为辅助证据。

## Risks / Trade-offs

- 新增 sidecar 会增加主进程中的一条前台辅助链路；如果实现偷懒，把协议处理放进控制回调，就会破坏当前 timing 边界。
- 双轮 PID 重构会显著改变低速实车调参面；如果不同时提供结构化 observability，Phase B 诊断成本会升高。
- 如果后续要开放调参写入，必须另起 change，在 project-owned overlay 边界、验证和回滚策略明确后再进入验收面。
- 维持 public actuator contract 不变会让内部实现多一层映射，但这是为了保住已闭环的外部规格和验证口径，值得接受。
