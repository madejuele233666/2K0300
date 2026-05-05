# BEV Simple Reference Extension Rules

本文档定义 `bev-simple-reference-extension` 分支后续扩展必须继承的架构约束。
当前参数合同见 `new/config/default_params.md`，port 类型与 include 边界见 `new/code/port/README.md`；本文档只补充后续扩展必须遵守的设计边界。

当前基线 `04d6da13b Simplify BEV reference control contract` 是“干净基础寻线”基线。后续扩展可以增加能力，但不能破坏现有分层语义，不能把旧 topology / policy / scene / trusted / memory 体系带回 active runtime。

## 1. 思想

### 大道至简

active 视觉输入可以分成 sparse BEV facts 与 BEV element raster 两个输入面；下游控制链路固定为：

```text
camera frame
-> Otsu threshold
-> sparse BEV row scans
     ├─ line reference builder
     │    -> line BEVReferencePath candidate
     └─ sparse-derived visual evidence
          -> cross visual evidence
             -> element candidate builders

camera frame
-> BEV element raster
     ├─ circle visual evidence
     ├─ roadblock visual evidence
     └─ ML visual evidence
          -> element candidate builders

line candidate + element candidates
-> visual reference orchestration
-> current visual BEVReferencePath / source / reason
-> reference continuity / hold selection
-> selected BEVReferencePath
-> reference usability
-> reference lateral error
-> reference-control readiness
-> safety gate
-> turn-output target
-> gyro feedback / actuator
-> debug transport
```

后续 extension 的第一判断标准不是功能是否复杂，而是每一层是否只做自己该做的事。

### 互不知晓

各层只消费上一层的明确输出，不窥探别层内部语义：

- `reference_path` 只表达白点事实。
- `present` 只表达白点事实存在。
- `source` / `mode` 只用于 debug、overlay、protocol，不参与可用性、lateral error、控制、安全决策。
- `usable` 只属于 reference usability 层。
- `computed` 只属于 reference lateral-error 层。
- `ready` 只属于 reference-control readiness 层。
- safety gate 是唯一 safety owner。
- yaw/turn controller 不知道 validity，不知道 source/mode，不知道 `PerceptionResult`。

### 事实优先

后续扩展必须先产生可解释视觉事实，再基于事实生成路径。

正确顺序：

```text
视觉事实 -> 元素证据 -> reference facts -> usability / lateral error / control
```

禁止顺序：

```text
控制想要什么路径 -> 用 score / memory / strategy 补一个看似合理的证据
```

### Debug 不是 Authority

debug、overlay、media、assistant telemetry 只能序列化事实，不能参与 runtime 决策，不能重新推导 speed、reference、safety、element state。

白线只表示 selected reference path。不要再画“控制保护路径”“trusted error blend”“旧候选路径”来混淆语义。

## 2. 原则

### 视觉输入

- 不改 BEV 标定。
- 不改纵向尺度修正。
- runtime 视觉输入面包括 sparse BEV row scans 和 BEV element raster。
- sparse BEV row scans 是 line reference builder 和 sparse-derived evidence 的输入。
- BEV element raster 是 circle、roadblock、ML 等元素 evidence detector 的输入，必须携带投影/采样可用性信息。
- BEV element raster 与 debug/overlay 输出必须分离命名和接口，debug 输出不能反向参与 runtime 决策。
- visual reference orchestration 是唯一把 line candidate 与 element candidates 汇总成 current visual `BEVReferencePath` 的层。
- reference continuity / usability / lateral error / readiness / safety / yaw 链路只消费 orchestration 后的 selected reference facts，不直接消费 raster 或元素内部细节。

### 采样与分类

- sparse scan 只处理 BEV metric sample。
- sample 投影状态只表达是否能采样：
  - `kSampleable`
  - `kOutsideFrame`
  - `kProjectionFailed`
