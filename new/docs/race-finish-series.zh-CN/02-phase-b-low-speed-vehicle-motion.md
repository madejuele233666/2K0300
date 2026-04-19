# Phase B：低速实车闭环

阶段状态：

- 当前激活阶段
- `2026-04-19` 时点下，Phase B 的 runtime lifecycle contract、controlled stop-before-exit 语义和 source-first review 已闭环通过。
- 当前剩余主问题不再是文档或 stop-exit 语义，而是板端 `B-1` 到 `B-5` 实测证据包尚未闭环。

## 1. 阶段目标

让车辆在真实地面上完成低速、可控、可恢复、可解释的闭环运动。

这一阶段的目标不是“跑得快”，而是验证下面这条 project-owned 生命周期：

1. `DISARMED`
2. `START_REQUESTED`
3. `SPINUP`
4. `RUNNING`
5. `STOPPING`
6. `FAIL_SAFE_LATCHED`

并且验证：

1. 车辆从显式 start intent 才能进入运动生命周期，而不是“进程活着就默认能跑”
2. 起步和停车是可重复、可审查的 lifecycle 行为，而不是偶然的 PWM 突变
3. 异常时会进入 fail-safe latch，恢复时必须经过明确 reset intent
4. smoke / bounded automation 只是 test harness，不是长期产品语义

## 1A. 本阶段继承的控制与安全原则

本阶段默认以下前提已经成立：

1. Phase A 已经把关键硬件链路从诊断态推进到可控制态。
2. 低速实车阶段仍然以 fail-safe 优先，而不是以“先跑起来”为优先。
3. 本阶段验证的是方向、速度、恢复能力，不是比赛成绩。
4. 任何实车异常都必须能立刻回退到可人工接管状态。

## 1C. Phase B lifecycle contract

本阶段默认运行时契约如下：

1. `new/code/runtime/motion_supervisor.*` 是生命周期唯一 owner。
2. `new/code/platform/*` 只负责 vendor-facing 归一化和受限 fault injection，不负责运动状态机。
3. `control.veto.*` 只表示真实 safety blocker；`control.apply.hold_disarmed` 用来表示“安全门已开，但当前仍故意不出力”。
4. 产品运行时的基线 reset 触发是 `SIGUSR1`，用于把 `FAIL_SAFE_LATCHED` 映射到 `reset_fault_requested`。
5. 产品运行时的基线 start 触发保持在 accepted runtime entrypoint 上；当前实现使用 `SIGUSR2` 作为最小可用 operator-owned start intent。
6. 当前实现中，`SIGINT` 表示 operator-owned controlled stop；`SIGTERM` 保留为 forced shutdown fallback，用于 wrapper timeout 或 faulted session 无法自然回到 `DISARMED` 的兜底退出。
7. `LS2K_AUTO_START`、`LS2K_AUTO_START_DELAY_MS`、`LS2K_AUTO_STOP_AFTER_MS`、`LS2K_AUTO_RESET_FAULT` 仍是 accepted runtime entrypoint 上的可移除 harness helper；bounded frame budget 则由 wrapper 侧的 `SMOKE_MAX_FRAMES` 持有。
8. 当设置 `SMOKE_MAX_FRAMES` 时，`run_remote_smoke.sh` 必须观察 runtime 的 `main.frame.processed` marker，再通过 `SIGINT` 请求 controlled stop；若 timeout 或 faulted session 不能 graceful shutdown，wrapper 再升级到 `SIGTERM` / `SIGKILL` 兜底退出。
9. `run_remote_smoke.sh` 产出的日志头必须单独记录 `harness_context_begin ... harness_context_end`，不能只靠 runtime marker 反推测试注入条件。

## 1B. `old_2` 对本阶段的补充约束

`old_2/` 说明了 LS2K/Linux 形态下，低速实车闭环不能只看“PID 算出来了没有”，还要把发车和停机动作本身视为控制语义的一部分。

当前迁移阶段应吸收的高价值事实包括：

