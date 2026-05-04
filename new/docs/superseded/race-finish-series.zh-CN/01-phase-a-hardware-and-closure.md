# Phase A：硬件闭环与控制解锁

阶段状态：

- 已于 `2026-04-17` 正式结束
- 过程复盘与泛化经验见 `01a-phase-a-retrospective-and-lessons.md`

## 1. 阶段目标

把当前系统从“板上能跑但主要停留在 fail-safe 观察模式”推进到“关键传感器有效、控制环可以在满足条件时解除 veto、执行器具备真实出力能力”。

这是后续一切实车运动和赛道工作的前提阶段。

## 2. 当前代码基线下的关键事实

1. 启动默认要求 `camera/imu/encoder/motor` 全部 direct-match，见 `new/code/runtime/startup.cpp`。
2. 控制环在 `!imu.valid`、`!encoder.valid`、感知不 fresh、低压或感知 veto 时进入 project-owned veto 判定；当前日志至少可区分 `control.veto.perception_stale`、`control.veto.perception_emergency`、`control.veto.low_voltage`、`control.veto.imu_invalid`、`control.veto.encoder_invalid`，并能观察 `control.command.requested_nonzero`、`control.apply.*`、`control.arm.transition`，见 `new/code/runtime/control_loop.cpp`。
3. 截至 `2026-04-15`，默认 direct-match profile 已经在板上跑到 `imu.init`、`imu.detect`、`startup.complete` 和 `control.start`；当前 accepted baseline 为 `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.log`、`runtime-smoke-retry-2026-04-15.exit`、`hardware-discovery-retry-2026-04-15.log`，并由同目录 `runtime-smoke-execution-evidence.md` 说明 supersession 关系，因此“IMU 是否能 direct-match 启动”不再是未知项。
4. `2026-04-15` 的执行器拓扑与控制观测重跑也已归档到 `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/runtime-smoke-2026-04-15.{log,exit,md}`，其中确认了首周期的 `control.veto.perception_stale` 与 `control.apply.emergency_stop`，以及后续的 `control.command.requested_nonzero`、`control.apply.drive` 和 `control.arm.transition`。
5. 当前车辆采用纯差速转向，`new/code/legacy/motor_logic.cpp` 的左右轮差分输出方向是主执行路径。
6. 上述 direct-match 启动证据仍不能单独证明 IMU 的静止姿态解释已经闭环，也不能单独证明编码器反馈已经足够可信到可直接支撑闭环控制。
7. 当前平台不存在独立舵机，`servo_pwm` 已从 `port::ActuatorCommand` 删除，主线执行器 contract 只保留 `left_pwm + right_pwm + emergency_stop`。

## 2A. 本阶段继承的硬约束

Phase A 不是单纯的驱动修复阶段，它承接以下基础原则：

1. `new/` 是唯一有效迁移边界，不能把新逻辑散回 vendor 示例目录。
2. `legacy` 逻辑必须继续通过 adapter 输入输出工作，不能重新泄漏 TC264 直接硬件访问。
3. 唯一接受的 vendor root 是 `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library`，旧 `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library` 只作历史对照，不再作实现依据。
4. `new/user/CMakeLists.txt`、`new/user/build.sh` 与远程测试入口必须只解析到接受的 vendor baseline，不能保留旧 root 回退。
5. 从错误 baseline 得出的 `.hpp` wrapper 假设不能直接沿用，真实 baseline 下必须重新按 `.h`/真实 API 形态校验。
6. `direct-match` 是比赛主路径；`adaptation-hook` 和 `degraded startup` 只能作为诊断或扩展边界显式化手段。
7. 启动是 fail-closed 的：camera、IMU、encoder、motor 任一关键项不 ready，就不能算控制链路就绪。
8. vendor 头文件、全局符号和 vendor 类型只能留在 bridge 侧，不能穿透到 `legacy` 或 `runtime` 公共边界。
9. 板测必须留下可诊断日志，不能只看进程退出码。
10. `hardware_profile.json` 是强约束输入，缺失 block、非法 mode、未知 hook 含义都应 fail-closed。
11. `persistence` 必须保持 `direct-match`，不能通过 adaptation-hook 绕过参数与启动关键项校验。
12. 本阶段的直接验证入口仍是 `new/user/build.sh` 与 `new/user/run_remote_smoke.sh`，不能另起一套临时入口。
13. `run_remote_smoke.sh` 现已默认切到 diagnostics-only 的 `motor=disabled` smoke profile，并自动带 `LS2K_ALLOW_DEGRADED_STARTUP=1`；只有显式加 `SMOKE_ENABLE_MOTOR=1` 才允许真实电机出力。

