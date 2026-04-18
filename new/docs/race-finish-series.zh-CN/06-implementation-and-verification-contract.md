# Phase Support：实现、验证与扩展契约

## 1. 文档定位

这份附录不是新阶段，而是整个 `race-finish-series.zh-CN` 的实现与验证硬契约。

如果后续接手者没有当前对话上下文，只要先读：

1. `README.md`
2. `00-master-roadmap.md`
3. 本文件
4. 当前要执行的阶段文档

就应能继续推进，不必再依赖已归档文档。

## 2. 无上下文接手规则

后续 AI 或开发者接手时，默认执行规则如下：

1. 先确认当前目标属于哪个阶段。
2. 再确认要改的是：
   - 运行时主线
   - 硬件适配
   - 感知/语义
   - 参数/运维
3. 改动前必须定位触达文件。
4. 改动后必须同时留下：
   - 代码或配置改动
   - 运行/测试命令
   - 证据日志或视频
   - 结论与是否通过该阶段

如果某次更改无法同时给出代码与证据，就不能宣称该任务完成。

## 3. 实现边界契约

### 3.1 工作区边界

实现边界固定如下：

1. `new/` 是唯一有效迁移工作区。
2. `new/user/` 负责构建、部署和运行入口。
3. `new/code/legacy/` 负责保留和改写旧算法逻辑。
4. `new/code/platform/` 负责 project-owned adapter。
5. `new/code/platform/true_ls2k0300/` 负责 vendor bridge。
6. `new/code/runtime/` 负责 startup、perception、control、shutdown。
7. `new/config/` 负责参数和 hardware profile。
8. `new/docs/race-finish-series.zh-CN/` 是当前主执行文档集。
9. `old/` 与 `old_2/` 都是迁移证据源，不是当前运行时的实现落点。

新业务逻辑不得重新散回 vendor 示例目录。

### 3.2 构建边界

当前实现默认以下事实：

1. `new/user/build.sh`
2. `new/user/CMakeLists.txt`
3. `new/user/run_remote_smoke.sh`

都必须继续作为主构建和 smoke 入口，而不是另起一套顶层构建系统。

### 3.3 Vendor 隔离边界

以下内容只能留在 `new/code/platform/true_ls2k0300/`：

1. vendor 头文件
2. vendor 全局变量
3. vendor free function
4. vendor C/C++ 类型
5. vendor 设备节点和路径细节

以下层不得直接碰 vendor 细节：

1. `new/code/legacy/`
2. `new/code/runtime/`
3. `new/code/port/` 的公共类型边界

对 legacy/runtime 的硬性禁止项包括：

1. `zf_common_headfile.hpp`
2. `zf_driver_*`
3. `zf_device_*`
4. 直接 `pwm_set_duty`
5. 直接 `encoder_get_count`
6. 直接 `mt9v03x_*`
7. 直接 `mpu6050_*`
8. TC264 中断宏、pin 宏或双核假设

### 3.4 Legacy 保留与递延范围

当前必须继续承认以下映射结论：

1. `old/code/FUZZY_PID_UCAS.c/.h` 的 `InitMH`、`DuoJiGetP`、`P_Mode` 相关转向表契约属于保留范围，不能被隐式删除。
2. `old/code/PID.c` 的 camera+gyro 控制思路保留在 `new/code/legacy/pid_control.cpp`。
3. `old/code/Motor.c` 的混控逻辑保留在 `new/code/legacy/motor_logic.cpp`，硬件写入移到 adapter。
4. `old/code/ZiTaiJieSuan.c` 的姿态更新依赖保留在 `new/code/legacy/attitude_logic.cpp`。
5. `old/code/camera.c` 的基础阈值/循迹核心仅保留其可解释部分，不视为最终赛道算法。
6. `old_2/project/code/camera.cpp` 提供的中线生成、丢线统计、角点/十字/斑马线/环岛/障碍规则链，属于 Phase C 的高价值迁移证据，可拆分吸收进 project-owned legacy/strategy 层。
7. `old_2/project/code/isr.cpp` 与 `old_2/project/user/main.cpp` 提供的发车、缓启动、刹停、周期组织和状态调度语义，属于 Phase B/Phase D 的高价值迁移证据。
8. `old_2/project/code/motor.cpp` 主要提供速度 PID、差速与转向控制语义，应单独作为控制层参考，而不是被混同成启动/停车生命周期实现。
9. `old_2/project/code/key.cpp`、`old_2/project/code/save.cpp` 提供的现场调参和持久化 workflow 只可借鉴其交互目标，不可直接把旧文件格式、旧菜单状态或旧显示耦合拷入主线契约。