1. `old_2/project/user/main.cpp` 和 `old_2/project/code/isr.cpp` 已经把主循环、定时触发和车上状态切换组织成 Linux 运行时，而不是单次函数调用。
2. `old_2/project/code/isr.cpp` 明确体现了发车后的场景速度调度、刹停和无刷缓启动状态；`old_2/project/user/main.cpp` 负责按键发车后的状态复位与进入 `run_brushless_start`。
3. `old_2/project/code/motor.cpp` 主要体现速度 PID、差速与转向控制面，而不是启动/停车生命周期本体。
4. 因此，本阶段不应把“车能动”误判成通过；还要确认起步不过冲、减速不突兀、停机可复现、异常后能重新进入可控状态。

## 2. Phase B 任务清单

### B-1 静态安全检查

目标：
把低速实车前的安全边界和标准起停流程固定下来。

任务：

1. 检查轮组安装方向、编码器方向、IMU 安装方向。
2. 检查线束、电源、电机、相机固定。
3. 确认停机和急停路径可用。
4. 固化安全操作流程：
   - 上电
   - 启动
   - 停机
   - 异常中断

验收标准：

1. 连续多次上电/停机没有残留异常状态。
2. 人工可随时接管和断电。

触达文件：

- `new/code/runtime/startup.cpp`
- `new/code/runtime/shutdown.cpp`
- `new/docs/race-finish-series.zh-CN/04-phase-d-track-operations-and-tuning.md`

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-b-b1-static-safety.log SMOKE_ENABLE_MOTOR=0 SMOKE_AUTO_START=0 SMOKE_MAX_FRAMES=20 ./run_remote_smoke.sh)
```

预期 marker：

- `main.harness_context`
- `main.frame.processed`
- `startup.complete`
- `control.start`
- `motion.start.requested`
- `motion.stop.requested`
- `motion.stop.complete`
- `main.exit.ready`
- `shutdown.complete`

证据文件：

- `new/verification/phase-b-b1-static-safety.md`
- `phase-b-b1-static-safety.log`
- `phase-b-b1-manual-lifecycle.log`

失败回退方向：

- 启动/停机/急停链路异常：回 Phase A

### B-2 架空轮与半载测试

目标：
在不直接上地跑的前提下确认左右轮与差速转向方向都正确。

任务：

1. 架空轮验证左右轮响应方向。
2. 验证差速调节对转向趋势的影响。
3. 观察启动、减速、急停时的输出平顺性。
4. 记录电机响应延迟和过冲。
5. 参考 `old_2/project/code/isr.cpp` 的缓启动状态与 PWM 渐增逻辑，确认当前实现不会以突变占空比直接冲击轮组。

验收标准：

1. 左右轮对命令的响应方向和相对量级正确。
2. 差速调节不会出现明显反向。

触达文件：

- `new/code/legacy/motor_logic.cpp`
- `new/code/platform/motor_adapter.cpp`
- `new/code/runtime/control_loop.cpp`

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-b-b2-half-load.log SMOKE_ENABLE_MOTOR=1 SMOKE_AUTO_START=1 SMOKE_AUTO_START_DELAY_MS=200 SMOKE_MAX_FRAMES=120 ./run_remote_smoke.sh)
```

预期 marker：

- `main.harness_context`
- `main.frame.processed`
- `motion.phase.transition`
- `main.frame.processed`
- `motion.spinup.enter`
- `motion.spinup.complete`
- `control.apply.drive`
- `control.apply.command`
- `motion.stop.complete`

证据文件：

- `phase-b-b2-half-load.log`
- `new/verification/phase-b-b2-half-load.md`
- `phase-b-half-load.mp4`

失败回退方向：

- 电机方向或混控方向异常：回 Phase A

### B-3 低速直线闭环

目标：
验证车辆已经能在真实地面上以低速进入可解释的基础闭环。

任务：

1. 在简单直线路面低速前进。
2. 观察车辆是否能维持可接受方向。
3. 校验 IMU、编码器、感知误差、输出命令之间的关系。
4. 修正：
   - IMU 符号或比例
   - 编码器比例
   - 速度目标
   - PID 参数
5. 把起步阶段单独留证，确认当前 `new/` 的发车行为至少达到 `old_2` 同等级别的“可解释、可复现、不过冲”。

验收标准：

1. 车辆能低速直线前进，不出现立刻蛇形或失控。
2. 车辆在停止命令下可以快速回到安全状态。

触达文件：