- `OutsideFrame` / `ProjectionFailed` 不得变成 edge、opening、元素或路径证据。
- 分类只基于中心像素亮度与阈值，输出 white / black / unknown / invalid。
- 图像边缘、扫描边界、FOV 边界都不能冒充 observed boundary 或元素事实。

### Reference

- `BEVReferencePath` 表达“从 index 0 开始的近端连续控制路径”。
- 当前 strict builder 不补点、不插值、不跨 gap、不把远端点接回近端。
- index 0 没有 interval，则当前视觉 reference 无事实。
- 中间第一处断点之后全部 `present=false`、`source=kNone`。
- hold 只能复制上一帧 leading present 段，source 必须重写为 `hold`。
- hold 是 continuity 层事实，不是视觉事实。
- hold 不得覆盖 last visual reference。
- hold 是否可控仍要经过 usability 判断。

### Control

- `ReferenceControlReadiness` 只判断 selected reference + lateral error 是否足够进入控制。
- low voltage、projector/perception health、stale、IMU、encoder 只属于 safety gate。
- safety gate 主因优先级固定为：

```text
low_voltage
perception_stale
perception_invalid
reference_control_not_ready
imu_invalid
encoder_invalid
none
```

- gate veto、emergency stop、hold disarmed、no-drive 时，不调用 yaw/turn controller，不更新 yaw/turn memory。
- turn-output target 只由 weighted lateral error + speed target 计算。
- gyro feedback 只在 gate clear 且 motion allow-drive 时运行。

### Low Voltage

- `low_voltage_raw_threshold` 属于 power adapter / low-voltage sampler owner。
- 默认 fail-safe threshold 是 `400`。
- `LS2K_LOW_VOLTAGE_RAW_THRESHOLD` 是硬件调试 override，优先级高于 runtime params。
- env override 只接受正整数；非法值不能关闭保护。
- `LowVoltageSample.threshold` 记录实际使用阈值。
- `low_voltage_sample_interval_ms` 只控制采样周期，不配置阈值。
- startup 首次 low-voltage sample 前必须先配置 threshold。

### 参数

- `default_params.json` 是人工编辑的当前运行默认合同。
- `RuntimeParameters{}` 是缺文件 / 解析失败 fallback 镜像，必须保持一致。
- 不保留旧 JSON alias。
- 有范围的参数必须在参数合同层显式校验；公式层不得偷偷 clamp 或改写语义。
- 参数名必须表达当前真实语义：
  - `RUNNING_SPEED_TARGET`
  - `YAW_RATE_PID`
  - `low_voltage_raw_threshold`
  - `BEV_GEOMETRY`
  - `BEV_CLASSIFICATION`
  - `BEV_CONTROL_MODEL`
  - `BEV_ELEMENT`

### 元素扩展

后续恢复 cross / circle / roadblock 时，必须作为新的 BEV visual element evidence 引入：

- cross 只能来自横穿白线等明确 BEV 图像事实。
- circle 只能来自明确视觉事实，不能继承旧 opening / memory 伪证据。
- roadblock 后续如果恢复，也应作为类似 cross/circle 的道路元素 evidence，不是 debug 占位字段。
- ML 输出必须先落成可解释 BEV metric visual evidence，不能直接输出运行模式、控制模式或 actuator 意图。
- 元素识别只负责识别事实，不直接控制 actuator。
- visual reference orchestration 只负责把当前帧视觉事实汇总成 current visual reference candidate，不负责 hold、安全、yaw 或 actuator。
- 元素识别后的路径策略必须先进入 current visual reference facts，再经过 reference continuity / usability / lateral error / safety gate 链路。

### 元素 Evidence 层

元素 evidence 层位于视觉采样和 visual reference orchestration 之间，负责把原始视觉输入转成可解释、可测试、可序列化的 BEV metric element facts。

```text
sparse BEV row scans / BEV element raster
-> element visual evidence detector
-> element visual evidence
-> element candidate builder
-> VisualReferenceCandidate
-> visual reference orchestration
```