以下范围明确递延，不允许被误当成“已经自动具备”：

1. `old/code/Servo.c`
2. `old/code/All_init_core1.c`
3. `old/user/cpu1_main.c`
4. 双核执行语义
5. 嵌套中断语义
6. Flash/TFT 交互式调参模式

### 3.4A `old_2` 吸收边界

`old_2/` 的使用规则必须单独说清，否则很容易在“历史上跑过”与“当前应该怎么落地”之间混淆。

允许吸收的内容：

1. 算法和规则链语义
2. project-owned 状态机含义
3. 发车、缓启动、刹停、参数切换等运行流程目标
4. 用于验证当前设计是否遗漏中间状态的字段和条件

禁止直接泄漏的内容：

1. `old_2` 的 Linux 设备访问、线程封装和厂库式 API
2. `old_2` 的 vendor-like 全局变量和目录内直接耦合
3. `old_2/project/out/*` 等构建产物
4. 把 `old_2` 目录直接当成新的 `legacy` 运行时

也就是说：

1. `old/` 主要回答“最初算法想表达什么”。
2. `old_2/` 主要回答“LS2K 时代真实跑车时保留了哪些运行语义和赛道规则”。
3. `new/` 负责在当前边界下重新实现这些结论。

### 3.5 显式初始化状态

以下旧全局/时序依赖必须继续视为“显式初始化状态”，不允许重新退化成隐式行为：

1. `W_Target_last`
2. `bcount`
3. `circle_find`
4. `zebra_flag`
5. `cross_flag`

它们属于 `runtime_state` 或后续 project-owned state object 的责任，不属于 vendor 或 legacy 隐式全局。

## 4. Hardware Profile 契约

### 4.1 当前 profile 是强约束输入

`new/config/hardware_profile.json` 不是参考件，而是正式运行输入。

当前 profile 至少必须有以下 block：

1. `camera`
2. `imu`
3. `encoder`
4. `motor`
5. `timer`
6. `persistence`
7. `display`

缺少 block、JSON 非法、mode 非法、hook 非法，都属于 fail-closed。

### 4.2 Mode 语义

每个 subsystem 的 `mode` 只能是：

1. `direct-match`
2. `adaptation-hook`
3. `disabled`

其意义固定如下：

1. `direct-match`：当前阶段主线路径
2. `adaptation-hook`：显式扩展路径，只能带着 marker 和证据存在
3. `disabled`：当前配置不启用该设备

禁止 silent fallback。

### 4.3 当前默认 subsystem 契约

| Subsystem | 默认 mode | 当前 hook | 主线行为 |
|---|---|---|---|
| `camera` | `direct-match` | `phase1-160x120-to-160x128-no-exp-light-control` | 接受 `160x120`，按固定 legacy 行映射适配成 `160x128`；非默认 `exp_light` 在 direct-match 下 fail-closed |
| `imu` | `direct-match` | `imu-core-device-detect` | 通过真 baseline 的 IMU core 路径检测设备并输出 project-owned sample |
| `encoder` | `direct-match` | `absolute-count-delta-baseline` | 读取绝对计数，先建 baseline，再导出 delta |
| `motor` | `direct-match` | `pwm-gpio-free-function` | 通过 bridge 封装后的 PWM/GPIO 路径输出左右轮命令 |
| `timer` | `direct-match` | `pit-timer-bridge` | 使用 true LS2K0300 PIT bridge 驱动周期控制 |
| `persistence` | `direct-match` | `json-file-store` | 参数与 startup-critical 校验 |
| `display` | `disabled` | `phase1-deferred` | 不属于当前比赛主线 |

### 4.4 启动时的 profile 规则

当前阶段必须遵守：

