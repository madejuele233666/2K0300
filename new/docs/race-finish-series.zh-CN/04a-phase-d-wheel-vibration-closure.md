# Phase D：电机高频抖动与轮速振荡问题闭环

## 1. 文档目的

固定记录这次“电机高频抖动 / 高频启停 / 悬空振荡”问题的完整排查链路、结论、代码改动和当前可复用基线。

后续如果再次遇到类似问题，优先读本文件，不要重新从旧代码或聊天记录里倒推。

## 2. 问题现象

现场先后出现过两类容易混淆的症状：

1. 电机在运行中表现为高频地“转一下、停一下、再转一下”，车体振动明显。
2. 在悬空静止条件下，车辆没有前进负载，但左右轮输出仍然持续抖动，表现为闭环振荡。

这两类症状最后确认不是同一个根因。

## 3. 最终结论

这次问题最终分成两层：

1. 第一层是真实存在的控制门控抖动：
   - 根因是 `perception_stale` 以感知采集起始时间而不是发布时间判 stale。
   - 启动阶段会出现约 `292-293ms` 的 stale/emergency-stop。
   - 修复前，这会被误判成“高频启停”。
2. 第二层是门控修复后残留的轮速闭环振荡：
   - 开环标定证明左右轮静摩擦门槛差异很大，左轮显著弱于右轮。
   - 在这种 plant 不对称前提下，原先双轮同参 `P=240 / I=10 / D=20` 过激。
   - 结果不是 gate 在反复切，而是轮速 PID 自己把 PWM 推得过猛，导致悬空条件下也会持续抖动。

因此，这次“抖动问题”的闭环不是单一改一个参数，而是：

1. 先修正感知 stale 判定，排除假性高频启停。
2. 再做开环 PWM/编码器标定，确认左右轮响应不对称。
3. 最后按标定结果把轮速 PID 改成保守基线。

## 4. 已落地改动

### 4.1 感知 stale 判定修复

已把 stale 判定从“采集时间”改为“发布时间”，并把默认 `perception_stale_ms` 提高到 `120`。

相关代码：

- `new/code/port/control_types.hpp`
- `new/code/runtime/control_decision.hpp`
- `new/code/runtime/control_decision.cpp`
- `new/code/runtime/control_loop.cpp`
- `new/code/runtime/perception_frontend.cpp`
- `new/code/legacy/camera_logic.cpp`
- `new/config/default_params.json`

这一步之后，`RUNNING` 阶段不再出现重复的 `control.veto.perception_stale`，启动前只保留一次约 `293ms` 的初始 stale 窗口。

### 4.2 执行器输出参数解耦到配置

已把执行器相关保护参数放入运行时参数文件，而不是硬编码在控制路径里。

当前已支持：

1. `pwm_floor`
2. `prohibit_reverse_pwm`

相关代码：

- `new/code/port/control_types.hpp`
- `new/code/platform/param_store.cpp`
- `new/code/runtime/control_loop.cpp`
- `new/config/default_params.json`

当前结论：

1. `prohibit_reverse_pwm=1` 是有效保护项，应保留。
2. 单一全局 `pwm_floor` 不是这次抖动问题的主解。
3. 由于左右轮门槛明显不对称，后续如果要加 floor，应优先做分轮参数，而不是继续只靠一个全局值。

### 4.3 开环 PWM/编码器标定工具

已新增板端开环标定工作流：

- `new/user/debug.sh bench calibrate`
- `new/user/calibrate_pwm.py`

它复用了板端 one-shot bench PWM pulse 入口，对每个 PWM 脉冲记录编码器响应并输出 CSV。

## 5. 标定数据结论

主要证据文件：

- `new/verification/phase-d-pwm-encoder-calibration-open-loop.csv`
- `new/verification/phase-d-pwm-encoder-calibration-left-high.csv`

在 `pulse-ms=300`、`settle-ms=100` 条件下，得到的关键结论是：

### 5.1 右轮

右轮从约 `1400` PWM 就开始有稳定响应：

1. `1400 -> avg_total ≈ 124`
2. `1600 -> avg_total ≈ 219`
3. `1800 -> avg_total ≈ 300`
4. `2000 -> avg_total ≈ 363.5`
5. `2400 -> avg_total ≈ 526`
6. `2800 -> avg_total ≈ 737.5`

### 5.2 左轮

