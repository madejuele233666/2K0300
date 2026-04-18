# Phase B：低速实车闭环

阶段状态：

- 当前激活阶段

## 1. 阶段目标

让车辆在真实地面上完成低速、可控、可恢复的闭环运动。

这一阶段的目标不是“跑得快”，而是验证：

1. 车辆会动
2. 车辆按期望方向修正
3. 车辆可以安全停车
4. 异常时会进入 fail-safe

## 1A. 本阶段继承的控制与安全原则

本阶段默认以下前提已经成立：

1. Phase A 已经把关键硬件链路从诊断态推进到可控制态。
2. 低速实车阶段仍然以 fail-safe 优先，而不是以“先跑起来”为优先。
3. 本阶段验证的是方向、速度、恢复能力，不是比赛成绩。
4. 任何实车异常都必须能立刻回退到可人工接管状态。

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
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-b-start-stop.log ./run_remote_smoke.sh)
```

预期 marker：

- `startup.complete`
- `shutdown.complete`

证据文件：

- `phase-b-static-safety.md`
- `phase-b-start-stop.log`

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
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-b-half-load.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
```

预期 marker：

- `control.start`
- `control.veto`

证据文件：

- `phase-b-half-load.log`
- `phase-b-half-load.md`
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
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-b-straight-run.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
```

预期 marker：

- `control.start`
- `control.veto`
- `imu.detect`
- `encoder.baseline`

证据文件：

- `phase-b-straight-run.log`
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
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-b-turn-run.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
```

预期 marker：

- `control.start`
- `control.veto`

证据文件：

- `phase-b-turn-run.log`
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
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-b-fault-injection.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
(cd new/user && LS2K_ALLOW_DEGRADED_STARTUP=1 LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_PARAMS_PATH=../config/default_params.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-b-fault-injection.log 2>&1)
(cd new/user && LS2K_FORCE_LOW_VOLTAGE=true LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_PARAMS_PATH=../config/default_params.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-b-fault-injection.log 2>&1)
```

预期 marker：

- `control.veto`
- `startup.low_voltage.force`
- `startup.low_voltage.emergency`
- `startup.mode.degraded`

证据文件：

- `phase-b-fault-injection.md`
- `phase-b-fault-injection.log`

失败回退方向：

- fail-safe 或恢复流程异常：回 Phase A
- 实车操作边界异常：留在 Phase B

## 3. Phase B 必须产出的证据

1. 低速直线视频和对应日志
2. 轻弯修正视频和对应日志
3. 异常注入记录
4. 当前可用参数集的版本记录
5. 测试操作步骤记录：上电、启动、测试、停车、复位

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