1. `camera/imu/encoder/motor` 在正常比赛路径下必须是 `direct-match`。
2. `timer` 不能缺失或被禁用。
3. `persistence` 不能缺失、禁用或走 `adaptation-hook`。
4. `adaptation-hook` 只允许作为显式诊断或扩展边界。
5. `LS2K_ALLOW_DEGRADED_STARTUP=1` 只能用于诊断，不是比赛部署。

### 4.4A 相机 frame 适配硬契约

当前 phase-1 相机适配不是“把任意图塞进 `160x128`”。

后续接手者必须保留以下硬契约，除非显式更新设计和验证口径：

1. 源 frame 只接受 `160x120`
2. source rows `0..119` -> destination rows `8..127`
3. destination rows `0..7` 重复 source row `0`
4. 不允许静默改成：
   - 裁掉顶部或底部
   - 缩放到 `160x128`
   - 居中拷贝
   - 任意补黑边

这个映射存在的原因不是格式好看，而是为了保留当前 legacy camera logic 依赖的行语义。

### 4.5 新增硬件的 profile 扩展规则

如果未来引入新硬件，例如距离传感器：

1. 必须新增显式 profile block，例如 `distance`。
2. 不允许把距离传感器语义偷偷塞进现有 `camera` 或 `imu` block。
3. 新 block 也必须声明 `mode + hook`。
4. 初期即使只是实验，也必须明确是：
   - `direct-match`
   - `adaptation-hook`
   - `disabled`

也就是说，新增硬件的第一步不是改控制逻辑，而是先成为 `hardware_profile` 中可审计的显式子系统。

## 5. 参数契约

### 5.1 当前必须存在的参数字段

当前 phase-1 参数面至少包括：

1. `Speed_base`
2. `JWJC`
3. `circle_k`
4. `circle_b`
5. `road_k`
6. `road_b`
7. `see_max`
8. `PID_TURN_CAMERA.D`
9. `PID_TURN_GYRO_CAMERA.D`
10. `Straight_permit`
11. `island_point`
12. `island_delay`
13. `circle_k_err`
14. `P_Mode`
15. `exp_light`

当前还支持以下附加运行策略字段：

1. `emergency_threshold`
2. `control_period_ms`
3. `perception_stale_ms`
4. `pwm_limit`

### 5.2 启动关键字段

`P_Mode` 和 `exp_light` 属于 startup-critical 参数。

它们必须在以下动作之前应用：

1. fuzzy table 初始化
2. 相机相关 bring-up
3. 执行器解锁

### 5.3 参数失败行为

当前参数契约必须明确保留：

1. 文件缺失：记录 marker，使用文档化 defaults
2. JSON 非法：记录 fail-safe marker，使用 defaults
3. 必填字段缺失或类型错误：记录 fail-safe marker，使用 defaults
4. startup-critical 字段非法：不允许执行器解锁

因此，后续任何参数扩展都必须同步写清：

1. 是否必填
2. 默认值
3. 失败时是 default 还是 stop
4. 是否属于 startup-critical

## 6. 运行时诊断契约

### 6.1 诊断记录格式

运行时诊断默认通过 `port::DiagnosticSink` 发出。

后续读取 smoke 日志时，至少要知道每条主诊断记录都围绕以下信息理解：

1. level
   - `INFO`
   - `WARN`
   - `ERROR`
   - `FAIL_SAFE`
2. stable code
   - 供 grep、对照 marker、写观测模板使用
3. monotonic timestamp
   - 供时序、状态切换和故障发生先后关系判断使用

因此，后续文档里提到的 marker，默认指的是可在诊断输出中定位的稳定 code，而不是泛指任意内部枚举值。

### 6.2 关键 marker 清单

后续改动时，以下 marker 仍是主证据，不得丢失语义：

1. `startup.complete`
2. `params.critical.apply`
3. `startup.low_voltage.raw`
4. `startup.low_voltage.emergency`
5. `startup.mode.degraded`
6. `startup.profile.timer.hook`
7. `imu.detect`
8. `profile.*`
9. `control.start`
10. `control.start.degraded.motor_disabled`
11. `control.arm.motor_disabled`
12. `control.veto`
13. `control.veto.perception_stale`
14. `control.veto.perception_emergency`
15. `control.veto.low_voltage`
16. `control.veto.imu_invalid`
17. `control.veto.encoder_invalid`
18. `control.command.requested_nonzero`
19. `control.apply.drive`
20. `control.apply.suppressed`
21. `control.apply.emergency_stop`
22. `control.apply.failed`
23. `control.arm.transition`
24. `encoder.baseline`
25. `encoder.delta.jump`
26. `power.low_voltage.transition`
27. `shutdown.complete`
28. `main.frame_limit`