左轮明显更弱，`2400` 左右才进入可靠响应区：

1. `2400 -> avg_total ≈ 339`
2. `2500 -> avg_total ≈ 431.5`
3. `2600 -> avg_total ≈ 445.5`
4. `2700 -> avg_total ≈ 553`
5. `2800 -> avg_total ≈ 613`
6. `2900 -> avg_total ≈ 679`
7. `3000 -> avg_total ≈ 707.5`

### 5.3 标定结论

标定直接说明三件事：

1. 左右轮不是同一套开环响应曲线。
2. 左轮起转门槛显著高于右轮。
3. 双轮同参高增益 PID 在这种不对称 plant 上天然更容易振荡。

## 6. 当前固定基线

基于上述标定结论，当前先固定一套“最保守可运行基线”：

### 6.1 轮速 PID

文件：

- `new/config/default_params.json`

当前固定值：

1. `LEFT_WHEEL_PID = { P: 32.0, I: 0.0, D: 0.0, INTEGRAL_LIMIT: 0.0 }`
2. `RIGHT_WHEEL_PID = { P: 25.0, I: 0.0, D: 0.0, INTEGRAL_LIMIT: 0.0 }`

设计原则：

1. 先只保留低增益纯 `P`。
2. 先完全关掉 `I`，避免静止或低速时积分蓄积后反复推饱和。
3. 先完全关掉 `D`，避免编码器离散跳变把输出放大成抖动。
4. 左轮 `P` 略高于右轮，用于补偿左轮更弱的开环响应。

### 6.2 其它相关运行基线

1. `perception_stale_ms = 120`
2. `pwm_floor = 0`
3. `prohibit_reverse_pwm = 1`

## 7. 板端验证结论

在以上基线下，已完成一次重新下发、哈希校验后再启动的板端短测。

验证结论：

1. 运行前已确认板端二进制与本地构建哈希一致。
2. 运行前已确认板端参数文件与本地待下发参数哈希一致。
3. `RUNNING` 阶段抓到 `51` 个 `control.snapshot`。
4. 其中左轮 PWM 范围为 `846..2551`，`zero_count=0`。
5. 其中右轮 PWM 范围为 `428..1569`，`zero_count=0`。
6. `RUNNING` 阶段未再出现反复掉到 `0` 再拉起的高频启停。
7. 当前用户反馈为“不抖了”。

因此，当前可以把“高频启停式抖动”判定为已闭环。

## 8. 对后续调参的约束

这次闭环后，后续调参必须遵守以下顺序：

1. 先确认 gate 是否稳定，再谈 PID。
2. 先做开环标定，再改闭环参数。
3. 优先改分轮参数，不要默认左右轮必须同参。
4. 在没有新的分轮 feedforward 或分轮 PWM floor 前，不要贸然重新打开较大的 `I` 或 `D`。
5. 如果后续还要提性能，先小步增加 `P`，最后才考虑引入很小的 `I`。

不允许再用以下说法替代证据：

1. “和原车不一样所以有问题”
2. “感觉像启停”
3. “先把 PID 加大看看”

必须以当前系统、当前标定数据、当前板测日志为准。

## 9. 当前遗留项

虽然抖动问题已经闭环，但还有两个明确遗留项：

1. 目前仍只有全局 `pwm_floor`，没有分轮 `left_pwm_floor/right_pwm_floor`。
2. 当前保守基线主要目标是“先稳住”，不是“已经达到赛道最优速度响应”。

因此，后续如果继续提升性能，优先方向应是：

1. 分轮最小有效 PWM 参数
2. 分轮 feedforward 或分轮标定映射
3. 在更稳的开环补偿基础上，再做更细的闭环调参

## 10. 本次闭环涉及的主要文件

代码与配置：

- `new/code/runtime/control_loop.cpp`
- `new/code/runtime/control_decision.cpp`
- `new/code/runtime/perception_frontend.cpp`
- `new/code/platform/param_store.cpp`
- `new/code/port/control_types.hpp`
- `new/code/legacy/camera_logic.cpp`
- `new/config/default_params.json`
- `new/user/debug.sh`
- `new/user/calibrate_pwm.py`

证据：

- `new/verification/phase-d-pwm-encoder-calibration-open-loop.csv`
- `new/verification/phase-d-pwm-encoder-calibration-left-high.csv`