- `new/code/runtime/control_loop.cpp`
- `new/code/legacy/pid_control.cpp`
- `new/code/platform/imu_adapter.cpp`
- `new/code/platform/encoder_adapter.cpp`

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-b-b3-straight-run.log SMOKE_ENABLE_MOTOR=1 SMOKE_AUTO_START=1 SMOKE_AUTO_START_DELAY_MS=200 SMOKE_MAX_FRAMES=180 ./run_remote_smoke.sh)
```

预期 marker：

- `main.frame.processed`
- `motion.spinup.enter`
- `motion.spinup.complete`
- `control.apply.drive`
- `control.apply.command`
- `imu.sample.summary`
- `encoder.delta.summary`
- `motion.stop.complete`

证据文件：

- `phase-b-b3-straight-run.log`
- `new/verification/phase-b-b3-straight-run.md`
- `phase-b-straight-run.mp4`
- `phase-b-straight-tuning.md`

失败回退方向：

- 关键传感器或控制解锁异常：回 Phase A
- 低速运动异常但基础链路正常：留在 Phase B

### B-4 基础弯道与回正

目标：
验证车辆对轻微横向偏差已经具备可解释的修正能力。

任务：

1. 在轻微弯道或人工标线条件下做左右修正测试。
2. 观察 `turn_output` 这个差速调节量对左右轮转向效果的影响是否合理。
3. 验证从偏移到回正的过程是否连续。
4. 如果出现振荡，调整：
   - 速度目标
   - 相机误差滤波
   - IMU/gyro 相关参数
5. 预留后续与 `old_2/project/code/isr.cpp` 场景速度调度对齐的验证口径，避免将来一接入语义状态就推翻低速阶段结论。

验收标准：

1. 能在轻弯中完成可解释的修正。
2. 不出现持续大幅振荡。

触达文件：

- `new/code/runtime/control_loop.cpp`
- `new/code/legacy/pid_control.cpp`
- `new/code/runtime/perception_frontend.cpp`

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-b-b4-turn-run.log SMOKE_ENABLE_MOTOR=1 SMOKE_AUTO_START=1 SMOKE_AUTO_START_DELAY_MS=200 SMOKE_MAX_FRAMES=220 ./run_remote_smoke.sh)
```

预期 marker：

- `motion.spinup.enter`
- `motion.spinup.complete`
- `control.apply.command`
- `encoder.delta.summary`
- `motion.stop.requested`
- `motion.stop.complete`

证据文件：

- `phase-b-b4-turn-run.log`
- `new/verification/phase-b-b4-turn-run.md`
- `phase-b-turn-run.mp4`
- `phase-b-turn-analysis.md`

失败回退方向：

- 振荡或回正异常：留在 Phase B
- 若追溯到硬件闭环基础异常：回 Phase A

### B-5 异常工况实车验证

目标：
确认实车运行时出现短时异常会优先回到 fail-safe，而不是继续乱动。

任务：

1. 模拟短时丢帧。
2. 模拟短时 IMU 无效。
3. 模拟短时编码器异常。
4. 模拟低压或低压强制路径。
5. 验证系统是否回到 fail-safe，而不是继续乱动。

验收标准：

1. 异常时优先停机或进入安全输出。
2. 异常解除后可以重新启动，不留下脏状态。

触达文件：

