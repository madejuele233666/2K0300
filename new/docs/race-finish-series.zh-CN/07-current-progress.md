# 当前推进记录

## 1. 文档目的

这份文档只回答一个问题：

当前项目实际推进到了哪里。

它不是路线图替代品，也不是阶段任务书替代品，而是给无上下文接手者一个当前快照。

默认先读：

1. `README.md`
2. 本文件
3. `00-master-roadmap.md`
4. `06-implementation-and-verification-contract.md`
5. 当前要执行的阶段文档

## 2. 当前时间点

当前记录时间点：

- `2026-04-19`

如果后续推进了代码、板测或实车测试，这份文档也必须同步更新。

## 2A. 后续维护规则

后续更新这份文档时，至少同时改这几处：

1. `2. 当前时间点`
2. `3. 当前结论`
3. `4. 已完成事项`
4. `5. 尚未完成事项`
5. `9. 推进时间线`

每次更新尽量满足：

1. 写清是文档推进、代码推进、板测推进，还是实车推进。
2. 写清新增证据放在哪里。
3. 写清阶段状态有没有变化。
4. 如果结论被推翻，要明确写“旧结论失效”。

## 3. 当前结论

当前可以明确确认：

1. `race-finish-series.zh-CN` 当前主文档面已经完成两轮 review 闭环：
   - `2026-04-14` 基线文档完成 working + fresh challenger 闭环
   - `2026-04-15` 的 `old_2` 吸收增量也已完成当时的独立验证闭环；对应证据现视作历史归档验证记录
2. 当前文档面上的命令、marker、证据文件、`SMOKE_MAX_FRAMES` wrapper 预算语义、`exp_light` fail-closed 分支、future semantic marker 命名等问题，已经与代码和现行 review 证据重新对齐。
3. `old_2/` 已完成补充分析，并已被吸收到本系列，作为 `old/` 之外的第二条迁移证据链。
4. 这次吸收明确修正了三个关键认识：
   - 当前真正高价值的视觉迁移对象是 `mid_servo` 生成链，而不只是 `Err`
   - LS2K/Linux 历史分支已经给出发车、缓启动、定时线程与车上状态切换语义
   - 现场调参 workflow 需要 project-owned 快速切换和回滚能力
5. 这意味着“路线怎么走、阶段怎么判、证据怎么留、当前实现边界是什么”这件事，文档主线已经稳定，而且对 `old_2` 也已有 challenger-confirmed 的统一定位。
6. `2026-04-15` 的默认 direct-match 板端重跑已经确认：
   - `imu.init`
   - `imu.detect`
   - `startup.complete`
   - `control.start`
   这意味着“IMU 是否能以默认 direct-match profile 启动”已经不是未确认事项；当前 accepted baseline 为 `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.log`、`runtime-smoke-retry-2026-04-15.exit`、`hardware-discovery-retry-2026-04-15.log`，并以同目录 `runtime-smoke-execution-evidence.md` 记录 supersession。
7. 主线 actuator contract 已清理为 `left_pwm + right_pwm + emergency_stop`；`turn_output` 现在只表示左右轮差速调节量。
8. `runtime` 已经增加 project-owned 的控制观测边界，当前日志可区分 `control.veto.*` 原因、`control.command.requested_nonzero`、`control.apply.*` 和 `control.arm.transition`。
9. 对应 OpenSpec change 已完成主规格同步并归档到 `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/`；归档证据包含 `implementation-challenger-evidence.json` 和 `runtime-smoke-2026-04-15.md`，可直接作为这次 contract/observability 收口的历史记录。
10. `2026-04-19` 的 `implement-phase-b-motion-lifecycle` source-first verification 已完成 pass；`checkpoint-4` attempt-3 已把 repaired stop-path、diagnostics-only rerun 和 motor-enabled rerun v2 一并收口到 authoritative findings / verifier evidence 中。
11. `2026-04-19` 当前 `new/` 树已新增双轮独立 PID、`control.snapshot` 结构化观测出口、逐飞助手 TCP-only 波形/图传 sidecar 骨架，并完成一次本地交叉构建通过；但 `new/verification/` 下仍未有已接受的 wheel-level snapshot 板端证据日志。

当前仍然不能确认：

1. 车辆已经完成低速实车闭环。
2. 图片识别或未来距离传感器能力已经真正接入运行时。
3. 车辆已经具备上赛道完赛能力。

因此，当前推进位置是：

