## Context

当前 steering 栈的主问题不是单个分类阈值不稳，而是主几何真相、场景判据、控制误差和调试出口分散在不同层次的像素域近似里：

- `legacy/camera_logic.*` 与 `legacy/steering_scene_common.*` 混合维护当前帧统计、history fallback、ordinary 参考线和特殊场景候选。
- `legacy/steering_scene_orchestrator.cpp` 仍用串行抢占顺序选择 `zebra/cross/circle/special_wide/bend/straight`，`special_wide` 仍是正式 `active_module`。
- `legacy/steering_bottom_tracker.*` 和 `LaneMetrics` 仍是 ordinary 主几何链，straight/bend 直接透传 `steering_reference_col/lateral_error`。
- `legacy/pid_control.*` 仍以旧像素域 `lateral_error/heading_error/curvature/highest_line` 为主要输入。
- `runtime/control_loop.*`、`assistant`、`steering-media` 和 host tooling 继续把这些像素域字段当成 steering 真相。

本 change 的目标不是在现有链路上继续加一个 IPM sidecar，而是把 BEV 升级为 steering perception 的唯一主几何真相，并以此统一 ordinary control、special scene FSM、reference policy 和 future observation extensibility。

### Reference Alignment Inventory

目标动作：`adapt`

必须显式对齐的当前系统参考：

- `new/code/legacy/camera_logic.cpp`
- `new/code/legacy/steering_scene_common.hpp`
- `new/code/legacy/steering_scene_common.cpp`
- `new/code/legacy/steering_bottom_tracker.cpp`
- `new/code/legacy/steering_scene_orchestrator.cpp`
- `new/code/legacy/steering_scene_special_wide.cpp`
- `new/code/legacy/steering_scene_circle_entry.cpp`
- `new/code/legacy/pid_control.cpp`
- `new/code/runtime/perception_frontend.cpp`
- `new/code/runtime/control_loop.cpp`
- `new/code/port/control_types.hpp`
- `new/code/platform/steering_media_protocol.*`
- `new/user/scene_overlay_probe.cpp`
- `new/user/steering_media_capture.py`

补充参考证据：

- `old_2/project/code/all_init.h`
  - `USED_ROW/USED_COL` 表明旧系统曾显式区分“透视图使用面”和原始图像面，说明本 change 引入 IPM/BEV 并非无约束发明，而是把历史上隐含存在的几何层正式化。

### Alignment Mapping

| Reference Module | Target Module | Action | Notes |
|---|---|---|---|
| `camera_logic + steering_scene_common` 像素域混合统计 | `steering_bev_projector + steering_bev_geometry + steering_observation_assembly` | Adapt | 从“像素统计混合层”拆成图像前端、BEV 几何、观测装配三层 |
| `steering_bottom_tracker` ordinary 主链 | `BEVTrackEstimate` | Adapt | ordinary 主几何从图像行扫描切到固定前向距离采样点列 |
| `steering_scene_orchestrator` 串行抢占 | `SpecialSceneFSM + ReferencePolicyResolver` | Adapt | FSM 负责阶段，reference policy 负责路径选择，删除抢占式场景竞争 |
| `steering_scene_special_wide` | `FSM candidate/confirm 内部状态` | Adapt then remove formal surface | `special_wide` 不再作为正式模块公开 |
| `pid_control` 像素域误差输入 | `ControlErrorModel` | Adapt | 控制继续输出 `turn_output`，但误差定义改成 BEV-native |
| `control_loop` / `steering-media` 旧字段快照 | `compatibility_projection + BEV-first snapshot` | Adapt | 保留可观测能力，但 compatibility 字段不再拥有主语义 |

### Coverage Report

- ✅ 已覆盖：ordinary 几何主真相、special scene FSM、reference policy、控制误差模型、调试/telemetry 出口、扩展观测接入边界。
- ⚠️ 部分覆盖：IPM 标定参数的最终实测值、overlay probe 的最终双视图呈现细节。
- ❌ 未覆盖：任何基于旧 bottom tracker 的长期正式角色；任何 `special_wide` 对外正式语义。

### Unresolved Alignment Risks

