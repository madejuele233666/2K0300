# Phase E：完赛闸门

## 1. 阶段目标

给出一个“只有全部通过才允许认定具备完赛能力”的最终闸门。

这不是开发任务集合，而是最终通过标准。

## 1A. 本阶段继承的最终判定原则

本阶段的判定原则来自整个系列的共同约束：

1. 不能用一次偶然成功代替“具备完赛能力”。
2. 不能用 degraded 或诊断配置代替比赛配置。
3. 不能只看功能成功，还要看异常恢复和现场操作是否成立。
4. 不能只看基础循迹，还要看图片识别与策略切换是否已经真正进入比赛链路。
5. 如果比赛配置采用距离传感器，它也必须通过同一策略层进入比赛链路，而不是临时特判。

## 2. 完赛闸门清单

### E-1 硬件与控制闸门

必须全部通过：

1. 相机、IMU、编码器、电机、ADC、定时器均按最终比赛配置工作。
2. 启动不依赖 degraded 模式。
3. 控制环在正常运行时不会长期停留在 `control.veto`。
4. 电机输出、停机、急停均可靠。

### E-2 感知与语义闸门

必须全部通过：

1. 基础循迹在目标赛道环境中稳定。
2. 图片识别算法可稳定识别比赛要求的图片类别。
3. 如果采用距离传感器，其判定结果可稳定识别比赛要求的触发场景。
4. 语义识别结果能正确影响方向、速度或状态机。
5. 误识别、漏识别或距离误触发不会直接把车辆带入不可恢复错误。

### E-3 赛道运行闸门

必须全部通过：

1. 车辆能完成完整路线。
2. 连续多次运行结果稳定，不是偶发一次成功。
3. 复杂赛道元素处的策略切换正确。
4. 车辆不会因为常见光照变化而立刻失效。

### E-4 异常恢复闸门

必须全部通过：

1. 短时感知异常可恢复。
2. 短时传感器抖动不会导致永久失控。
3. 低压、停机、重启流程可执行。
4. 现场出现错误后能快速回到已知稳定参数集。

### E-5 运营与比赛闸门

必须全部通过：

1. 有明确的发车前检查单。
2. 有明确的参数切换流程。
3. 有明确的急停和人工接管流程。
4. 有比赛当天使用的稳定版本和备用版本。

## 2A. 最低证据包

在宣称“具备完赛能力”之前，至少要具备以下证据包：

1. 板测日志包
2. 低速实车验证包
3. 视觉鲁棒性对比包
4. 图片识别类别与策略验证包
5. 如果采用距离传感器，还要有距离判定与策略验证包
6. 完整路线测试包
7. 现场参数与回滚包

## 3. 完赛前最后一轮任务

### 3.1 端到端彩排

目标：
在不改代码的前提下，用比赛配置完整验证一次端到端流程。

任务：

1. 用比赛配置完整跑流程。
2. 从上电开始记录到结束。
3. 不中途改代码，只允许按既定参数和流程操作。

触达文件：

- 比赛配置参数集
- 比赛 profile
- `new/docs/race-finish-series.zh-CN/04-phase-d-track-operations-and-tuning.md`

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-e-end-to-end.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
```

预期 marker：

- `startup.complete`
- `control.start`
- `shutdown.complete`

证据文件：

- `phase-e-end-to-end.md`
- `phase-e-end-to-end.log`

通过标准：

- 全流程按比赛配置完整走通，无临场改代码

失败回退方向：

- 按失败类型回退到 Phase A / B / C / D

### 3.2 连续性验证

目标：
证明系统不是偶发一次成功，而是可重复运行。

任务：

1. 连续多次启动
2. 连续多次运行
3. 连续多次停机和重启

触达文件：

- 比赛配置参数集
- 现场操作流程文档

运行命令：

```bash
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-e-repeatability.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
```

预期 marker：

- `startup.complete`
- `shutdown.complete`

证据文件：

- `phase-e-repeatability.md`
- `phase-e-repeatability.log`

通过标准：

- 多次连续运行结果稳定，不依赖偶发一次成功

失败回退方向：

- 若问题来自现场流程或参数：回 Phase D
- 若问题来自运行时或控制：回 Phase A / B / C

### 3.3 备用方案验证

目标：
确认比赛当天确实存在可切换的备用方案，而不是口头预案。

任务：

1. 备用参数集可切换
2. 图片识别降级策略可触发
3. 如果采用距离传感器，其降级或失效策略可触发
4. 关键传感器轻微波动下仍能继续完成流程

触达文件：

- 备用参数集目录
- 策略层降级逻辑
- 现场回滚流程文档

运行命令：

```bash
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-e-fallback.log SMOKE_ENABLE_MOTOR=1 ./run_remote_smoke.sh)
(cd new/user && LS2K_ALLOW_DEGRADED_STARTUP=1 LS2K_PROFILE_PATH=../config/hardware_profile.json LS2K_PARAMS_PATH=../config/default_params.json LS2K_MAX_FRAMES=40 ../out/new >> ../verification/phase-e-fallback.log 2>&1)
```

预期 marker：

- `startup.mode.degraded`
- `control.veto`

证据文件：

- `phase-e-fallback.md`
- `phase-e-fallback.log`

通过标准：

- 备用方案可触发、可回退、不会把车辆带入不可恢复状态

失败回退方向：

- 传感器/策略降级设计异常：回 Phase C
- 参数/回滚流程异常：回 Phase D

## 4. 通过标准

只有当以下结论全部成立时，才能认定“按本系列推进后，车辆已具备上赛道完赛能力”：

1. Phase A 到 Phase D 的退出条件全部满足。
2. 完赛闸门 E-1 到 E-5 全部通过。
3. 完整路线验证不是偶发一次成功，而是可重复结果。
4. 比赛当天有稳定主版本和可回滚备用方案。

这四条缺一不可。

## 5. 不通过时的回退规则

如果在 Phase E 失败，按失败类型回退：

1. 传感器/执行器问题：
   - 回到 Phase A
2. 低速实车控制问题：
   - 回到 Phase B
3. 视觉鲁棒性或语义识别问题：
   - 回到 Phase C
4. 参数、流程、现场运营问题：
   - 回到 Phase D

不允许在 Phase E 临时堆补丁然后直接宣称“可以完赛”。