- `2026-04-14` 基线文档与验证契约：已闭环
- `2026-04-15` 的 `old_2` 吸收增量：已并入主文档，并已完成历史独立验证闭环
- `2026-04-17` 的 Phase A 实机证据已补齐，允许进入 `Phase B`
- `2026-04-18` 已在 `new/code/runtime/*`、`new/user/main.cpp`、`new/user/run_remote_smoke.sh` 落地 Phase B motion lifecycle、controlled stop-before-exit、fault-latch/re-arm、wrapper-owned frame budget stop 与 harness context contract
- `2026-04-19` 已补齐 stop-exit 修复后的 board rerun evidence，并完成 final source-first review pass；当前剩余问题集中在 `B-1` 到 `B-5` 的板端/实车证据闭环，而不是实现契约或 review gate

## 4. 已完成事项

### 4.1 文档体系

以下事项已完成：

1. `race-finish-series.zh-CN/` 已成为当前主执行文档集。
2. 原先分散的设计、调试、mapping、hardware-matrix、vendor-baseline、track-readiness-gap 等核心思想，已经吸收到当前系列中。
3. 被替代文档已归档到 `new/docs/superseded/`。

### 4.2 文档契约对齐

以下契约现已明确：

1. 三轮车纯差速转向是主线假设。
2. degraded startup 只用于诊断，不是比赛部署路径。
3. 图片识别和未来距离传感器都必须通过 project-owned semantic/strategy 层接入。
4. phase-scoped `.log` 证据文件必须在命令块里显式生成。
5. `run_remote_smoke.sh` 的 wrapper 帧数控制开关是 `SMOKE_MAX_FRAMES`，实际 stop 通过观察 `main.frame.processed` 后发送 `SIGINT` 完成。
6. `2026-04-17` 起，`run_remote_smoke.sh` 默认使用 diagnostics-only 的 `motor=disabled` smoke profile，并自动带 `LS2K_ALLOW_DEGRADED_STARTUP=1`；只有显式设置 `SMOKE_ENABLE_MOTOR=1` 才允许真实电机出力。
6. `exp_light=66` 这类“语法合法但 direct-match 不支持”的分支，已经被写入共享参数验证矩阵。
7. 当前平台没有独立舵机，`servo_pwm` 已从主线 contract 删除；`turn_output` 的文档语义固定为“差速调节量”。
8. `openspec/specs/tc264-to-true-ls2k0300-adapter-layer/spec.md` 与 `openspec/specs/true-ls2k0300-port-workspace/spec.md` 已同步吸收本次差速执行器 contract 与 Phase A control observability 约束。

### 4.2A `old_2` 吸收完成

以下事项已完成：

1. 已把 `old_2/` 定位为 LS2K/Linux 平台下的历史分支，而不是普通备份目录。
2. 已把 `old_2` 的高价值部分按主题并入主文档：
   - Phase B：发车、缓启动、场景相关速度行为
   - Phase C：中线生成、赛道元素状态链、`mid_servo` 语义
   - Phase D：`settings.txt + IPS` 所代表的现场调参 workflow
   - Phase Support：允许吸收与禁止泄漏的边界
3. 已明确 `old_2` 中哪些内容只具参考价值，例如平台封装、构建产物和目录内直接耦合实现。

### 4.3 审查闭环

以下 review 工件已经落盘：

1. `review/race-finish-series-docs/working-findings.json`
2. `review/race-finish-series-docs/working-verifier-evidence.json`
3. `review/race-finish-series-docs/challenger-findings.json`
4. `review/race-finish-series-docs/challenger-verifier-evidence.json`
5. `review/review-runs/race-finish-series-old2-doc-integration-2026-04-15/working/attempt-4/findings.json`
6. `review/review-runs/race-finish-series-old2-doc-integration-2026-04-15/working/attempt-4/verifier-evidence.json`
7. `review/review-runs/race-finish-series-old2-doc-integration-2026-04-15/challenger/attempt-2/findings.json`
8. `review/review-runs/race-finish-series-old2-doc-integration-2026-04-15/challenger/attempt-2/verifier-evidence.json`

当前状态是：

- `2026-04-14` 基线文档：历史独立验证证据已经完整通过
- `2026-04-15` `old_2` 吸收增量：历史独立验证证据已经完整通过

上述两轮闭环合起来，已经覆盖当前 `race-finish-series.zh-CN` 主文档面的现行内容。

## 5. 尚未完成事项

当前真正阻挡“继续往赛道推进”的，不再是文档，而是实机与闭环事实：