- 若 compatibility projection 被主链回读，BEV 单真相会退化成双真相。
- 若 future observation provider 直接侵入 PID 或 FSM，而不是经过统一 observation assembly，后续扩展会重新形成耦合。
- 若控制仍长期依赖 `highest_line` 进行主增益调度，BEV-native 控制语义将被旧像素语义绑死。

## Goals / Non-Goals

**Goals:**

- 将 `BEV` 固定为 steering perception 的唯一主几何真相。
- 用统一 `BEVTrackEstimate` 驱动 ordinary straight/bend、special scene FSM 和 reference policy。
- 删除 `special_wide` 正式模块语义，并将当前串行抢占式场景选择重构为统一 FSM。
- 将控制误差模型重构为 BEV-native：近场横向误差、远场航向误差、预瞄曲率、可见距离/置信度和控制约束。
- 为后续控制扩展新增清晰的 observation provider / merger / constraint 接口，不允许新模块绕过主边界。
- 保留 runtime 主入口、turn 输出末端、调试与验证链路的可观测能力，但把旧字段降级为 compatibility projection。

**Non-Goals:**

- 继续长期维护“像素域 ordinary 主链 + BEV special scene”的双真相方案。
- 为兼容性保留 `special_wide` 对外模块、旧 bottom tracker ordinary 主地位或由 compatibility 字段回流主链。
- 本轮引入新的执行器终端、servo 命名字段或脱离 `turn_output -> wheel_target_mixer -> wheel PID -> PWM` 的控制末端。
- 本轮实现具体的新控制扩展模块；本轮只提供扩展边界和约束模型。

## Decisions

### Decision: BEV 成为 steering perception 的唯一主几何真相

问题：
当前 ordinary 几何、special scene 判据和调试字段分别依赖像素域不同层次的近似，导致任何一次场景或控制调参都可能变成“哪套几何是真相”的争论。继续保留像素域 ordinary 主链，只会把未来的 BEV 引入变成第二次重构。

备选方案：

- 方案 A：保留像素域 ordinary 主链，只把 circle/cross 判据迁入 BEV。
  - 优点：短期改动较小。
  - 缺点：长期形成双真相系统，ordinary 与 scene 继续用两种几何语言。
  - 验证影响：差。
- 方案 B：让 BEV 成为唯一主几何真相，ordinary 与 special scene 全部消费 BEV 结果，像素字段只作为 compatibility projection 保留。
  - 优点：架构统一、长期成本低、未来扩展清晰。
  - 缺点：初次切换更激进，参数和调试链需同步迁移。
  - 验证影响：强。

选定方案：
方案 B。

Stack Equivalent：

- 像素域混合几何统计 -> `steering_bev_projector.*` + `steering_bev_geometry.*`
- ordinary 主参考线 -> `BEVTrackEstimate`
- 外部 legacy 字段 -> `CompatibilityProjection`

Named Deliverables：

- `new/code/legacy/steering_bev_projector.hpp`
- `new/code/legacy/steering_bev_projector.cpp`
- `new/code/legacy/steering_bev_geometry.hpp`
- `new/code/legacy/steering_bev_geometry.cpp`
- `new/code/port/control_types.hpp`

Failure Semantics：

- 若 ordinary 控制或 scene 判据仍以 `LaneMetrics`/bottom tracker 为主，则本决策失败。
- 若 compatibility 字段被主链回读作为主几何真相，则本决策失败。

Boundary Examples：

- ordinary 直道：从 `BEVTrackEstimate` 读取中心路径和几何摘要。
- circle entry：从 `BEVSceneObservation` 读取单侧分岔、对侧直道、宽度变化和 cross whitelist 证据。
- host 调试：允许继续看到 `steering_reference_col/highest_line/farthest_line`，但这些值由 compatibility projection 生成。

Contrast Structure：

- 采用：BEV 单真相 + 薄兼容投影层。
- 不采用：像素域 ordinary 主链 + BEV special-scene 辅助层。

Direct Cutover Policy：