### 6.3 相机和 adaptation marker

这里必须区分两类名字：

1. `CameraGeometryMarker` 这类内部枚举值
2. `DiagnosticSink` 真正发到日志里的 stable code

相机内部 capture-state 枚举包括：

1. `kPhase1Adapted`
2. `kNonPhase1Geometry`
3. `kAdaptationHookRouted`

但当前 grep 日志时，优先看以下实际诊断 code：

1. `camera.init`
2. `camera.init.hook`
3. `camera.init.failed`
4. `startup.camera.init`
5. `camera.exposure.unsupported`
6. `camera.hook`
7. `camera.geometry.override`
8. `camera.shutdown`

这里要补一条当前实现事实：

1. `camera.geometry.override` 只对应 `LS2K_FORCE_UVC_GEOMETRY` 触发的人工验证路径
2. 真实运行时如果捕获到非 `160x120` frame，当前代码只会把内部 marker 置为 `kNonPhase1Geometry` 并拒绝发布帧
3. 也就是说，真实几何不匹配当前没有单独 grep-facing stable code
4. 因此，后续取证必须区分：
   - 人工强制覆盖路径：看 `camera.geometry.override`
   - 真实运行拒绝路径：看 frame 未发布、后续 `control.veto` 与内部几何 marker 语义

非相机 adaptation marker：

1. `imu.hook.read`
2. `encoder.hook.read`
3. `motor.hook.apply`

如果未来距离传感器引入 adaptation path，也应使用同一风格，例如：

1. `distance.init.hook`
2. `distance.hook.read`
3. `distance.read.invalid`

如果未来策略层开始承接赛道元素、图片识别或距离判定，当前系列保留以下 grep-facing stable code 作为优先命名约定：

1. `strategy.track_element.transition`
2. `semantic.image.decision`
3. `semantic.distance.decision`

这些 code 在对应任务真正接入运行时之前，只是保留命名目标，不代表当前基线已经会发出它们。

因此，后续如果文档要写“某个 marker 是否应出现在日志里”，必须明确它属于：

1. 内部枚举
2. 还是实际 diagnostic code

不能把两者混写。

### 6.4 低压 emergency 链

低压链路是正式控制 veto 来源，不是附属逻辑。

必须继续保证：

1. startup/runtime 都有低压采样
2. startup 无法采样时严格模式直接 fail-closed
3. runtime 无法采样时强制 emergency veto
4. `power.low_voltage.transition` 能表达状态切换
5. 控制路径会把低压传递为 `control.veto` / `control.veto.low_voltage` 与 emergency-stop

### 6.5 烟雾测试与运行寿命

当前运行时契约仍然是：

1. 产品默认是长运行进程
2. `LS2K_MAX_FRAMES` 只用于 smoke 或受控验证
3. `SIGINT` / `SIGTERM` 走 graceful shutdown
4. 高频故障 marker 可以被 rate-limit，避免扭曲时序

## 7. 标准执行命令与验证开关

### 7.1 基础命令

默认命令入口保持为：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-runtime-smoke.log ./run_remote_smoke.sh)
```

如果希望命令块可直接复制执行，推荐统一写成：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-runtime-smoke.log ./run_remote_smoke.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-runtime-drive.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
```

### 7.2 常用验证开关

当前需要继续记住以下验证开关：

1. `SMOKE_MAX_FRAMES`
2. `SMOKE_ENABLE_MOTOR`
3. `LS2K_MAX_FRAMES`
4. `LS2K_ALLOW_DEGRADED_STARTUP`
5. `LS2K_FORCE_UVC_GEOMETRY`
6. `LS2K_FORCE_LOW_VOLTAGE`
7. `LS2K_LOW_VOLTAGE_RAW_THRESHOLD`
8. `LS2K_LOW_VOLTAGE_RAW_PATH`

其中：