1. Phase B 的低速实车闭环还没有进入可验收状态。
2. Phase C 的语义/策略接口还没有变成真实运行时能力。
3. `old_2` 中高价值规则链虽然已经完成文档吸收，但尚未转译成 `new/` 的 project-owned 实现。
4. 默认 smoke 入口已经收口为 bench-safe；后续真实电机验证必须显式 opt-in，不能再把 generic smoke 当成默认可安全上电机的回归入口。
5. Phase B 的代码主线现在已经具备 project-owned motion lifecycle，且 source-first review 已通过，但还没有形成板端通过的 B-1 ~ B-5 实测证据包。
6. 新增的 assistant sidecar 与双轮 PID 代码还没有形成 checkpoint-2 ~ checkpoint-5 的 source-first verifier 证据，也还没有形成 assistant-disabled / assistant-enabled 的板端 observability 证据包。

## 6. 当前推荐下一步

如果下一个接手者要继续推进，不要再优先改大文档，默认按这个顺序做：

1. 以 `02-phase-b-low-speed-vehicle-motion.md` 为主线推进低速实车闭环。
2. 延续当前低风险验证口径，先用受限 PWM 和短窗口实车证据建立 Phase B 基线。
3. 继续利用现有 `control.veto.*`、`control.veto.interval.closed`、`control.unveto.sustained`、`control.apply.command` marker 做车辆行为归因。
4. 每完成一个子任务，就把实际日志和结论回填到 phase 证据目录。
5. 进入 Phase C 时，优先把 `old_2` 的中线生成链和场景状态拆成 project-owned 接口，不要直接整体搬 `camera.cpp`。
6. 如果代码行为与系列文档再次偏离，先修文档或代码的一侧，再继续阶段推进。
7. 板端上传后优先先跑默认 diagnostics-only smoke；只有在验证目标明确需要真实出力时，才使用 `SMOKE_ENABLE_MOTOR=1`。
8. 继续推进时，默认以 `new/verification/phase-b-board-test-checklist.md` 作为 Phase B 总测试入口，再回填 `new/verification/phase-b-b1-static-safety.md` 到 `phase-b-b5-fault-injection.md`。

## 7. 当前不该做的事

在当前状态下，不建议把主要精力优先投入到：

1. 再次重写整套路线文档。
2. 在没有 Phase B 低速实车基线前直接推进 Phase C/D 的大规模实现。
3. 把图片识别或距离传感器逻辑直接塞进 `control_loop.cpp`。
4. 把 degraded 路径误当成“已经可上赛道”的证据。

## 8. 一句话状态

当前项目的最准状态是：

- 路线文档和验证契约已经闭环，Phase A 的硬件闭环与控制解锁证据现已补齐；Phase B 的 motion lifecycle 实现和 source-first verification 也已闭环，下一阶段主问题已经收敛到 `B-1` 到 `B-5` 的低速实车证据。

## 9. 推进时间线

### 2026-04-14

类型：

- 文档与 review 闭环

本次完成：

1. `race-finish-series.zh-CN` 系列文档完成 working + challenger 双通过。
2. phase-scoped 日志生成规则与命令块完成统一。
3. `SMOKE_MAX_FRAMES` 的 wrapper-owned 预算语义与 `main.frame.processed` 观测路径完成对齐；产品运行时不再把 `LS2K_MAX_FRAMES` 当成退出语义。
4. `exp_light=66` direct-match fail-closed 分支写入共享参数验证矩阵。
5. 新增本文件，作为当前推进记录页。

本次未完成：

1. 未新增实机闭环证据。
2. 未推进 Phase A 板测结论。
3. 未改变“当前仍停留在 Phase A”的判断。

关联证据：

1. `review/race-finish-series-docs/working-verifier-evidence.json`
2. `review/race-finish-series-docs/challenger-verifier-evidence.json`

### 2026-04-15

类型：

- 文档推进
- 历史代码分析吸收

本次完成：

1. 完成 `old_2/` 全量梳理，并把其高价值部分并入 `race-finish-series.zh-CN`。
2. 明确 `old/` 与 `old_2/` 的角色分工：
   - `old/` 提供算法血缘
   - `old_2/` 提供 LS2K/Linux 时代的运行语义、赛道规则链和调参 workflow