## 3. Phase A 任务清单

### A-1 固化执行器拓扑

目标：
把“纯差速转向”从口头约束变成代码和文档约束。

触达文件：

- `new/code/port/control_types.hpp`
- `new/code/legacy/motor_logic.cpp`
- `new/docs/race-finish-series.zh-CN/00-master-roadmap.md`

任务：

1. 在文档中明确声明当前平台不依赖独立 servo。
2. 保持 `port::ActuatorCommand` 不再出现 `servo_pwm`，并同步清理文档中的 servo 主线路径暗示。
3. 把 `turn_output` 的文档语义固定为“左右轮差速调节量”，避免后续被误接为舵机角度。

验收标准：

1. 文档、控制逻辑、执行器代码都一致表述为“纯差速转向”。
2. 不再把 servo 作为主线路径的隐含依赖，public actuator contract 只表达左右轮 PWM 与 fail-safe stop 语义。

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-a-actuator-topology.log ./run_remote_smoke.sh)
```

预期 marker：

- `control.start`

证据文件：

- `phase-a-actuator-topology.log`
- `phase-a-actuator-topology.md`

失败回退方向：

- 仍存在执行器拓扑歧义：留在 Phase A

### A-2 IMU direct-match 板上闭环

目标：
让 IMU 从“已完成 direct-match 启动级验证”推进到“样本质量与控制可用性也闭环”。

现状补充：

1. `2026-04-15` 的默认 direct-match 板测已经产生 `imu.init`、`imu.detect`、`startup.complete` 与 `control.start`，accepted baseline 见 `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.{log,exit}` 与 `hardware-discovery-retry-2026-04-15.log`。
2. Phase A 在 IMU 上剩余的主问题，不再是“能否启动”，而是“样本是否可信、方向是否正确、运行中是否持续 valid”。

任务：

1. 把现有 direct-match 启动证据纳入 Phase A 证据口径，不再把 `imu.init` / `imu.detect` 视为未知项。
2. 使用启用 IMU 的目标 profile 在当前可达板上补采静止样本和短时连续运行样本。
3. 确认 IMU 是：
   - 解析到真实节点并可读
   - 且运行中可以持续产出有效样本
4. 对静止样本做基础校验：
   - 零偏范围
   - `gyro_z` 方向
   - 样本连续性
5. 如果方向或零偏不对：
   - 在桥接层修正方向
   - 增加校准或偏置处理

涉及代码：

- `new/code/platform/imu_adapter.cpp`
- `new/code/platform/true_ls2k0300/imu_bridge.cpp`
- `new/code/runtime/startup.cpp`
- `new/code/runtime/control_loop.cpp`

验收标准：

1. direct-match 启动证据已并入 Phase A，且不再因为 IMU 导致 fail-safe 退出。
2. 控制环运行时 `imu.valid` 可以持续为真。
3. 静止条件下 IMU 样本稳定，方向解释正确。

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-a-imu-closure.log ./run_remote_smoke.sh)
```

预期 marker：

- `imu.init`
- `imu.detect`
- `startup.complete`

证据文件：

- `phase-a-imu-closure.log`
- `phase-a-imu-closure.md`

失败回退方向：

- IMU 初始化、检测、方向、零偏异常：留在 Phase A

### A-3 编码器方向与速度基线

目标：
让编码器数据从“能读”变成“能用于速度闭环”。

任务：

1. 确认左右轮前进时编码器差分符号正确。
2. 校验 `encoder.baseline` 行为是否符合预期。
3. 实测低速、匀速、刹停三类工况下的计数变化。
4. 评估 `encoder.delta.jump` 阈值是否合理。
5. 建立“差分计数 -> 实际速度”的标定关系。

涉及代码：

- `new/code/platform/encoder_adapter.cpp`
- `new/code/platform/true_ls2k0300/encoder_bridge.cpp`
- `new/code/runtime/control_loop.cpp`

验收标准：