1. `SMOKE_MAX_FRAMES` 是 `run_remote_smoke.sh` 的外层帧数控制开关。
2. `run_remote_smoke.sh` 默认把 `motor` 切到 disabled 的 smoke profile，并自动带 `LS2K_ALLOW_DEGRADED_STARTUP=1`，避免默认 smoke 触发真实电机出力。
3. 只有显式设置 `SMOKE_ENABLE_MOTOR=1`，`run_remote_smoke.sh` 才会按给定 profile 运行真实电机路径。
4. `run_remote_smoke.sh` 会把 `SMOKE_MAX_FRAMES` 传递给 runtime 的 `LS2K_MAX_FRAMES`。
5. 直接运行 `../out/new` 时，才应直接设置 `LS2K_MAX_FRAMES`。

这些开关只能用于验证和诊断，不得被当成比赛正式配置的一部分。

### 7.3 日志与证据落点

当存在 active OpenSpec change 时：

1. review 和证据应优先组织到该 change 的 `verification/`
2. 结论性 review 也落到该 change 的 `verification/`

但必须注意：

1. `new/user/run_remote_smoke.sh` 当前实现里的默认 `VERIFY_LOG` 不是自动跟随 active change 推导
2. 它默认仍写到固定路径：
   - `openspec/changes/fix-board-runtime-hardware-detection/verification/runtime-smoke.log`
3. 如果当前验证对象不是这个历史 change，必须显式传入：
   - `VERIFY_LOG_PATH=<target log path>`
4. `run_remote_smoke.sh` 当前不会因为远程失败自动执行本地 fallback
5. 只有显式设置 `SMOKE_LOCAL_ONLY=1` 时，脚本才走 `run_local`

也就是说：

1. “应当如何组织证据”与
2. “脚本当前默认写到哪里”

是两件不同的事，后续取证时必须把两者对齐。

如果某个阶段任务明确要求产出 `phase-*.log` 这类 phase-scoped 证据文件，那么命令块里必须显式写出其生成方式：

1. diagnostics-only smoke 入口：`VERIFY_LOG_PATH=<target> ./run_remote_smoke.sh`
2. 需要真实电机出力的 smoke：`VERIFY_LOG_PATH=<target> SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh`
3. 直接运行二进制：`../out/new > <target> 2>&1` 或 `>> <target> 2>&1`

关于旧文档中“远程失败后自动本地回退”的描述，视为已经被本节替代，不再作为现行契约。

如果不存在 active change，则至少要建立一个可追溯的 phase 证据目录，包含：

1. 运行日志
2. 参数版本
3. profile 副本
4. 测试视频或其索引
5. 结论摘要

不能只留下口头结论。

### 7.4 各阶段建议证据包命名

如果当前没有更具体的 change-level 命名约定，可按阶段使用以下建议名称：

1. Phase A
   - `phase-a-runtime-smoke.log`
   - `phase-a-actuator-topology.log`
   - `phase-a-imu-direct-match.log`
   - `phase-a-hardware-profile-compat.log`
   - `phase-a-control-unveto.log`
   - `phase-a-encoder-samples.log`
   - `phase-a-camera-exposure.log`
   - `phase-a-encoder-calibration.md`
   - `phase-a-actuator-topology.md`
   - `phase-a-motor-output.md`
   - `phase-a-motor-output.log`
   - `phase-a-camera-exposure.md`
   - `phase-a-hardware-profile-compat.md`
   - `phase-a-summary.md`
2. Phase B
   - `phase-b-start-stop.log`
   - `phase-b-half-load.log`
   - `phase-b-straight-run.log`
   - `phase-b-straight-run.mp4`
   - `phase-b-turn-run.log`
   - `phase-b-turn-run.mp4`
   - `phase-b-fault-injection.log`
   - `phase-b-fault-injection.md`
   - `phase-b-params.json`
   - `phase-b-summary.md`
3. Phase C
   - `phase-c-vision-smoke.log`
   - `phase-c-track-elements.log`
   - `phase-c-vision-before-after.md`
   - `phase-c-dataset-index.md`
   - `phase-c-strategy-interface.md`
   - `phase-c-image-semantics.md`
   - `phase-c-distance-semantics.md`
   - `phase-c-strategy-mapping.md`
   - `phase-c-summary.md`