- 本 change 不采用旧 `LaneMetrics`/bottom tracker/像素域 ordinary 链作为 runtime shadow oracle、fallback truth 或 BEV 正确性参照。
- 旧链可作为阅读参考帮助理解历史行为，但实现验收必须来自 BEV 标定工件、synthetic BEV fixtures、raw-frame/BEV overlay、控制符号/连续性 sanity 和 board smoke evidence。
- compatibility projection 只能由 BEV 结果投影生成；若实现需要读取 compatibility 字段才能完成 ordinary、scene、reference 或 control 主决策，则本决策失败。

Verification Hook：

- `scene_classifier_selftest` 与新 BEV geometry fixture
- `scene_overlay_probe` 原图/BEV 双视图
- board steering-media 对齐证据，证明 ordinary 与 scene 都由同一 BEV 几何驱动

### Decision: ordinary 主表示采用固定前向距离采样点列的 `BEVTrackEstimate`

问题：
若继续把 ordinary 主几何建模成零散 anchor 或图像行统计，后续 FSM、reference policy 和控制误差仍会不断回退到图像坐标思维。需要一种既能服务控制、又能服务场景判据和未来扩展模块的统一路径表示。

备选方案：

- 方案 A：保留 anchor/row-band 统计，外加少量 BEV 摘要。
  - 优点：更接近当前代码。
  - 缺点：主表示仍然碎片化，reference policy 和 future modules 很难统一依赖。
  - 验证影响：中等偏差。
- 方案 B：使用固定前向距离采样点列，外加局部几何摘要（可见距离、宽度分布、曲率、置信度）。
  - 优点：ordinary 控制、场景判据、扩展观测都可共享一套空间基底。
  - 缺点：需要新建 BEV geometry extractor 和更多 fixture。
  - 验证影响：强。

选定方案：
方案 B。

Stack Equivalent：

- bottom tracker center anchors -> `BEVTrackEstimate.sampled_centerline`
- left/right visible anchors -> `sampled_left_boundary` / `sampled_right_boundary`
- lane width median/bands -> `lane_width_profile`
- heading/curvature summary -> `near_lateral_offset` / `far_heading_error` / `preview_curvature`

Named Deliverables：

- `new/code/legacy/steering_bev_geometry.hpp`
- `new/code/legacy/steering_bev_geometry.cpp`
- `new/code/legacy/steering_bev_types.hpp`
- `new/user/scene_overlay_probe.cpp`

Failure Semantics：

- 若 ordinary 输出仍依赖图像行 band 和临时 anchor 统计作为主模型，则本决策失败。
- 若 future observation modules 不能在统一前向距离坐标上附着证据，则扩展性目标失败。

Boundary Examples：

- fixed sample points: `forward_m_samples[]` from the accepted BEV/control parameter surface
- geometry summary: near lateral offset、far heading error、preview curvature、visible range、track confidence
- fallback mode: BEV boundary partial loss 仍保留 `source/fallback_mode`，但不返回像素域主链

Contrast Structure：

- 采用：固定前向距离采样点列 + 局部摘要。
- 不采用：row-band/anchor 拼接式 ordinary 主表示。

Verification Hook：

- synthetic BEV geometry fixture
- regression frame replay for sampled path continuity
- host-side overlay of sampled centerline and boundaries

### Decision: BEV 坐标、标定与板端构建策略必须先固定

问题：
BEV 一旦成为唯一主几何真相，坐标系、采样表、投影标定和运行时构建方式就不能隐含在代码常量或 host 脚本里。否则 scene evidence、reference policy、control error 和 overlay evidence 会看似都叫 BEV，实际却可能使用不同坐标解释。

备选方案：

- 方案 A：板端主链先生成 dense BEV bitmap，再从 bitmap 提取路径。
  - 优点：直观，host overlay 容易复用。
  - 缺点：每帧算力和内存压力更高，控制实际只需要固定前向距离上的边界/中心线/摘要。
  - 验证影响：中等。
- 方案 B：板端主链优先采用 sparse BEV lattice sampler：startup 后根据已加载标定预计算 inverse-projection/LUT，在固定 forward-distance 与 lateral 候选上采样原图，直接产出边界、中心线、宽度、置信度和控制摘要；dense BEV 只作为可选 host/debug artifact。
  - 优点：更贴近控制需求，实时性风险低，fixture 和 overlay 可直接复用同一采样点列。
  - 缺点：需要把采样表、坐标系和 overlay 重建合同写清楚。
  - 验证影响：强。