职责边界：

- detector 只判断当前帧视觉事实，例如 cross / circle / roadblock / ML grounded evidence 是否存在、置信度、BEV metric 位置范围、投影/采样可用性和 reason。
- detector 不生成 `BEVReferencePath`，不读 hold memory，不读 safety、IMU、encoder、low voltage、actuator 或 yaw memory。
- candidate builder 是唯一允许把 element evidence 转成 `VisualReferenceCandidate` 的元素层组件。
- candidate builder 必须构造从 index 0 开始的近端连续 `BEVReferencePath`；不能跨 gap、不能补远端点、不能把 unknown / image border / FOV boundary 当路径事实。
- candidate 的 `source` / `reason` / `confidence` 只用于 orchestration debug 和仲裁解释，不能被 usability / lateral error / readiness / safety / yaw 读取。
- 第一版新增元素必须默认只进入 evidence/debug；candidate 可以构造但 takeover 默认关闭，直到对应 detector、candidate builder、离线帧和受控发车证据都稳定。
- ML 只能落成 BEV metric visual evidence；不得绕过 candidate builder 直接输出模式、路径接管结论、速度、转角或 actuator 意图。

## 3. 代码细节

### 核心文件边界

`steering_bev_simple_perception.*`

- 只做 sparse BEV row scan、white intervals、line current visual reference candidate。
- 不 include / 不依赖 reference usability。
- 不负责 BEV element raster 编排，不负责 circle / roadblock / ML evidence。
- 不选择 hold。
- 不判断 control。

`steering_bev_element_raster.*`

- 负责从 camera frame / BEV projector 构建 BEV element raster。
- 必须保留每个 raster sample 的投影/采样可用性，FOV 外、投影失败、图像边缘不能冒充元素事实。
- 只服务 visual element evidence detector，不直接生成 selected reference、不做 hold、不判断 control。

`steering_cross_exit_element_evidence.*`

- 负责 cross grounded element evidence detector 和对应 candidate builder。
- cross 行为默认不接管 arbitration，除非显式打开 `BEV_ELEMENT.CROSS_EXIT_TAKEOVER_ENABLED`。

`steering_visual_element_pipeline.*`

- 负责汇总 cross / circle / roadblock / ML grounded element evidence detector 和对应 candidate builder。
- detector 输出 element evidence；candidate builder 输出 `VisualReferenceCandidate`。
- 不直接写 `PerceptionResult`，不直接选择 selected reference，不执行 hold，不判断 usability / lateral error / readiness / safety / yaw。
- 每个元素必须有独立测试覆盖 absent、low confidence、invalid geometry、gap rejection、candidate takeover disabled 等情况。

`steering_visual_reference_orchestration.*`（后续扩展）

- 唯一汇总 line candidate、cross / circle / roadblock / ML element candidate 的 current visual reference owner。
- 输出 current visual `BEVReferencePath`、`source`、`reason` 和只用于 debug/selection 的解释字段。
- 不读取 safety、IMU、encoder、low voltage、actuator、yaw memory。
- 不执行 hold；hold 仍属于 reference continuity 层。

`steering_reference_usability.*`

- 唯一判断 leading reference 是否 usable。
- 只看 `present + finite geometry + configured leading sample count`。
- 不看 source/mode。

`steering_reference_lateral_error.*`

- 只从 selected reference facts + usability 计算 weighted lateral error。
- 不依赖 speed。
- 不看 source/mode。
- 不读 `PerceptionResult`。

`steering_reference_control_readiness.*`

- 只看 selected usability、lateral error、hold_selected。
- 不接收 low voltage、projector、IMU、encoder、stale。
- hold ready 时 degraded reason 为 `reference_hold`。

`perception_frontend.*`