4. Phase D
   - `phase-d-parameter-matrix.md`
   - `phase-d-parameter-failure.log`
   - `phase-d-parameter-failure.md`
   - `phase-d-ops-drill.log`
   - `phase-d-log-example.log`
   - `phase-d-ops-checklist.md`
   - `phase-d-marker-template.md`
   - `phase-d-track-record.log`
   - `phase-d-track-record.md`
   - `phase-d-performance.md`
   - `phase-d-summary.md`
   - `phase-d-latency-samples.log`
5. Phase E
   - `phase-e-end-to-end.md`
   - `phase-e-end-to-end.log`
   - `phase-e-repeatability.md`
   - `phase-e-repeatability.log`
   - `phase-e-fallback.md`
   - `phase-e-fallback.log`
   - `phase-e-final-gate.md`

这些文件名不是强制唯一标准，但至少应保持：

1. 一看文件名就知道对应阶段
2. 一看文件名就知道证据类型
3. 不同运行批次可以通过日期或批次号扩展

### 7.5 常用诊断命令示例

用于取几何、低压和降级路径证据时，至少可以使用以下形式：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-runtime-smoke.log ./run_remote_smoke.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-runtime-drive.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
sed '/"imu": {/,/},/s/"direct-match"/"adaptation-hook"/; /"imu": {/,/},/s/"imu-core-device-detect"/"imu-diagnostic-hook"/' new/config/hardware_profile.json >/tmp/ls2k-hardware-profile-imu-hook.json
(cd new/user && LS2K_ALLOW_DEGRADED_STARTUP=1 LS2K_PROFILE_PATH=/tmp/ls2k-hardware-profile-imu-hook.json LS2K_PARAMS_PATH=../config/default_params.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-runtime-smoke.log 2>&1)
rm -f /tmp/ls2k-params-missing.json
printf '{ invalid json }\n' >/tmp/ls2k-params-malformed.json
sed '/"P_Mode":/d' new/config/default_params.json >/tmp/ls2k-params-missing-field.json
sed 's/"P_Mode": 3/"P_Mode": 99/' new/config/default_params.json >/tmp/ls2k-params-invalid-critical.json
sed 's/"exp_light": 65/"exp_light": 66/' new/config/default_params.json >/tmp/ls2k-params-valid-nondefault-exposure.json
sed 's/"exp_light": 65/"exp_light": 9999/' new/config/default_params.json >/tmp/ls2k-params-invalid-exposure.json
(cd new/user && LS2K_PARAMS_PATH=/tmp/ls2k-params-missing.json LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-runtime-smoke.log 2>&1)
(cd new/user && LS2K_PARAMS_PATH=/tmp/ls2k-params-malformed.json LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-runtime-smoke.log 2>&1)
(cd new/user && LS2K_PARAMS_PATH=/tmp/ls2k-params-missing-field.json LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-runtime-smoke.log 2>&1)
(cd new/user && LS2K_PARAMS_PATH=/tmp/ls2k-params-invalid-critical.json LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-runtime-smoke.log 2>&1)
(cd new/user && LS2K_PARAMS_PATH=/tmp/ls2k-params-valid-nondefault-exposure.json LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-runtime-smoke.log 2>&1)
(cd new/user && LS2K_PARAMS_PATH=/tmp/ls2k-params-invalid-exposure.json LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-runtime-smoke.log 2>&1)
(cd new/user && LS2K_FORCE_UVC_GEOMETRY=320x240 LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_PARAMS_PATH=../config/default_params.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-runtime-smoke.log 2>&1)
(cd new/user && LS2K_ALLOW_DEGRADED_STARTUP=1 LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_PARAMS_PATH=../config/default_params.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-runtime-smoke.log 2>&1)
(cd new/user && LS2K_FORCE_LOW_VOLTAGE=true LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_PARAMS_PATH=../config/default_params.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-runtime-smoke.log 2>&1)
```

具体命令以当前环境可执行入口为准，但证据中必须写明实际执行命令。

## 8. Phase 执行模板

后续每个阶段任务都按同一模板执行：

1. 目标
2. 触达文件
3. 运行命令
4. 预期 marker
5. 证据文件
6. 通过条件
7. 失败回退方向

如果某任务描述里缺少这七项，应补齐后再实施。

## 9. 统一语义/策略解耦契约

### 9.1 当前问题

当前代码的 adapter 解耦已经成立，但控制决策层仍不够解耦：

1. `PerceptionResult` 仍偏向相机循迹结果
2. `PerceptionFrontend` 目前主要是 `camera + low voltage`
3. `ControlLoop` 直接消费 `perception + imu + encoder`

这意味着：

1. 新增图像识别模型不会足够顺滑
2. 新增距离传感器也不会足够顺滑

更具体地说，当前控制 veto 直接建立在以下输入之上：

1. `PerceptionResult.published`
2. `PerceptionResult.fresh`
3. `PerceptionResult.emergency_veto`
4. low-voltage emergency 结论
5. `ImuSample.valid`
6. `EncoderDelta.valid`

### 9.2 未来统一输入分层

后续必须把“基础循迹”“图片识别”“距离传感器判定”统一放到分层结构中：

1. 原始传感器层
   - `CameraCapture`
   - `ImuSample`
   - `EncoderDelta`
   - `DistanceSample`
2. 基础感知层
   - 输出循迹误差、边界、置信度、几何合法性
3. 语义判定层
   - 图片识别结果
   - 距离传感器触发的特定场景判定
4. 策略层
   - 汇总基础感知与语义判定
   - 输出速度目标、状态切换、临时方向建议、等待/减速建议
   - 输出上层 allow-motion / fail-safe veto 结论
5. 控制层
   - 只消费 project-owned `StrategyDecision`
   - 不直接理解图片模型细节或距离硬件细节

### 9.2A 最小 `StrategyInput` / `StrategyDecision` 契约

为了保证后续接手者在无上下文时不会把解耦做歪，最低限度按下面的 ownership 推进：

1. `StrategyInput` 至少包含：
   - 基础循迹结果
   - 图片识别结果
   - 未来距离判定结果
   - `perception` 的 freshness / published / veto 摘要
   - `imu.valid`
   - `encoder.valid`
   - low-voltage emergency 摘要
2. `StrategyDecision` 至少包含：
   - 目标速度或速度修正
   - 转向或路线建议
   - 状态机迁移
   - `allow_motion`
   - `fail_safe_reason` 或等价可审计字段
3. `ControlLoop` 完成迁移后应只负责：
   - 消费 `StrategyDecision`
   - 执行 PID / 混控 / actuator apply
   - 把 `emergency_stop` 落地到执行器
4. 图片识别与未来距离传感器都只能影响 `StrategyDecision`，不能直接向 `ControlLoop` 塞私有特例分支
5. 新增语义输入时，如果发现还需要在 `ControlLoop` 里直接判断某个模型类别或某个距离阈值，说明解耦失败，必须先补 strategy 契约

### 9.3 图像识别与距离传感器的定位

如果未来距离传感器的用途是：

1. 判定特殊场景
2. 触发减速或停车
3. 触发方向/状态切换

那么它在架构上的定位应更接近“语义判定源”，而不是新的 PID 主反馈量。

也就是说：

1. 原始距离读数属于 adapter 输入
2. 高层距离判定属于 semantic/strategy 输入
3. 其结果应通过 `StrategyDecision` 影响方向、速度或状态机
4. 不应把距离传感器逻辑直接焊死进现有 `camera_logic.cpp` 或 `control_loop.cpp`

### 9.4 新增硬件的落地顺序

以后新增图像识别或距离传感器时，顺序固定如下：

1. 在 `hardware_profile` 里声明新子系统
2. 补 adapter/bridge/raw sample
3. 定义 project-owned 高层判定结果
4. 定义 `StrategyInput` / `StrategyDecision`
5. 让 `ControlLoop` 只消费策略结果
6. 最后再做算法调优

如果跳过第 3 到第 5 步，解耦性会再次被破坏。

## 10. 这份附录解决什么问题

从现在开始，这个系列不只是“路线图”，还必须能够回答：

1. 要改哪些文件
2. 哪些边界不能破坏
3. 要跑什么命令
4. 要看哪些 marker
5. 证据应该长什么样
6. 新增图像识别或距离传感器时该如何解耦

如果这些问题答不上来，就说明系列文档仍不完整，必须先补文档再继续推进。
