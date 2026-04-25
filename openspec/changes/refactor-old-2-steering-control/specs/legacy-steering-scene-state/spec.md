## ADDED Requirements

### Requirement: scene-state 必须以 runtime-owned state 承接并模块化
`new/` MUST 以运行时拥有的 steering state 承接场景标志与跨周期上下文，并将 straight、bend、circle entry、circle interior、circle exit、zebra、cross 的场景逻辑拆成独立模块。主编排层可以统一调度这些模块，但各模块 MUST 有清晰的输入、状态推进和 steering 输出边界。

#### Scenario: 运行时在多个场景模块间切换
- **WHEN** 控制循环从普通赛道进入环岛入口或从 bend 回到 straight
- **THEN** runtime-owned steering state MUST 记录当前场景模块与必要上下文，并通过显式模块切换而不是在直道逻辑中隐式插入场景分支

### Requirement: runtime-owned steering state 必须承接跨周期控制与边界上下文
除了场景模块切换外，runtime-owned steering state MUST 承接 `highest_line` / `farthest_line` 相关上下文、prior-cycle controller memory 和场景修正来源，使感知、控制和 reset 边界都不再依赖 `old_2` 的裸全局。

#### Scenario: 下一控制周期复用上一拍上下文
- **WHEN** 控制循环进入下一周期且上一拍已经积累有效的场景或控制上下文
- **THEN** runtime-owned steering state MUST 能继续提供这些上下文给感知编排层和控制层，而不是把它们重新编码成裸全局或单帧结果

### Requirement: 环岛状态必须拆分为 entry/interior/exit 模块
新的 scene-state 语义 MUST 将环岛拆分为 circle entry、circle interior、circle exit 三类独立逻辑模块，并保留 old_2 中方向判定、补线/固定打角/出环修正对 turn 计算的作用。

#### Scenario: 左环从入口推进到出环
- **WHEN** 环岛状态从左环入口推进到环内再推进到出环
- **THEN** steering 计算 MUST 经过 entry、interior、exit 三个模块化阶段，而不是把这些规则混写在一个普通赛道函数里

### Requirement: zebra 与 cross 必须作为独立模块参与 steering
scene-state 实现 MUST 将 zebra 与 cross 作为独立模块处理，并定义它们的进入条件、保持条件、退出条件以及对 steering 目标的覆盖或偏置规则。

#### Scenario: 识别到 zebra 或 cross
- **WHEN** 场景状态机确认当前输入符合 zebra 或 cross 条件
- **THEN** 对应模块 MUST 接管或修正 steering 语义，并在调试快照中公开当前模块来源

### Requirement: roadblock 本次只保留接口不实现绕行
本次移植 MUST NOT 引入 roadblock 绕行控制逻辑，但 MUST 保留 roadblock 的控制接口、状态占位、调试字段边界和后续扩展入口。实现 MUST 明确区分“接口保留”与“本次未实现”。

#### Scenario: 调试面查询 roadblock 能力
- **WHEN** 运行时、脚本或测试查询 roadblock 相关状态
- **THEN** 系统 MUST 能表达 roadblock 接口存在且当前未启用绕行实现，而不是伪装成已支持完整路障绕行

### Requirement: scene-state 必须服从统一 reset 路径
所有 runtime-owned legacy steering state MUST 通过现有运行时 controller reset 路径统一复位，包括启动前复位、fail-safe 后复位、显式 reset 请求以及 stop-to-disarmed 边界；复位后不得残留上一轮场景模块的修正。

#### Scenario: 触发控制器 reset
- **WHEN** 运行时执行 steering controller reset
- **THEN** straight、bend、circle、zebra、cross 以及 roadblock 接口占位状态 MUST 同步回到文档定义的初始态