3. 在主文档中补入 `mid_servo` 生成链、场景状态链、发车/缓启动、参数快速切换与回滚等迁移重点。
4. 按当时的独立验证流程完成复核闭环，并通过对应 guard 校验；该旧流程现已归档，不再作为当前活跃规范。
5. 根据 `2026-04-15` 默认 direct-match 重跑证据，纠正“IMU direct-match 仍未确认”的旧表述，并把 accepted baseline 固定到 `runtime-smoke-retry-2026-04-15.{log,exit}` 与 `hardware-discovery-retry-2026-04-15.log`。
6. 清理主线 actuator contract，删除 `servo_pwm`，并把 `turn_output` 固定为差速调节量。
7. 在 `runtime` 中加入 project-owned 控制观测边界，使 veto 原因、arming 变化与 apply 结果可被 Phase A 证据直接引用。
8. 将上述 delta spec 同步到 `openspec/specs/` 主规格，并把 OpenSpec change 归档到 `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/`。

本次未完成：

1. 还没有把 `old_2` 规则链真正实现进 `new/code/legacy/` 或 `new/code/runtime/`。
2. 还没有补齐 IMU 静止样本、编码器标定、电机真实出力和 `control.veto` 解锁证据；新的 observability marker 只解决“看不清原因”，没有替代板测本身。

阶段状态变化：

- 仍停留在 Phase A，但文档侧已吸收 `old_2`，后续 Phase B/C/D 的实现目标更清晰。

关联证据：

1. `old_2/project/code/camera.cpp`
2. `old_2/project/code/isr.cpp`
3. `old_2/project/user/main.cpp`
4. `old_2/project/code/key.cpp`
5. `review/review-runs/race-finish-series-old2-doc-integration-2026-04-15/challenger/attempt-2/verifier-evidence.json`
6. `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.log`
7. `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/runtime-smoke-2026-04-15.md`
8. `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/implementation-challenger-evidence.json`

### 2026-04-17

类型：

- 代码推进
- 板测推进
- 文档推进

本次完成：

1. 在 `imu_adapter`、`encoder_adapter`、`motor_bridge` 和 `control_loop` 中收口 Phase A 所需的 project-owned 归一化与观测点，把 vendor 编号和符号解释继续限制在 owning boundary 内。
2. 板端补采 `phase-a-imu-closure.*`、`phase-a-encoder-closure.*`、`phase-a-motor-closure.*`、`phase-a-control-unveto.*`、`phase-a-camera-exposure.*` 与 `phase-a-exit-judgment.md`。
3. 控制环运行证据已证明启动 `perception_stale` veto 会闭合，并出现持续非 veto 区间、非零 drive apply 与 `control.apply.command` 左右轮命令观测。
4. 相机 direct-match 路径的曝光策略已定论为 `exp_light=65` bounded mainline；`exp_light=66` 在板上明确 fail-closed。
5. 新增默认关闭的 env-gated bench PWM 脉冲测试，利用 `LS2K_BENCH_PWM_MS`、`LS2K_BENCH_PWM_LEFT`、`LS2K_BENCH_PWM_RIGHT`、`LS2K_BENCH_SETTLE_MS` 做隔离式 side-local 验证，不影响正常运行入口。
6. bench PWM 结果表明左右逻辑 PWM 都能打到对应 side-local 编码器响应，静态 runtime smoke 中的零增量不再被误读成“电机路径已死”。
7. `run_remote_smoke.sh` 已从“可能直接触发真实电机出力”的泛用入口，收口为默认安全的 diagnostics-only smoke；真实电机验证改为显式 `SMOKE_ENABLE_MOTOR=1`。
8. 新增 `phase-a-imu-static-raw-20260417.log`、`imu_ccw_live_20260417.log` 与配对 live yaw 证据，补齐了 IMU 静止姿态稳定性与 `gyro_z` 方向解释。
9. 复核 `runtime_smoke_motor_lowrisk_20260417.log` 后，确认低风险 runtime 命令与逻辑编码器反馈符号一致，Phase A 口径下的 encoder control-trust 已闭环。
10. Phase A 退出判断已更新为允许进入 `Phase B`。

本次未完成：

1. 仍未完成 Phase B 的低速实车闭环验收。
2. 还没有把 `old_2` 的高价值规则链真正转译成运行时主线能力。
3. 还没有证明车辆具备赛道级完赛能力。

阶段状态变化：

- Phase A 退出条件已满足，后续允许进入 `Phase B`。

关联证据：

1. `openspec/changes/archive/2026-04-17-close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-control-unveto.md`
2. `openspec/changes/archive/2026-04-17-close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-camera-exposure.md`
3. `openspec/changes/archive/2026-04-17-close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-bench-pwm.md`
4. `openspec/changes/archive/2026-04-17-close-phase-a-hardware-loop-and-runtime-unveto/verification/phase-a-exit-judgment.md`

### 2026-04-18

类型：

- 代码推进
- 文档推进

本次完成：

