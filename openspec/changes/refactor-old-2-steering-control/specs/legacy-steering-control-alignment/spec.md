## ADDED Requirements

### Requirement: steering 控制律必须对齐 old_2 但只服务于 turn_output
`new/code/legacy/pid_control.*` MUST 以 `old_2/project/code/motor.cpp`、`motor.h`、`FUZZY_PID.cpp`、`FUZZY_PID.h` 和 `all_init.cpp` 为参考，实现 old_2 对齐的 steering 控制律。该控制律 MUST 只服务于当前 `turn_output` 差速链路，不得恢复 servo 终端，也不得继续输出 servo 命名字段。

#### Scenario: 普通赛道下计算新的转向目标
- **WHEN** 控制器收到 normal-lane 感知结果且没有特殊场景覆盖
- **THEN** 它 MUST 使用 old_2 对齐的模糊 P、误差项和上一周期记忆计算新的 `turn_output` 目标，而不是生成 servo 风格目标

### Requirement: 控制输入命名必须去 servo 化
若实现需要承接 old_2 `mid_servo` 对应的控制语义，MUST 将其转换为通用的中线/转向参考命名，并在 `pid_control`、`runtime`、调试快照与脚本中统一使用。新代码 MUST NOT 因为参考 old_2 而继续扩散 `servo` 命名字段。

#### Scenario: 调试链输出控制中间量
- **WHEN** `ControlDebugSnapshot::steering` 或相关脚本输出控制中间量
- **THEN** 字段集合 MUST 使用与 `turn_output` 和差速控制一致的命名，而不是继续发布 servo 风格字段

### Requirement: 陀螺仪阻尼与跨周期记忆必须保留
steering 控制律 MUST 包含 old_2 风格的陀螺仪阻尼项、跨周期误差记忆和 reset 语义，并继续将这些量映射到当前 `turn_output` 所有权模型中。

#### Scenario: 陀螺仪角速度与目标角速度存在偏差
- **WHEN** 摄像头侧控制生成了新的目标角速度，且当前 `gyro_z` 与该目标存在偏差
- **THEN** 控制器 MUST 生成 old_2 对齐的陀螺仪阻尼输出，并将其映射到当前 differential-drive turn 计算

### Requirement: reset 路径必须统一清理控制器记忆与 legacy carry-over
运行时 reset 路径 MUST 同时清理 camera error、gyro error、prior-cycle memory、runtime-owned legacy steering carry-over 和与 turn 计算有关的控制器状态；复位后的第一拍 turn 计算 MUST 从文档定义的干净状态起算。

#### Scenario: stop 或 fail-safe 后重新进入可驱动阶段
- **WHEN** 控制循环经历 stop、fail-safe 或显式 reset 后再次进入可驱动阶段
- **THEN** `pid_control`、runtime-owned steering state 和相关 turn 记忆 MUST 已经完成 reset，上一轮输出不得继续污染新一轮 turn 计算

### Requirement: 参数与调试字段必须删旧增必需
控制相关的 `RuntimeParameters`、`PerceptionResult`、`ControlDebugSnapshot::steering` 和脚本解析字段 MUST 在尽可能复用当前有价值字段的基础上进行裁剪；实现 MUST 删除无用字段，保留仍有价值的字段，并只新增控制对齐与验收必需字段。

#### Scenario: 更新参数与诊断字段集
- **WHEN** steering 移植需要新增或删除参数/调试字段
- **THEN** 变更 MUST 同步说明哪些字段被复用、哪些字段被删除、哪些字段是新增且必需的，并保持 `new/user` 下相关调试脚本同步

### Requirement: 输出限幅与执行器链路必须保持 current differential-drive terminal
steering 控制输出 MUST 在 current runtime 中归属于 `turn_output`，随后进入 `wheel_target_mixer -> wheel PID -> left/right PWM`。实现 MUST 记录 raw 与 applied turn 输出、限幅结果及其原因，但 MUST NOT 恢复 direct servo-device 输出或旁路当前差速驱动末端。

#### Scenario: steering 输出接近限幅
- **WHEN** old_2 对齐控制律计算出的 raw turn 输出超过当前运行时允许的 turn 限幅
- **THEN** 运行时 MUST 先按文档定义执行 clamp，再通过当前 differential-drive 执行器链路生效，并在 debug 快照中同时公开 raw 和 applied turn 输出

### Requirement: 控制诊断面必须支持多周期与 assistant-disabled 验收
`ControlDebugSnapshot::steering` 与 `control.steering_snapshot` MUST 暴露多周期控制与 assistant-disabled 验收所必需的控制字段，包括 raw/applied turn、camera/gyro 中间项、场景修正来源和用于解释上一拍记忆影响的必要上下文。

#### Scenario: 多周期控制器测试复核上一拍记忆
- **WHEN** 验证夹具执行多周期 steering 回放并需要判断上一拍记忆是否参与了下一拍 turn 计算
- **THEN** 诊断输出 MUST 足够重建该控制链路，而不需要依赖额外交互式工具