1. 左右轮方向与车辆前进方向一致。
2. 正常运行时不会频繁误触发 jump reset。
3. 控制环中的速度反馈符号和量级可解释。

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-a-encoder-closure.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
```

预期 marker：

- `encoder.baseline`
- `encoder.delta.jump`

证据文件：

- `phase-a-encoder-closure.log`
- `phase-a-encoder-closure.md`

失败回退方向：

- 编码器方向、量级、jump 阈值异常：留在 Phase A

### A-4 电机真实出力与 fail-safe 回退

目标：
让电机路径从“板级写入成功”变成“可控地出力并可安全停机”，并把 side-local 响应和后续持续控制可信度分开论证。

任务：

1. 在架空轮或安全工装条件下验证非零 PWM 输出。
2. 验证左右轮正反向差速是否正确。
3. 验证 emergency-stop 是否会立刻拉回安全状态。
4. 验证 motor write 失败后是否能保持 fail-safe。

涉及代码：

- `new/code/platform/motor_adapter.cpp`
- `new/code/platform/true_ls2k0300/motor_bridge.cpp`
- `new/code/legacy/motor_logic.cpp`
- `new/code/runtime/shutdown.cpp`

验收标准：

1. 非 veto 条件下存在真实非零输出。
2. 紧急停止路径可靠。
3. 出错后不会继续保持错误出力。

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-a-motor-closure.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
(cd new/user && LS2K_BENCH_PWM_MS=500 LS2K_BENCH_PWM_LEFT=3000 LS2K_BENCH_SETTLE_MS=120 ../out/new >> ../verification/phase-a-bench-pwm-left.log 2>&1)
(cd new/user && LS2K_BENCH_PWM_MS=500 LS2K_BENCH_PWM_RIGHT=3000 LS2K_BENCH_SETTLE_MS=120 ../out/new >> ../verification/phase-a-bench-pwm-right.log 2>&1)
```

预期 marker：

- `control.start`
- `control.veto`
- `shutdown.complete`

证据文件：

- `phase-a-motor-closure.log`
- `phase-a-motor-closure.md`
- `phase-a-bench-pwm.md`

失败回退方向：

- 电机出力、急停、fail-safe 回退异常：留在 Phase A

### A-5 控制环从长期 veto 进入可控解锁

目标：
证明 `control.veto` 不是系统的常态，而是异常保护。

任务：

1. 在 IMU、编码器、相机、ADC 均有效时运行控制环。
2. 观察是否存在持续非 veto 周期。
3. 验证 `state.actuators_armed` 的进入与退出逻辑。
4. 增加必要日志或观测点，确认：
   - veto 来自 `stale`、`perception.emergency_veto`、低压、`!imu.valid`、`!encoder.valid` 中的哪一项，并能在 `control.veto.*` 中单独辨认
   - 感知是否 fresh
   - 传感器是否 valid
   - 是否出现 `control.command.requested_nonzero`
   - 输出命令是 `control.apply.drive`、`control.apply.failed`、`control.apply.emergency_stop` 还是 `control.apply.suppressed`
   - `state.actuators_armed` 的变化是否与 `control.arm.transition` 一致

涉及代码：

- `new/code/runtime/control_loop.cpp`
- `new/code/runtime/perception_frontend.cpp`
- `new/code/legacy/pid_control.cpp`

验收标准：

1. 已能用明确 marker 证明启动 veto 区间会闭合，并进入持续的非 veto 区间。
2. 该区间内输出命令不是空命令，且可在 `control.apply.command` 中看到 project-owned 左右轮 PWM。
3. 出现异常时仍能回到 veto。

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-a-control-unveto.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
```

预期 marker：

- `control.start`
- `control.veto`
- `control.veto.*`
- `control.veto.interval.closed`
- `control.unveto.sustained`
- `control.command.requested_nonzero`
- `control.apply.drive`
- `control.apply.command`
- `control.apply.suppressed`
- `control.apply.failed`
- `control.arm.transition`

证据文件：

- `phase-a-control-unveto.log`
- `phase-a-control-unveto.md`

失败回退方向：

- 控制环长期 veto 或解锁后不稳定：留在 Phase A

### A-6 相机曝光策略定论

目标：
解决“默认曝光才能 direct-match”的问题，至少给出可执行策略。

现状补充：

1. 当前代码已经明确：direct-match 相机路径只接受默认 `exp_light=65`。
2. 非默认 `exp_light` 在 direct-match 下会告警并 fail-closed。
3. 因此本任务的剩余重点，不是继续把曝光问题留成开放项，而是把当前策略边界正式定论并留证。

任务：

1. 确认 direct-match camera path 是否支持当前主线所需的曝光控制。
2. 如果不支持：
   - 明确 `exp_light=65` 是当前 bounded mainline
   - 把非默认曝光请求界定为 adaptation path 或 diagnostics-only 路径
3. 用真实图像样本分析默认曝光下的稳定性。

涉及代码：

- `new/code/platform/camera_adapter.cpp`
- `new/code/platform/param_store.cpp`
- `new/code/runtime/perception_frontend.cpp`

验收标准：

1. 不再把曝光问题留在“以后看”的状态。
2. 当前赛道准备路径有明确且有边界的曝光策略。

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-a-camera-exposure.log ./run_remote_smoke.sh)
sed 's/"exp_light": 65/"exp_light": 66/' new/config/default_params.json >/tmp/ls2k-camera-exp-unsupported.json
(cd new/user && LS2K_PARAMS_PATH=/tmp/ls2k-camera-exp-unsupported.json LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-a-camera-exposure.log 2>&1)
(cd new/user && LS2K_FORCE_UVC_GEOMETRY=320x240 LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_PARAMS_PATH=../config/default_params.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-a-camera-exposure.log 2>&1)
```