1. 在 `runtime` 中新增 project-owned `motion_supervisor`、`motion_types`、Phase B 参数面和 runtime state，显式区分 `DISARMED`、`START_REQUESTED`、`SPINUP`、`RUNNING`、`STOPPING`、`FAIL_SAFE_LATCHED`。
2. `control_loop` 已改成 `sample -> gate -> motion supervisor -> shaping/apply -> observation` 顺序，并补入 `control.apply.hold_disarmed`、`motion.phase.transition`、`motion.spinup.*`、`motion.stop.complete`、`motion.failsafe.*` marker。
3. `main.cpp` 已把 start / stop / reset intent 与进程退出拆开；当前 accepted baseline 是 `SIGUSR2` start、`SIGUSR1` fault reset、`SIGINT` controlled stop、`SIGTERM` forced shutdown fallback。
4. `run_remote_smoke.sh` 已记录独立 harness context block，并记录 `LS2K_AUTO_*`、`SMOKE_MAX_FRAMES`、Phase B fault injection env；当启用 bounded frame budget 时，wrapper 通过 `main.frame.processed` 观察 frame 进度并先发出 `SIGINT`，必要时再升级到 `SIGTERM` / `SIGKILL`。
5. 新增 `new/verification/phase-b-b1-static-safety.md` 到 `phase-b-b5-fault-injection.md` 五份 implementation-grade Phase B notes。

本次未完成：

1. 还没有板端实跑 `B-1` 到 `B-5`，因此 Phase B 仍未进入可验收状态。
2. 当日时点尚未完成 `openspec-verify-change` 和所有 checkpoint review 的 authoritative pass 证据。

阶段状态变化：

- 仍停留在 Phase B；从“仅有文档目标”推进到“代码主线已具备 motion lifecycle contract，但板端证据未闭环”。

关联证据：

1. `new/code/runtime/motion_supervisor.cpp`
2. `new/user/main.cpp`
3. `new/user/run_remote_smoke.sh`
4. `new/verification/phase-b-b5-fault-injection.md`

### 2026-04-19

类型：

- 代码推进
- 板测推进
- 文档推进
- review 收口

本次完成：

1. 修复了 `SMOKE_MAX_FRAMES` bounded run 到点后仍需外部强制结束的问题：`STOPPING` 现在直接拥有执行器降零路径，不再让 residual PID 输出拖住 real-motor stop。
2. `STOPPING -> DISARMED` 的 stop-completion 复判改为基于当前 cycle 仍可到达执行器的 effective command zero，而不是仅依赖 diagnostics-oriented requested PWM 零值。
3. 重新采集并落盘 diagnostics-only stop-exit 修复日志与 motor-enabled stop-exit v2 日志；两者都已出现 `motion.stop.complete`、`main.exit.ready`，且不再以 `main.exit.forced` 作为正常退出证据。
4. `openspec/changes/implement-phase-b-motion-lifecycle/verification/checkpoint-4/attempt-3/` 已写入 authoritative `findings.json` 与 `verifier-evidence.json`，`agent-table.json` 已更新到 active valid pass 终态，orchestrator 对当前 active verifier 的机械决议为 `terminate`。

本次未完成：

1. 仍未形成 `B-1` 到 `B-5` 的完整板端/实车 evidence bundle。
2. Phase B 退出条件要求的轻弯修正、fault recovery 和参数集固化，仍待实测关闭。

阶段状态变化：

- 仍停留在 Phase B；但主问题已经从“runtime contract 与 stop-exit 语义是否成立”收敛到“板端证据是否足够通过 B-1 ~ B-5”。

关联证据：

1. `new/code/runtime/control_loop.cpp`
2. `openspec/changes/implement-phase-b-motion-lifecycle/verification/checkpoint-4/runtime-smoke-stop-exit-fix.log`
3. `openspec/changes/implement-phase-b-motion-lifecycle/verification/checkpoint-4/runtime-smoke-motor-enabled-stop-exit-fix-v2.log`
4. `openspec/changes/implement-phase-b-motion-lifecycle/verification/checkpoint-4/attempt-3/verifier-evidence.json`

## 10. 后续记录模板

后续新增记录时，直接按下面格式追加一个新小节：

```md
### YYYY-MM-DD

类型：

- 文档推进 / 代码推进 / 板测推进 / 实车推进

本次完成：

1. ...
2. ...

本次未完成：

1. ...
2. ...

阶段状态变化：

- 仍停留在 Phase X / 从 Phase X 进入 Phase Y

关联证据：

1. ...
2. ...
```