选定方案：
方案 B。

Stack Equivalent：

- image pixel -> calibrated inverse-projection/LUT lookup
- row-band anchors -> vehicle-frame sampled boundary/path points
- debug dense BEV -> optional overlay artifact, not required runtime authority

Accepted Coordinate Contract：

- vehicle-frame origin: left/right wheel axle midpoint on the vehicle centerline.
- `forward_m`: positive forward along the vehicle centerline.
- `lateral_m`: positive to the vehicle right.
- sampled path, boundary, width profile, reference path, and control-error terms MUST use this coordinate convention unless a future calibration change explicitly updates the contract.
- sampling distances, lateral search range, grid/debug output size, image source points, vehicle-frame target points, and projector version/hash MUST be startup-loaded or reconstructable from accepted calibration artifacts.

Named Deliverables：

- `new/code/legacy/steering_bev_projector.*`
- `new/code/legacy/steering_bev_geometry.*`
- accepted projector calibration artifact/table
- runtime parameter keys for BEV sampling/grid/calibration
- overlay evidence that carries projector id/hash and sampled point lists

Failure Semantics：

- 若 board 侧 projector 数值无法从 accepted artifact/table 重建，则标定合同失败。
- 若 dense BEV bitmap 成为板端主链必需中间态并导致实时性不可验收，则运行时构建策略失败。
- 若不同模块对 lateral/forward/sign convention 的解释不一致，则 BEV 单真相失败。

Verification Hook：

- calibration artifact reconstruction test
- sparse sampler fixture for straight/left/right/cross/circle synthetic geometry
- overlay review showing raw frame, sampled BEV path, reference path, and compatibility projection share one projector id/hash

### Decision: BEV calibration and tuning ownership are explicit port/platform contracts

问题：
proposal 已经把参数面和 `param_store.cpp` 列为主影响面，但如果 design 不明确 projector calibration、BEV geometry tuning、FSM thresholds 和 control tuning 的所有权与持久化位置，后续实现很容易把关键常量重新散落到 `legacy` 或 host 脚本里。

备选方案：

- 方案 A：先实现主链，后续再补参数/标定合同。
  - 优点：前期写代码更快。
  - 缺点：STRICT change 会在实现中形成隐式常量和不可审计的 board calibration。
  - 验证影响：差。
- 方案 B：把参数和标定所有权作为本 change 的正式边界：camera/IPM 标定、BEV geometry tuning、FSM 阈值、control-model tuning 都通过 startup-loaded project-owned 参数面进入运行时，并保留 accepted calibration artifact/table 解释 board 侧数值来源。
  - 优点：实现、调试、验证和 future maintenance 的边界一致。
  - 缺点：本轮必须同步修改参数装载、config snapshot 和 host evidence。
  - 验证影响：强。

选定方案：
方案 B。

Stack Equivalent：

- hardcoded projector constants -> `RuntimeParameters` / `param_store.cpp`
- board-side accepted calibration source -> host-side calibration artifact or table persisted in verification evidence
- scene/control tuning constants -> startup-loaded project-owned parameter surface
- 1.4 control-error weights/thresholds/sample selectors -> `RuntimeParameters` / accepted runtime parameter JSON or documented compile-time invariant

Named Deliverables：

- `new/code/port/control_types.hpp`
- `new/code/platform/param_store.cpp`
- accepted runtime parameter JSON keys for projector/geometry/FSM/control tuning
- calibration artifact/table preserved under `new/verification/...` and surfaced in `config_snapshot`

Failure Semantics：

- 若 projector calibration 只能从源码常量或未记录脚本中恢复，则本决策失败。
- 若 FSM/control tuning 与 camera/IPM calibration 混在同一无边界常量组内，则参数所有权失败。
- 若 `steering_control_error_model.*`、`pid_control.*` 或 `control_loop.*` 引入未归属的 tuning/calibration magic numbers，则 1.4 cutover 失败；源码只能保留算法结构、默认值、容量上限、单位换算和 hard safety clamps。

Boundary Examples：