- `new/code/runtime/control_loop.cpp`
- `new/code/runtime/perception_frontend.cpp`
- `new/code/platform/power_adapter.cpp`
- `new/code/runtime/startup.cpp`

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-b-b5-drop-frame.log SMOKE_ENABLE_MOTOR=1 SMOKE_AUTO_START=1 SMOKE_AUTO_START_DELAY_MS=200 SMOKE_AUTO_RESET_FAULT=1 SMOKE_FAULT_INJECT_DROP_FRAME_EVERY_N=5 SMOKE_MAX_FRAMES=120 ./run_remote_smoke.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-b-b5-imu-invalid.log SMOKE_ENABLE_MOTOR=1 SMOKE_AUTO_START=1 SMOKE_AUTO_START_DELAY_MS=200 SMOKE_AUTO_RESET_FAULT=1 SMOKE_FAULT_INJECT_IMU_INVALID_EVERY_N=10 SMOKE_MAX_FRAMES=120 ./run_remote_smoke.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-b-b5-encoder-invalid.log SMOKE_ENABLE_MOTOR=1 SMOKE_AUTO_START=1 SMOKE_AUTO_START_DELAY_MS=200 SMOKE_AUTO_RESET_FAULT=1 SMOKE_FAULT_INJECT_ENCODER_INVALID_EVERY_N=7 SMOKE_MAX_FRAMES=120 ./run_remote_smoke.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-b-b5-low-voltage.log SMOKE_ENABLE_MOTOR=1 SMOKE_AUTO_START=1 SMOKE_AUTO_START_DELAY_MS=200 SMOKE_AUTO_RESET_FAULT=1 SMOKE_FORCE_LOW_VOLTAGE=true SMOKE_MAX_FRAMES=120 ./run_remote_smoke.sh)
```

预期 marker：

- `main.harness_context`
- `main.frame.processed`
- `perception.inject.drop_frame` / `imu.inject.invalid` / `encoder.inject.invalid` / `power.low_voltage.injected`
- `control.veto.*`
- `motion.failsafe.latched`
- `motion.failsafe.reset_ready`
- `motion.failsafe.reset_requested`
- `motion.failsafe.rearmed`

证据文件：

- `new/verification/phase-b-b5-fault-injection.md`
- `phase-b-b5-drop-frame.log`
- `phase-b-b5-imu-invalid.log`
- `phase-b-b5-encoder-invalid.log`
- `phase-b-b5-low-voltage.log`

失败回退方向：

- fail-safe 或恢复流程异常：回 Phase A
- 实车操作边界异常：留在 Phase B

## 3. Phase B 必须产出的证据

1. `START_REQUESTED -> SPINUP -> RUNNING -> STOPPING -> DISARMED` 的完整直线或半载证据
2. 轻弯修正视频和对应 lifecycle 日志
3. `FAIL_SAFE_LATCHED` 与 re-arm 记录
4. 当前可用参数集的版本记录
5. 测试操作步骤记录：上电、启动、测试、停车、复位
6. 每条证据对应的 harness context block 或显式 signal 操作记录

## 4. Phase B 退出条件

只有同时满足以下条件，才允许进入 Phase C：

1. 车辆低速可动，且行为可解释
2. 车辆可安全停机
3. 轻微转向修正已可工作
4. 异常注入能够回到 fail-safe
5. 已形成一套可重复启动和测试的低速参数集

## 5. 本阶段完成后的结构性结论

Phase B 通过后，项目必须能够回答：

1. 车是否真的会按控制输出运动，而不是只在日志里“看起来正确”。
2. 车是否已经具备“可重复低速测试”的安全边界。
3. 后续的主矛盾是否已经转移到“稳不稳、聪不聪明”，而不是“能不能动”。

## 6. 当前实现状态（截至 2026-04-19）

当前已经成立：

1. `new/code/runtime/motion_supervisor.*`、`control_loop.cpp`、`new/user/main.cpp` 与 `run_remote_smoke.sh` 已经把 Phase B start / spinup / running / stop / fail-safe / re-arm 的 contract 落到 accepted runtime entrypoint 上。
2. `SMOKE_MAX_FRAMES` 的 accepted 语义已经固定为 wrapper-owned bounded run budget；wrapper 观察 `main.frame.processed` 后发送 `SIGINT` 请求 controlled stop，而不是把帧数退出语义继续留在产品 runtime 内。
3. `checkpoint-4` 的 source-first review 已在 `openspec/changes/implement-phase-b-motion-lifecycle/verification/checkpoint-4/attempt-3/` 通过；stop-exit 修复后的 diagnostics-only 与 motor-enabled rerun 都已经出现 `motion.stop.complete` 和 `main.exit.ready`，不再依赖 `main.exit.forced` 作为正常退出路径。

当前仍未成立：

1. `B-1` 到 `B-5` 的板端/实车 evidence bundle 还没有形成完整可验收包。
2. `Phase B 退出条件` 中要求的轻弯修正、fault injection recovery、参数集固化，仍然需要以实测证据而不是 source review 结论来关闭。

当前执行入口：

- `new/verification/phase-b-board-test-checklist.md` 是 `B-1` 到 `B-5` 的持久化总测试文档；后续板测默认先更新这份 checklist，再分别回填各子 note。