- runtime capture/publish lifecycle owner。
- 负责 camera capture、fault injection、empty-frame fallback、state publish、memory reset。
- 不直接调用 cross / circle / roadblock / ML detector。

`steering_frame_perception_pipeline.*`

- runtime single-frame perception owner。
- 负责调用 sparse perception、BEV element raster / evidence、visual reference orchestration 和 reference continuity。
- 负责 current / hold / none selection，其中 hold 仍只属于 reference continuity 层。
- 唯一组装 `PerceptionResult`。
- 不拥有 IMU safety memory。

`control_decision.*`

- safety gate owner。
- 独占 low voltage、perception health、stale、IMU、encoder veto。

`steering_yaw_controller.*`

- turn-output target / gyro feedback owner。
- 不 include `perception_result.hpp`。
- 不接收 `control_valid`。
- 不接收 `imu_valid`。
- 不读取 readiness/source/mode。

`power_adapter.cpp`

- 低电压 raw threshold owner。
- configured threshold 非法时回退 `400`。
- env override 非法时忽略 env。
- `LowVoltageSample.threshold` 记录实际阈值。

`low_voltage_sampler.*`

- 只配置和执行低频采样周期。
- 不配置 threshold。

### 类型语义

- `BEVPathSample::present`：白点事实存在。
- `BEVPathPointSource`：`kNone`、`kIntervalCenter`、`kHold`。
- `ReferenceMode`：`kNone`、`kIntervalCenter`、`kHoldLast`。
- `ReferenceUsability::usable`：reference 是否足够连续。
- `ReferenceLateralErrorEstimate::computed`：weighted lateral error 是否算出。
- `ReferenceControlReadiness::ready`：reference + lateral error 是否足够进入控制。
- `ControlGateDecision::veto_active`：safety gate 是否禁止控制。
- `PerceptionResult`：runtime transport snapshot，不是依赖捷径。

### Debug / Media 合同

debug JSON 分组应保持：

```json
{
  "perception_health": {},
  "element_evidence": {},
  "reference": {},
  "eligibility": {},
  "lateral_error": {},
  "reference_control": {},
  "safety_gate": {},
  "degraded": {},
  "yaw_control": {},
  "actuator": {}
}
```

steering media config snapshot 必须暴露当前真实参数，包括：

```json
{
  "running_speed_target": 100.0,
  "yaw_rate_pid": { "p": 0.5, "i": 0.0, "d": 0.0 },
  "low_voltage_sample_interval_ms": 1000,
  "low_voltage_raw_threshold": 400,
  "BEV_PROJECTOR": {},
  "BEV_GEOMETRY": {},
  "BEV_CLASSIFICATION": {},
  "BEV_CONTROL_MODEL": {},
  "BEV_ELEMENT": {}
}
```

注意：config snapshot 中的 `low_voltage_raw_threshold` 是 runtime 参数；实际阈值以 `LowVoltageSample.threshold` 为准。
`control.steering_internal` 只用于 tuning evidence，必须标识为 internal debug，不能作为 authority 回推 runtime 事实。

### Archive / Authority 边界

- `new/code/archive/**` 只保留历史参考，不参与 build/test。
- `docs/archive/**` 只保留历史文档，不参与 active authority。
- `new/docs/superseded/**` 只保留历史路线图和草稿，不参与 active authority。
- `new/verification/archive/**` 只保留历史证据，不参与 active authority。
- `authority-baseline/manifest.json` 是历史 manifest，必须留在 archive。
- active `authority-baseline` 目录只允许 raw `*.raw` 作为当前输入样本。
- historical manifest / overlay / txt 不能作为 active verification authority。

### 必跑回归

后续 extension 每次触碰相关链路，至少运行：