预期 marker：

- `camera.init`
- `camera.exposure.unsupported`
- `camera.geometry.override`

证据文件：

- `phase-a-camera-exposure.log`
- `phase-a-camera-exposure.md`
- `phase-a-camera-samples.md`

失败回退方向：

- 曝光策略未定或图像质量无法支撑主线：留在 Phase A

### A-7 Hardware Profile direct-match / adaptation-hook 兼容性

目标：
把 `hardware_profile` 的 direct-match 主线和 adaptation-hook 诊断边界都做成有证据的显式行为，而不是只留在附录契约里。

现状补充：

1. direct-match / degraded startup / adaptation-hook 的实现底座已经存在。
2. 历史验证与 `2026-04-15` 的 accepted direct-match baseline 已经证明：默认 profile 可以走到 `startup.complete`，而 adaptation-hook 诊断边界也能显式产出 marker。
3. Phase A 在本项的剩余工作，不是重新定义契约，而是把当前 hook 名称和现行 profile 重新整理成 phase-scoped 证据。

任务：

1. 用当前 `direct-match` profile 跑一轮标准 runtime smoke，并引用 `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.{log,exit}` 与 `hardware-discovery-retry-2026-04-15.log` 作为现有基线。
2. 复制当前 profile，并把一个非持久化子系统改成 `adaptation-hook`。
3. 用 `LS2K_ALLOW_DEGRADED_STARTUP=1` 跑 adaptation-hook 诊断启动。
4. 记录：
   - startup 对非 direct-match 的判定
   - 对应 adapter 的 init/hook read 行为
   - 控制环是否保持 fail-safe
5. 明确哪些 adaptation-hook 只允许诊断，不允许比赛部署。

涉及代码：

- `new/config/hardware_profile.json`
- `new/code/platform/hardware_profile.cpp`
- `new/code/runtime/startup.cpp`
- `new/code/platform/imu_adapter.cpp`
- `new/code/platform/encoder_adapter.cpp`
- `new/code/platform/motor_adapter.cpp`

验收标准：

1. 至少有一轮 `direct-match` 证据。
2. 至少有一轮 `adaptation-hook` 证据。
3. 文档能说明该 hook 仅用于诊断还是已具备主线可用性。

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-a-hardware-profile-compat.log ./run_remote_smoke.sh)
sed '/"imu": {/,/},/s/"direct-match"/"adaptation-hook"/; /"imu": {/,/},/s/"imu-core-device-detect"/"imu-diagnostic-hook"/' new/config/hardware_profile.json >/tmp/ls2k-hardware-profile-imu-hook.json
(cd new/user && LS2K_ALLOW_DEGRADED_STARTUP=1 LS2K_PROFILE_PATH=/tmp/ls2k-hardware-profile-imu-hook.json LS2K_PARAMS_PATH=../config/default_params.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-a-hardware-profile-compat.log 2>&1)
```

预期 marker：

- `startup.mode.degraded`
- `startup.profile.imu.degraded`
- `imu.init.hook`
- `imu.hook.read`
- `control.veto`

证据文件：

- `phase-a-hardware-profile-compat.log`
- `phase-a-hardware-profile-compat.md`

失败回退方向：

- profile 契约、hook 语义或 fail-safe 行为不清：留在 Phase A

## 4. Phase A 必须产出的证据

1. `phase-a-imu-closure.{log,md}`
2. `phase-a-encoder-closure.{log,md}`
3. `phase-a-motor-closure.{log,md}` 与 env-gated `phase-a-bench-pwm.{left,right}.log`
4. `phase-a-control-unveto.{log,md}`
5. `phase-a-camera-exposure.{log,md}` 与 `phase-a-camera-samples.md`
6. hardware profile direct-match / adaptation-hook 兼容性记录
7. 关键 marker 对照表，至少覆盖：
   - `startup.complete`
   - `params.critical.apply`
   - `imu.init`
   - `imu.detect`
   - `startup.profile.imu.degraded`
   - `imu.init.hook`
   - `imu.hook.read`
   - `encoder.baseline`
   - `encoder.delta.jump`
   - `control.veto`
   - `control.veto.*`
   - `control.command.requested_nonzero`
   - `control.apply.drive`
   - `control.apply.suppressed`
   - `control.apply.failed`
   - `control.arm.transition`
   - `power.low_voltage.transition`
   - `shutdown.complete`

## 4A. Phase A 默认执行入口

如果当前任务没有额外说明，Phase A 的默认执行入口是：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-a-runtime-smoke.log ./run_remote_smoke.sh)
```

