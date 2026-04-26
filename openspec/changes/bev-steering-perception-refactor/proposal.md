## Why

当前 steering 主链仍以像素域统计和旧 bottom-tracker 参考线为核心，ordinary control、special scene 判据、调试输出分别依赖不同层次的几何近似，导致场景竞争、普通大弯误判、历史补线污染和控制语义耦合持续存在。既然距离正式比赛仍远，本轮应直接把 steering perception 的主真相切到 BEV，以统一 ordinary 控制、特殊场景 FSM 和参考线策略，而不是继续在旧像素链上补洞。

## What Changes

- 将 steering perception 重构为 `BEV` 主几何架构：图像前端负责二值化/边缘提取，`IpmProjector` 负责逆透视投影，`BEVGeometryExtractor` 负责输出左右边界、中心路径候选、宽度分布和可见距离。
- 用 `BEVTrackEstimate` 取代现有以 `LaneMetrics` 和 bottom tracker 为主的 ordinary 几何真相，主表示固定为“前向距离采样点列 + 局部几何摘要”，并由此计算 BEV-native 的横向误差、航向误差、预瞄曲率、可见距离和置信度。
- 采用直接 BEV authority cutover：本 change 不以旧像素域 `LaneMetrics`/bottom tracker 链作为 shadow oracle、长期 fallback truth 或正确性参照；BEV 正确性由标定工件、synthetic geometry fixtures、raw-frame/BEV overlay、控制符号/连续性 sanity 和 board smoke evidence 验收。
- 用统一 `SpecialSceneFSM` 取代当前串行抢占式 `special_wide/cross/circle/...` 场景选择，并将 `special_wide` 从正式模块语义中删除，只允许保留必要的 debug-only candidate 观测。
- 将 ordinary straight/bend 与 circle/zebra/cross 的判据统一迁入 BEV 坐标系，ordinary 和 special scene 共用同一套几何语言、参考线语义和进出阶段规则。
- 新增 `ReferencePolicyResolver` 与 `ControlErrorModel` 两层：FSM 只决定阶段，reference policy 只决定当前跟踪路径，控制层只消费近场横向误差、远场航向误差、预瞄曲率和控制约束，不再直接依赖旧像素域 `highest_line` 语义。
- 为后续控制扩展预留 `Observation Merger` / `ControlConstraintSet` 扩展点，使新观测模块可以通过统一 observation 入口影响 FSM、vehicle context 或控制约束，而不是直接侵入视觉几何和 PID 主链。
- 将旧像素域 `LaneMetrics`、bottom tracker、`highest_line/farthest_line/steering_reference_col` 降级为过渡兼容与调试投影层；兼容字段不得再反向驱动主几何、主场景判据或主控制误差。
- 同步更新 runtime-owned steering state、参数面、assistant/steering-media/selftest/overlay probe 和验证工件，明确 retained/adapter/removed 字段与模块边界。

## Capabilities

### New Capabilities
- `bev-steering-geometry`: 定义 BEV 作为 steering perception 的唯一主几何真相，包括 IPM 投影、BEV 边界/中心线提取、固定前向距离采样点列、几何置信度和 compatibility projection。
- `bev-special-scene-fsm`: 定义统一特殊场景 FSM、BEV 场景观测集、普通大弯 veto、十字白名单、环岛入口到出环的阶段推进，以及 reference policy 的职责边界。
- `bev-steering-control-model`: 定义基于 BEV reference path 的控制误差模型、约束输入面和 runtime-owned 控制记忆边界，替代旧像素域 ordinary control 主语义。

### Modified Capabilities
- `steering-tuning-media-observability`: steering snapshot、steering media 和调试证据束必须适配 BEV-first 几何与 FSM 语义，并区分主真相字段与 compatibility projection 字段。

## Risk Tier

- `STRICT`: 本变更同时替换 steering perception 主几何真相、ordinary 控制误差模型、特殊场景状态机、参数面和观测面，并显式删除旧模块正式语义。它直接影响车辆主控制链、调参链、调试链和验证工件，且要求未来扩展模块仍保持解耦，因此必须按 STRICT 级别执行 docs-first/source-first 审核。

## Impact

- 主要影响层：`port`（新主类型、状态字段、参数与调试合同）、`legacy`（IPM/BEV geometry/FSM/reference/control 模型）、`runtime`（perception/control 状态推进与 reset 边界）、`platform`（参数装载与调试发布）、`new/user`（media capture、overlay probe、selftest、调参脚本）。
- 主要影响代码：`new/code/legacy/camera_logic.*`、`new/code/legacy/steering_scene_*`、`new/code/legacy/steering_bottom_tracker.*`、`new/code/legacy/pid_control.*`、`new/code/runtime/perception_frontend.*`、`new/code/runtime/control_loop.*`、`new/code/runtime/runtime_state.hpp`、`new/code/port/control_types.hpp`、`new/code/platform/param_store.cpp`、相关调试协议与验证程序。
- 主要删除/降级对象：`active_module=special_wide` 正式语义、以 `LaneMetrics` 为核心的主模型、bottom tracker 的 ordinary 主地位、旧像素域竞争参数和由 compatibility 字段反向驱动主链的行为。
- 主要保留但降级为 adapter 的对象：`AnalyzeFrame(...)` 入口、runtime 主循环、调试链路、`steering_reference_col` / `highest_line` / `farthest_line` 的对外兼容输出。
- 相关技能与流程：`openspec-propose`、`openspec-artifact-verify`、`openspec-repair-change`、`openspec-apply-change`、`openspec-verify-change`；后续 steering/camera 调试与 assistant tooling 将消费新的 BEV-first 证据面。