```bash
rtk bash new/verification/tests/run_runtime_parameter_defaults_test.sh
rtk bash new/verification/tests/run_power_adapter_threshold_test.sh
rtk bash new/verification/tests/run_startup_low_voltage_order_test.sh
rtk bash new/verification/tests/run_bev_simple_perception_test.sh
rtk bash new/verification/tests/run_visual_reference_orchestration_test.sh
rtk bash new/verification/tests/run_reference_usability_lateral_error_test.sh
rtk bash new/verification/tests/run_assistant_telemetry_selftest.sh
rtk bash new/verification/tests/run_steering_media_selftest.sh
rtk bash new/verification/tests/run_bev_simple_residual_check.sh
rtk bash new/verification/tests/run_perf_counter_test.sh
rtk env SKIP_UPLOAD=1 new/user/build.sh
rtk git diff --check
rtk code-index refresh
```

## 4. 禁止做的事

### 禁止恢复旧 Topology 架构

不要恢复或重新引入 active：

- topology map
- corridor interval / graph
- topology evidence score
- opening score
- scene FSM
- road hypotheses
- reference policy
- trusted compatibility gate
- trusted error blend
- stable boundary offset
- circle inner island memory follow
- old cross/circle derived score
- camera PID / fuzzy PID
- old `control_types.hpp`

如果要做 cross/circle/roadblock，必须按新 evidence 层重新设计。

### 禁止让 Debug 参与决策

禁止：

- assistant/status 重新推导 speed。
- media/header 反向影响 runtime。
- overlay probe 另写一套和 runtime 类似但分叉的 pipeline。
- 用 debug 字段决定 control。
- 用 `reference.source` 或 `reference.mode` 判断 usable / lateral error / readiness / gate / yaw。

### 禁止混淆路径语义

禁止：

- 白线表示 trusted path、control error blend、旧候选路径或未选路径。
- hold 点伪装成当前视觉点。
- 无 evidence 时远端外推。
- 跨 gap 插值。
- index 0 断裂后使用远端点控制。
- 把 `Unknown`、`Invalid`、图像边缘、搜索边界当边线或元素证据。

### 禁止让 Safety 泄漏到非 Safety Owner

禁止：

- perception 判断 low voltage。
- perception 持有 IMU grace / IMU safety memory。
- reference-control readiness 判断 low voltage/projector/stale/IMU/encoder。
- yaw/turn controller 接收 `control_valid` 或 `imu_valid`。
- low-voltage threshold 进入 perception/reference/lateral-error/yaw。

### 禁止保留旧参数兼容 Alias

禁止 active JSON / docs / scripts / media 出现：

- `emergency_threshold`
- `Speed_base`
- `see_max`
- `PID_TURN_CAMERA`
- `PID_TURN_GYRO_CAMERA`
- `pid_turn_camera_*`
- `pid_turn_gyro_camera_*`
- `P_Mode`
- `w_target`
- `assistant_image_publish_interval_ms`
- `assistant_waveform_publish_interval_ms`

### 禁止 Active Authority 污染

禁止：

- active `authority-baseline/manifest.json` 回流。
- active verification 使用历史 overlay/txt 证明当前成功。
- archive 文件参与 build/test/residual authority。
- 把旧 cross/circle/trusted/memory 输出当当前合同证据。

### 禁止模糊命名继续扩散

后续新增字段必须是事实名，不要泛化：

- 不要新增不清楚归属的 `valid`。
- reference 层用 `present` / `usable`。
- lateral-error 层用 `computed`。
- reference-control 层用 `ready`。
- safety 层用 `veto_active`。
- debug snapshot 若后续清理，应把泛化 `valid` 改成 `has_*` 语义。

### 禁止为了“看起来效果好”扩大复杂度

尤其禁止：

- 为了修个别图像效果恢复复杂 score。
- 为了路径连续性在 builder 层预留空 options。
- 在 simple perception 里做 hold、usability、lateral error 或 control。
- 在路径层补救元素证据层断裂。
- 用平滑算法掩盖底层白点/边界证据错误。
- 为特殊场景开硬编码模板，除非先有明确视觉事实与测试夹具支撑。