如果任务目标包含真实电机出力、编码器随动、或 `control.apply.drive` 的电机侧验证，则必须显式 opt-in：

```bash
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-a-runtime-smoke.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
```

必要时可配合以下验证开关：

1. `SMOKE_MAX_FRAMES`
2. `SMOKE_ENABLE_MOTOR`
3. `LS2K_ALLOW_DEGRADED_STARTUP`
4. `LS2K_FORCE_UVC_GEOMETRY`
5. `LS2K_FORCE_LOW_VOLTAGE`
6. `LS2K_LOW_VOLTAGE_RAW_THRESHOLD`
7. `LS2K_LOW_VOLTAGE_RAW_PATH`

其中：

1. `SMOKE_MAX_FRAMES` 是 `run_remote_smoke.sh` 的外层控制开关。
2. `run_remote_smoke.sh` 默认把 `motor` 切到 disabled，并自动带 `LS2K_ALLOW_DEGRADED_STARTUP=1`。
3. `SMOKE_ENABLE_MOTOR=1` 是恢复真实电机出力的显式开关。
4. `LS2K_MAX_FRAMES` 主要用于直接运行 `../out/new` 时控制 runtime 帧数。

这些开关只能用于诊断和取证，不能作为比赛主路径的前提。

## 5. Phase A 退出条件

只有同时满足以下条件，才允许进入 Phase B：

1. IMU 运行时连续性闭环通过，且静止姿态解释不再存在未解释矛盾
2. 编码器速度反馈已被证明对闭环控制可信
3. 控制环可稳定解除 veto
4. 电机输出和 emergency-stop 均可验证
5. 曝光问题已有明确落地策略

只要有一项不满足，就不能进入实车低速闭环阶段。

截至 `2026-04-17` 的 change-local 证据表明：

1. accepted baseline 仍然是 `2026-04-15` 的 direct-match 启动级证据，不再重述为当前 change 的待验证问题。
2. `phase-a-control-unveto.*` 已证明启动 `perception_stale` veto 会闭合，并进入持续非 veto 区间。
3. `phase-a-camera-exposure.*` 已把 `exp_light=65` 固化为当前 direct-match 主线策略；非默认曝光仍属于非主线路径。
4. `phase-a-bench-pwm.*` 已在默认关闭的 env-gated 测试下证明左右逻辑 PWM 能打到各自 side-local 编码器响应。
5. `phase-a-imu-static-raw-20260417.log`、`imu_ccw_live_20260417.log` 与配对 live yaw capture 已补齐 IMU 静止样本稳定性与 `gyro_z` 方向解释。
6. `runtime_smoke_motor_lowrisk_20260417.log` 已在低风险 runtime 命令下给出与逻辑指令同符号的编码器反馈，补齐了 Phase A 口径下的 encoder control-trust。
7. 因此当前 Phase A 退出条件已满足，可以进入 `Phase B`。

## 6. 本阶段完成后的结构性结论

Phase A 通过后，项目必须同时满足以下结论：

1. 比赛主路径不再依赖 degraded 模式才能观察到关键硬件。
2. 控制环的常态不再是 `control.veto`。
3. 差速转向路径在代码、文档、测试三处表达一致。
4. 下一阶段的主矛盾从“底层硬件链路”转移为“实车闭环表现”。