- IPM source points、output grid、vehicle-frame scale -> camera/IPM calibration surface
- circle/cross threshold、ordinary bend veto threshold -> scene/FSM tuning surface
- near/far sample distances、curvature weighting、constraint scaling -> control-model tuning surface

Contrast Structure：

- 采用：显式参数 ownership + accepted calibration artifact/table。
- 不采用：实现后补文档或沿用隐式 hardcoded 常量。

Verification Hook：

- `param_store` tests for new parameter groups
- `config_snapshot` / steering-media evidence showing loaded projector and tuning values
- board smoke evidence cross-referencing the accepted calibration artifact/table

### Decision: 观测装配层统一接入 geometry、vehicle context 和 future observation providers

问题：
当前视觉、IMU 连续性、history fallback 和 scene 候选分散在不同实现里，没有统一的 observation assembly 边界。未来若新增坡度、摩擦、障碍或额外语义观测，若直接接入 PID/FSM，将重新形成跨层耦合。

备选方案：

- 方案 A：每个新观测模块直接接入需要它的层。
  - 优点：短期接线快。
  - 缺点：长期形成多点侵入，难以维护“谁可以影响控制”的边界。
  - 验证影响：差。
- 方案 B：所有观测模块先产出 typed observation fragment，经 `ObservationMerger` 合并成 `BEVSceneObservation`、`VehicleContext` 或 `ControlConstraintSet` 后再交给 FSM/控制层。
  - 优点：扩展边界稳定，可测试性强。
  - 缺点：需要前置设计统一类型。
  - 验证影响：强。

选定方案：
方案 B。

Stack Equivalent：

- IMU continuity + wheel motion summaries -> `VehicleContext`
- scene-specific geometry evidence -> `BEVSceneObservation`
- future control-affecting observations -> `ControlConstraintSet`

Named Deliverables：

- `new/code/legacy/steering_observation_assembly.hpp`
- `new/code/legacy/steering_observation_assembly.cpp`
- `new/code/port/control_types.hpp`
- optional provider interfaces under `new/code/legacy/` or `new/code/runtime/`

Failure Semantics：

- 若 future providers 可直接修改 PID 输入或 FSM 状态，则本决策失败。
- 若 geometry extractor 同时承担 future semantics 与 control constraints，模块边界失败。

Boundary Examples：

- obstacle provider -> 输出 `path_bias` 或 `speed_cap` 到 `ControlConstraintSet`
- friction provider -> 输出 `steering_gain_scale`
- semantic marker provider -> 输出场景白名单/黑名单 evidence 到 `BEVSceneObservation`

Contrast Structure：

- 采用：provider -> merger -> typed context/constraint。
- 不采用：provider 直连 scene 或 PID。

Verification Hook：

- document review: provider 只能通过 merger 接入
- source-first review: 新 provider 不跨层 include/依赖 geometry internals
- fixture tests for merged observation precedence

### Decision: 用统一 `SpecialSceneFSM` + `ReferencePolicyResolver` 替代串行抢占和 `special_wide`

问题：
当前场景调度仍是串行抢占，`special_wide` 作为正式模块存在，circle/cross 通过分数竞争和 prior state 过渡。该结构把候选确认、阶段推进和参考线策略混在一起，难以证明场景推进的稳定性。

备选方案：

- 方案 A：保留 orchestrator，继续优化模块顺序和竞争参数。
  - 优点：沿用当前结构。
  - 缺点：`special_wide` 和分数竞争会长期污染 scene-state 语义。
  - 验证影响：差。
- 方案 B：统一 FSM 负责 candidate/confirm/progress/release，`ReferencePolicyResolver` 独立负责当前跟踪路径选择，正式模块语义中删除 `special_wide`。
  - 优点：职责清晰，ordinary 与 special scene 共用几何和参考线语言。
  - 缺点：需要同步修改 runtime-owned scene state、telemetry 和 tests。
  - 验证影响：强。

选定方案：
方案 B。

Stack Equivalent：

- orchestrator priority chain -> `SpecialSceneFSM`
- `special_wide` confirm logic -> FSM 内部 candidate 状态
- scene-specific reference selection -> `ReferencePolicyResolver`

Named Deliverables：

