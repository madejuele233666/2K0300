## ADDED Requirements

### Requirement: steering 感知输入分辨率必须以 320x240 为准
`new/code/legacy/camera_logic.*` MUST 以 320x240 摄像头输入作为新系统的规范分辨率，并以 `old_2/project/code/camera.cpp` / `camera.h` 为行为参考迁移 steering 感知语义。实现 MAY 在内部做必要的几何变换或归一化，但外部契约 MUST 明确声明输入来源是 320x240，而不是把 160x120 作为新系统的摄像头分辨率。

#### Scenario: 摄像头帧进入 steering 感知链
- **WHEN** 一帧有效的 320x240 灰度图进入 `new/code/legacy/camera_logic.*`
- **THEN** 感知链 MUST 按文档定义处理该分辨率输入，并输出与 old_2 语义对齐的阈值、边界、`highest_line`、`farthest_line` 和转向参考结果

### Requirement: 感知输出不得使用 servo 命名字段
新代码中的 `PerceptionResult`、调试快照和相关脚本 MUST NOT 继续引入或扩散 `servo` 命名字段。若实现需要表达 old_2 `mid_servo` 对应的中线/转向参考语义，MUST 使用与差速转向一致的通用命名，例如中线列位置、转向参考列或等价的非 servo 字段。

#### Scenario: 感知模块发布中线结果
- **WHEN** 感知模块完成普通赛道或特殊场景的中线求解
- **THEN** 它 MUST 通过非 servo 命名字段发布该结果，并保证后续控制与调试链不依赖 `mid_servo` 之类的 servo 字段名

### Requirement: 模块化普通赛道与场景入口感知逻辑
感知实现 MUST 将直道、bend、circle entry、circle interior、circle exit、zebra、cross 的判断或入口逻辑拆成独立模块或独立实现单元，由 `camera_logic` 进行编排；这些逻辑 MUST NOT 再与直道主路径混写成一个不可分解的大函数。

#### Scenario: 弯道与直道都存在感知分支
- **WHEN** steering 感知同时需要支持 straight 和 bend
- **THEN** 直道与 bend 逻辑 MUST 由独立模块承接，并通过显式的编排边界接入主感知流程

### Requirement: 八邻域边界与转向参考语义必须保留
新的感知实现 MUST 保留 old_2 八邻域边界提取、`highest_line`、`farthest_line` 和加权中线的 steering 语义；它 MAY 以 `new/` 的数据结构重写实现，但 MUST 保持这些量对 turn 计算与场景切换的作用不变。

#### Scenario: 弯道帧存在不完整边界
- **WHEN** 输入帧只包含部分左/右边界，且仍能形成 old_2 可接受的八邻域轨迹
- **THEN** 感知结果 MUST 继续给出与 old_2 同语义的 `highest_line`、`farthest_line` 和转向参考上下文，而不是直接退化为单一近似误差

### Requirement: 感知结果必须保留 assistant-disabled 验收所需最小字段
即使本次字段策略改为删旧增必需，`PerceptionResult` 与 `ControlDebugSnapshot::steering` 仍 MUST 保留 assistant-disabled 验收所需的最小感知字段集合，以支持通过 `control.steering_snapshot` 和 host 侧脚本复核阈值、边界上下文、`highest_line`、`farthest_line` 与场景来源。

#### Scenario: assistant 未启用但需要复核感知对齐
- **WHEN** 运行时未启用 assistant 或 steering media，仅保留控制诊断输出
- **THEN** `control.steering_snapshot` 仍 MUST 提供足够字段来判断感知输出是否与 old_2 预期一致

### Requirement: 感知调试面必须与脚本同步
`ControlDebugSnapshot::steering`、`control.steering_snapshot` 以及 `new/user` 下读取这些字段的调试程序/脚本 MUST 同步更新到新的字段契约。变更 MUST 删除无用字段、保留仍有价值的现有字段，并只新增实现或验收必需字段。

#### Scenario: host 侧脚本读取 steering 快照
- **WHEN** `new/user/steering_media_capture.py`、`tune_steering.py` 或相关脚本读取 `control.steering_snapshot`
- **THEN** 它们 MUST 能正确解析更新后的字段集，且不会继续依赖已被移除的无用字段或 servo 命名字段