- `new/code/legacy/steering_scene_fsm.hpp`
- `new/code/legacy/steering_scene_fsm.cpp`
- `new/code/legacy/steering_reference_policy.hpp`
- `new/code/legacy/steering_reference_policy.cpp`
- `new/code/runtime/runtime_state.hpp`

Failure Semantics：

- 若 `active_module=special_wide` 仍出现在正式运行时契约，则本决策失败。
- 若 FSM 仍直接生成控制量而不是只推进阶段，则职责边界失败。
- 若 reference policy 逻辑散落在 scene evaluators 内，则本决策失败。

Boundary Examples：

- circle entry confirmed -> FSM 进入 `circle_entry_repair`，reference policy 输出 inner-offset path
- circle interior -> FSM 保持 `circle_interior`，reference policy 允许 dual-edge blend
- cross whitelist hit -> FSM 本帧 veto circle candidate，但 ordinary/path geometry 不被重写

Contrast Structure：

- 采用：FSM 负责阶段，reference policy 负责路径。
- 不采用：模块顺序抢占 + scene evaluator 内联 reference 修改。

Verification Hook：

- `scene_classifier_selftest` 覆盖 ordinary bend veto、cross whitelist、circle 过程锁存
- board steering-media run 检查 `active_module/scene_phase/reference_mode`
- overlay probe 检查 reference path 切换可视化

### Decision: 控制层消费 BEV-native `ControlErrorModel` 与 `ControlConstraintSet`

问题：
当前控制误差模型基于像素域 `lateral_error/heading_error/curvature/highest_line`。如果 BEV 只是前端换皮，而控制层继续把 compatibility projection 当成真相，本轮重构的长期价值将被延后兑现。

备选方案：

- 方案 A：保留旧控制误差接口，仅由 compatibility projection 伪装 BEV 结果。
  - 优点：短期最稳。
  - 缺点：长期仍受 `highest_line` 和像素曲率语义约束，属于推迟而非完成重构。
  - 验证影响：中等偏差。
- 方案 B：将控制层的主输入改为 `near_lateral_error`、`far_heading_error`、`preview_curvature`、`visible_range`、`track_confidence` 和 `ControlConstraintSet`，旧字段仅保留为 debug projection。
  - 优点：真正完成 BEV-native ordinary control；为 future modules 影响控制预留正式入口。
  - 缺点：需要同步修改 PID 输入合同和 tuning/telemetry 面。
  - 验证影响：强。

选定方案：
方案 B。

Stack Equivalent：

- pixel lateral/heading/curvature -> `ControlErrorModel`
- `highest_line` gain scheduling -> visibility/confidence-based modulation; any surviving `highest_line` surface is compatibility/debug-only and not runtime authority
- ad hoc future control knobs -> `ControlConstraintSet`

Named Deliverables：

- `new/code/legacy/steering_control_error_model.hpp`
- `new/code/legacy/steering_control_error_model.cpp`
- `new/code/legacy/pid_control.hpp`
- `new/code/legacy/pid_control.cpp`
- `new/code/runtime/control_loop.cpp`

Failure Semantics：

- 若 PID 继续把 `highest_line` 作为长期主语义输入，则本决策失败。
- 若 future observation 只能通过修改 reference path 或手工改 PID 才能影响控制，则扩展性目标失败。

Boundary Examples：

- ordinary straight: control model 输出低曲率、低航向误差、正常 visible range
- circle interior: same controller path，只有 reference path 和 control constraints 改变
- friction observation: constraint set 下调 steering gain，而不是写死新的 PID 分支

Contrast Structure：

- 采用：BEV-native errors + constraints。
- 不采用：compatibility projection 驱动主控制。

Verification Hook：

- controller fixtures 对齐 near/far/curvature 输入项
- `control.steering_snapshot` 中的新控制误差与 raw/applied turn 对齐
- on-board smoke test 验证 ordinary 与 scene 阶段均走同一控制误差入口

### Decision: compatibility projection 只保留可观测能力，不保留主算法所有权

问题：
assistant、steering-media、selftest 和 overlay probe 现阶段仍依赖若干旧字段。完全删除这些字段会在重构期间破坏证据链，但继续把它们当主合同会阻碍长期架构收敛。

备选方案：

- 方案 A：立即删除全部旧字段。
  - 优点：语义最纯。
  - 缺点：迁移风险过高，调试/验证面会短期失明。
  - 验证影响：中等。
- 方案 B：保留最小 compatibility projection，但明确其只用于外部调试和过渡脚本，不得回流主链；同时加速删除 `special_wide` 等低价值正式语义。
  - 优点：兼顾可观测性与长期收敛。
  - 缺点：需要文档和 source review 强制边界。
  - 验证影响：强。

选定方案：
方案 B。

Stack Equivalent：

- BEV-first runtime truth -> `PerceptionResult`/`ControlDebugSnapshot` primary BEV fields
- legacy-friendly debug view -> compatibility projection sub-surface
- old host tooling -> updated scripts that know primary vs compatibility distinction

Named Deliverables：

- `new/code/port/control_types.hpp`
- `new/code/runtime/control_debug_snapshot.hpp`
- `new/code/platform/steering_media_protocol.*`
- `new/user/steering_media_capture.py`
- `new/user/tune_steering.py`
- `new/user/steering_media_selftest.cpp`

Failure Semantics：

- 若 compatibility fields 仍被 scene or control 读取，则本决策失败。
- 若调试/host tooling 无法区分 primary BEV fields 与 compatibility fields，则可观测性设计失败。

Boundary Examples：

- retained adapter fields: `steering_reference_col/highest_line/farthest_line`
- removed formal fields: `active_module=special_wide`, `special_wide_candidate_streak` public semantics
- primary BEV debug fields: `near_lateral_error/far_heading_error/preview_curvature/visible_range_m/reference_mode`

Contrast Structure：

- 采用：薄兼容层，仅对外投影。
- 不采用：继续把旧字段视作 steering 主合同。

Verification Hook：

- docs-first review：字段表清楚区分 primary/compatibility
- host script selftests：脚本消费新主字段且兼容投影只作展示
- source-first review：compatibility fields 无主链反向依赖

## Independent Verification Plan (STANDARD/STRICT)

文档与实现的统一审核遵循共享序列 `verify-sequence/default`，并使用以下共享 contract：

- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`

Stage A flow：

- docs-first checkpoints 以本 change 的 `proposal/specs/design/tasks` 为主表面。
- source-first checkpoints 以 BEV geometry、FSM、control model、compatibility projection 和相关 tests/scripts 为主表面。
- approved docs 在 source-first review 阶段作为 reference material 保留。
- `.index/` 如被提及，仅作背景资料，不承担 closure authority。

Runtime profile policy：

- verifier runtime profile 固定使用 `.codex/agents/verify-reviewer.toml`。

Loop rule：

- `active` agent 报告 `block` 后保持权威，直到同一 agent 返回 valid `pass`。
- `agent-table.json` 仅记录 current-state；恢复流程由 `continuation_probe` 承担。
- 只有 valid `active` `pass` 才允许结束对应 checkpoint。

### Review Checkpoints

- Shared sequence reference: `verify-sequence/default`
- Review goal: `implementation_correctness`
- Verifier agent path: `.codex/agents/verify-reviewer.toml`
- Invocation template id: `verify-reviewer-inline-v3`
- Default loop behavior:
  - resume `active` first
  - prefer `send_input` while that same `active` agent is still open
  - use `continuation_probe` to distinguish resume from recovery spawn
  - spawn when no usable `active` agent exists
  - repair follows `block`
  - only `block -> pass` marks `non_active`
  - final termination requires a valid `active` pass
- Authoritative verifier-subagent findings JSON path: `openspec/changes/bev-steering-perception-refactor/verification/<checkpoint>/attempt-<n>/findings.json`
- Verifier execution evidence JSON path: `openspec/changes/bev-steering-perception-refactor/verification/<checkpoint>/attempt-<n>/verifier-evidence.json`
- Agent table path: `openspec/changes/bev-steering-perception-refactor/verification/<checkpoint>/agent-table.json`
- Optional `.index/` summary path: not used by default for this change
- Continuation target on pass: next artifact or implementation checkpoint in the same orchestrated sequence

Checkpoint-specific primary surfaces：

- artifact-completion docs-first review: changed `proposal/specs/design/tasks`
- active-change source-first review checkpoint-1: BEV geometry truth, control-error cutover, observation assembly, compatibility projection boundaries
- active-change source-first review checkpoint-2: FSM, reference policy, state ownership, deletion of `special_wide`
- active-change source-first review checkpoint-3: control constraints, confidence/fail-safe behavior, runtime-owned reset/memory boundaries, and accelerated legacy ownership removal
- active-change source-first review checkpoint-4: steering snapshot/media, host tooling, overlay evidence, and board smoke/alignment bundle completeness
- active-change source-first review checkpoint-5: final implementation bundle covering changed code, changed tests, directly impacted code, and preserved evidence

## External Repository Index Reference (Optional)

本 change 默认不依赖 `.index/` 完成 verification。若后续需要仓库级索引作为人类或外部 AI 背景材料，只允许以 `.index/` 为 canonical root，并遵守其“非权威背景资料”定位；它不得输出 verifier verdict、repair routing 或 workflow closure authority。

## Migration Plan

1. 新建 BEV projector、sparse BEV geometry extractor、observation assembly 和 calibration artifact/table，使 host-side fixtures、overlay probe 和 board evidence 能从同一 projector id/hash 重建原图/BEV 双视图与 sampled path。
2. 在 `port` 与 `runtime-owned state` 中引入 `BEVTrackEstimate`、`BEVSceneObservation`、`VehicleContext`、`ControlConstraintSet`、compatibility projection、`ControlErrorModel` 输入/输出和新的 FSM/reference state，同时将旧 `LaneMetrics`/bottom tracker 面标记为移除或兼容投影参考，不再允许新增主语义字段。
3. 在第一阶段完成 raw frame -> BEV geometry -> BEV-native control error -> `pid_control`/`control_loop` 的直接 authority cutover，保留 `turn_output -> wheel_target_mixer -> wheel PID -> PWM` 末端，但不使用旧像素域 ordinary 链作为 shadow oracle 或 fallback truth。
4. 用 `SpecialSceneFSM + ReferencePolicyResolver` 替换当前 orchestrator priority chain，并删除 `special_wide` 的正式模块语义；必要的 candidate 痕迹仅保留为 debug-only。
5. 更新 `steering-media`、`assistant`、`overlay probe`、selftest 和 host tooling，使 primary BEV 字段成为主解释面；同步迁移脚本和 evidence bundle。
6. 在 BEV 主链通过 source-first 与 on-board smoke 之后，加速删除低价值旧资产：
   - `active_module=special_wide`
   - public `special_wide_*` 状态语义
   - ordinary 主链中的 bottom tracker 正式角色
   - 仅服务旧 score competition 的参数和字段
   - `highest_line` 作为主控制输入的长期角色

Rollback strategy：

- 单次实现步骤可按提交粒度回退，但架构上不保留“长期双真相模式”作为 accepted rollback。
- 若某个中间提交需要回退，只能回到前一个明确的单真相/单入口稳定点，不接受“ordinary 继续走旧链、scene 继续走新链”的长期中间态。

## Open Questions

- host-side calibration artifact 的最终承载形式是独立校准脚本输出，还是并入现有 `debug.sh` / steering-media evidence workflow；本 change 已固定“必须有 accepted artifact/table”，但未固定其具体人机入口。
- `near/far` 采样距离的最终数值是否需要按车速区间分档；当前设计只固定误差模型形态，不固定最终数值表。

## Risks / Trade-offs

- IPM 标定误差会放大为 ordinary 与 scene 的共性误差；这是单真相架构的代价，也是必须尽早暴露和校准的问题。
- aggressive deletion 会让迁移窗口更短，短期内脚本与 tests 修改量更大，但可以避免长期双真相维护成本。
- control error model 从像素域切换到 BEV-native 后，现有 tuning 经验需要重建一部分；收益是后续调参将围绕车体坐标和参考路径，而不是像素行列近似。
- compatibility projection 若设计不严，会诱导实现者继续依赖旧字段；因此本 change 必须在 docs-first 和 source-first 两轮都专门审查“compatibility 不回流”。
