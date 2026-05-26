# Visual Element Sparse Circle V2 环岛状态机

本文档定义 circle V2 的最小环岛状态机和落地边界。V2 继承 V1 的 sparse-first 视觉事实面和原有 Phase1 环岛判定语义，但删除 V1 Phase2 中基于后方黑色支撑的入口寻线方案。

## 1. 目标

V2 的目标是把 circle 从“单帧入口 candidate”推进到最小完整状态机：

```text
Idle -> A -> B -> C -> Idle
```

必须达成：

- 保留原有 Phase1 环岛判定语义。
- 删除 rear / side-rear black frontier 作为内圆或入口路径的判定标准。
- 方向 `X` 由原有环岛判定给出，`X` 取 `left` 或 `right`。
- `A` 只能从 `Idle` 进入。
- `C` 保持一段时间，同时承担防止再次进入同一环岛的职责。
- 不新增独立 cooldown 状态。
- `CircleV2Scene` 是 circle 场景解释器，独占 circle detection telemetry、状态机和环岛 reference plan。
- `RunVisualElementPipeline()` 不再负责 circle evidence、circle state 或 circle candidate。
- 外部只通过稳定公共事实面调用 `CircleV2Scene`，不读取内部过渡事实。
- `CircleV2Scene` 输出 scene reference plan；由 adapter 包装为 `VisualReferenceCandidate` 后进入现有 arbitration。

不做：

- 不重新定义 Phase1 circle detector。
- 不把“存在后黑”作为内圆标准。
- 不为了旧兼容性保留废弃的 circle evidence / circle_entry 运行时语义。
- 不增加隐式兜底、置信度或复杂防御性策略。InnerTrace 长时间无有效转角的退回只作为显式参数化 stall fallback。
- 不对外发布 `left_open`、`right_straight`、`bottom_expansion`、`a_to_b` 等中间事实 API。

## 2. 保留的原环岛判定

V2 不替换现有环岛识别逻辑。`Idle -> A` 仍使用当前算法给出的环岛方向：

```text
detect_circle(X)
```

语义保持：

```text
circle_left  = 当前算法判定到左环岛
circle_right = 当前算法判定到右环岛
```

这里保留的是 Phase1 判定语义，不是旧模块归属。落地后，circle 的 Phase1 cue 应由 `CircleV2EventObserver` 或其私有 helper `ObserveCirclePhase1Cue` 产生，而不是由 `RunVisualElementPipeline()` 输出 circle evidence record。

## 3. 删除后黑判定

V1 Phase2 中的 rear / side-rear black frontier 不再作为环岛入环路径的定义。

删除的判定语义：

```text
当前点为 white
rear / side-rear 点为 black
=> 该点属于 circle entry frontier
```

V2 不再用这个规则寻找内圆，不再把“存在后黑”当作内圆边线、入口边线或环岛内部路径的必要条件。

V2 的内圆边线标准改为：

```text
从当前中线向环岛方向 X 侧搜索边线；
选择离中线最近的 X 侧边线；
将其视为内圆边线。
```

## 4. 优雅架构原则

V2 的优雅落地方式不是新增一个对外 facts API，也不是把状态逻辑分散到 detector、element pipeline、reference builder 和 control。V2 采用单一 circle 场景模块：

```text
CircleV2Scene
```

它的定位是：

```text
circle 场景解释器
```

不是：

```text
VisualReferenceCandidate 反向消费者
MotionHistory 容器消费者
旧 visual element circle evidence 的兼容壳
```

### 4.1 单一 circle 语义所有者

系统中只能有一个地方拥有 circle 语义：

```text
CircleV2Scene
```

`CircleV2Scene` 负责：

```text
Phase1 circle cue
entry gate
inner trace
exit trace
release hold
circle telemetry
circle reference plan
```

`RunVisualElementPipeline()` 负责：

```text
cross evidence
cross candidate
other non-circle visual element evidence
```

`RunVisualElementPipeline()` 不再负责：

```text
circle evidence record
circle_entry candidate
circle_entry diagnostics
Idle/A/B/C
```

### 4.2 稳定公共事实面

`CircleV2Scene` 只读取稳定公共事实面：

```text
SceneFrameView
CircleV2Memory
CircleV2Params
```

`SceneFrameView` 包含：

```text
rows
ordinary_road
motion_arc
stamp
```

它不包含：

```text
VisualReferenceCandidate line_candidate
MotionHistory concrete container
element pipeline circle evidence
reference arbitration result
control / safety state
```

### 4.3 场景事实私有

V2 仍遵守“事实优先”：

```text
公共观测面 -> 场景观察 -> 状态事件 -> 状态推进 -> reference plan
```

但 circle 中间事实只能在 `CircleV2Scene` 内部存在，不泄漏成跨模块 API。

不对外暴露：

```text
left_open
right_open
left_straight
right_straight
bottom_left_expansion
bottom_right_expansion
nearest_inner_edge
outer_straight_edge
turn_angle_build_detail
a_to_b
b_to_c
c_to_idle
```

### 4.4 状态机事件化与几何后置

FSM 不直接读取 `bottom_expansion`、`turn_angle_since_b_enter_rad` 或 edge trace。FSM 只读取 scene event：

```text
detected_dir
entry_gate_reached
exit_gate_reached
```

也就是说：

```text
ObserveEvents:
  把视觉和运动事实解释成状态转移事件。

Reducer:
  只根据 prior memory、event、阶段生命周期和参数推进 phase。

ObserveGeometry:
  根据 Reducer 已经决定的本帧 reference role 查找所需几何。

ReferenceComposer:
  只把几何观察组合成 reference plan。
```

事件观察和几何观察必须分开。事件服务状态转移，几何服务本帧最终 reference role。这样避免在 `prior = InnerTrace` 但本帧转入 `ExitTrace` 时，提前按旧阶段计算错误的几何。

## 5. 公共类型边界

### 5.1 SceneFrameView

`CircleV2Scene::Step()` 的公共输入是：

```cpp
template <typename T>
class ConstArrayView {
 public:
  ConstArrayView(const T* data, std::size_t size);

  const T* data() const;
  std::size_t size() const;
  const T& operator[](std::size_t index) const;
  bool empty() const;

 private:
  const T* data_;
  std::size_t size_;
};

struct BevRowsView {
  ConstArrayView<BEVSimpleRowScan> rows;
};

struct RoadHalfWidth {
  float value_m;
};

struct OrdinaryRoadModel {
  port::BEVReferencePath center_path;
  RoadHalfWidth half_width;
};

using YawDeltaQuery =
    bool (*)(void* context,
             uint64_t from_ms,
             uint64_t to_ms,
             float& out_delta_rad);

class MotionArcView {
 public:
  MotionArcView(void* context, YawDeltaQuery query);

  bool TryYawDeltaRad(uint64_t from_capture_time_ms,
                      uint64_t to_capture_time_ms,
                      float& out_delta_rad) const;

  float YawDeltaRad(uint64_t from_capture_time_ms,
                    uint64_t to_capture_time_ms) const;

 private:
  void* context_;
  YawDeltaQuery query_;
};

struct CaptureStamp {
  uint64_t capture_time_ms;
};

struct SceneFrameView {
  BevRowsView rows;
  std::optional<OrdinaryRoadModel> ordinary_road;
  MotionArcView motion_arc;
  CaptureStamp stamp;
};
```

`SceneFrameView` 是强契约输入。调用 `CircleV2Scene::Step()` 的前提是组合层已经构造完成 `rows`、`ordinary_road`、`motion_arc` 和 `stamp`。缺少这些结构性输入不是 scene 的业务分支。

`ConstArrayView` 是 C++17 下的 `std::span` 等价边界表达。它用于表达非拥有数组 view；构造层负责保证 `data != nullptr` 且 `size > 0`，scene 内不写空指针防御分支。

`SceneFrameView` 的不变量：

```text
rows 非空。
rows 与 ordinary_road.center_path 来自同一帧。
rows 与 ordinary_road.center_path 使用同一 BEV 坐标系。
rows 的 forward_m 顺序稳定。
ordinary_road.half_width 已由组合层构造完成。
stamp.capture_time_ms 对应 rows / ordinary_road 的同一图像采集时刻。
SceneFrameView 内所有 view 只在 Step 调用期间有效。
CircleV2Scene 不得跨帧保存 SceneFrameView 内引用。
```

`ordinary_road.half_width` 在 `OrdinaryRoadModel` 存在时必须已经构造完成。当前实现由 `BEV_GEOMETRY.NOMINAL_ROAD_HALF_WIDTH_M` 注入该事实；`BuildOrdinaryRoadModel()` 只负责把普通中心路径和稳定半路宽组装成公共道路模型，不再从本帧 rows 的白色 interval 宽度实时估计半路宽。scene 内不猜测半路宽，也不使用 magic constant。若 active scene 中 `ordinary_road` 暂不可用，几何层只能让本帧 `reference_plan = nullopt` 并报告 geometry unavailable；它不能改变 Reducer 状态。

`MotionArcView` 是能力接口，不是历史容器。它表达：

```text
给定两个图像时间戳，查询这一段的 yaw delta。
```

它不表达：

```text
IMU adapter
MotionHistory ring buffer
control tick 细节
IMU safety policy
```

`MotionArcView` 是 type-erased view。它可以包住 `MotionHistory` snapshot、yaw integration helper 或其它等价 motion source，但这些具体类型不进入 `SceneFrameView`。`TryYawDeltaRad()` 返回不可用时，EventObserver 不能推动 `InnerTrace -> ExitTrace`，也不能因此重置状态。

如果系统无法在整个 circle 生命周期内构造可查询的 `MotionArcView`，则不得注册 `CircleV2Scene`。在 `CircleV2Memory.phase == kIdle` 时，组合层可以因为 scene 未注册而不调用 `Step()`；一旦进入非 Idle 阶段，组合层必须持续调用 `CircleV2Scene::Step()`，不能因为某一帧 motion 输入不可用而静默跳过。

### 5.2 CircleV2Memory

`CircleV2Memory` 只保存状态机跨帧必须锁存的内容：

```cpp
enum class CirclePhase {
  kIdle,
  kApproach,
  kInnerTrace,
  kExitTrace,
};

struct CircleV2StageClock {
  uint64_t enter_capture_time_ms = 0;
  int phase_frame_index = 0;
  float max_directed_turn_angle_rad = 0.0F;
};

struct CircleV2Memory {
  CirclePhase phase = CirclePhase::kIdle;
  CircleDir dir = CircleDir::kNone;
  CircleV2StageClock clock{};
};
```

进入新阶段时统一设置：

```cpp
void EnterPhase(CircleV2Memory& memory,
                CirclePhase phase,
                CaptureStamp stamp) {
  memory.phase = phase;
  memory.clock = {};
  memory.clock.enter_capture_time_ms = stamp.capture_time_ms;
}

void EnterIdle(CircleV2Memory& memory) {
  memory.phase = CirclePhase::kIdle;
  memory.dir = CircleDir::kNone;
  memory.clock = {};
}
```

代码阶段名使用语义化命名：

```text
A = Approach
B = InnerTrace
C = ExitTrace
```

`clock.enter_capture_time_ms` 是当前阶段的时间锚点。`phase_frame_index` 在 `prior memory` 中表示本帧开始时该 phase 已经完整输出过的帧数。`max_directed_turn_angle_rad` 只记录当前 InnerTrace 阶段已经达到过的最大方向归一化转角，用于避免车辆已经明显绕环后因净 yaw 回落而触发“无明显积分”兜底。Reducer 内部 `EnterPhase()` 后，`current.clock.phase_frame_index = 0`，表示本帧是新 phase 的第 0 帧。如果本帧输出了该 phase reference，且本帧结束后仍保持该 phase，则 `next_memory.clock.phase_frame_index = current.clock.phase_frame_index + 1`。因此 `next_memory` 保存的是下一帧开始时的 `phase_frame_index`。

Reducer 必须保持状态不变量：

```text
phase == Idle  <=>  dir == None
phase != Idle  =>   dir == left 或 right
```

`EnterPhase()` 不负责选择方向，只负责阶段生命周期重置。从 `Idle` 进入非 Idle 阶段时，Reducer 必须先锁存 `dir`，再调用 `EnterPhase()`。

### 5.3 CircleV2Params

`CircleV2Params` 只包含业务参数：

```cpp
struct CircleV2Params {
  float exit_yaw_threshold_rad = kDefaultCircleV2ExitYawThresholdRad;
  int exit_hold_frames = 60;
  int inner_trace_stall_timeout_ms = 4000;
  float inner_trace_stall_yaw_min_rad = kDefaultCircleV2InnerTraceStallYawMinRad;
  float inner_trace_path_offset_m = 0.0F;
};
```

`CircleV2Params` 不得提供危险零默认值。类型默认值可以是安全业务默认值，运行时仍由 `param_store` 或等价配置构造覆盖；默认 330 度属于配置语义，不能退化为 `exit_yaw_threshold_rad = 0`。

约束：

```text
exit_hold_frames >= 2
```

原因是 `InnerTrace -> ExitTrace` 的进入帧已经输出 `ExitTrace` reference。若允许 `exit_hold_frames = 1`，本帧结束后的 memory 会直接进入 `Idle`，外部观察会形成非兜底语义的 `InnerTrace -> Idle`，破坏主链路状态转移语义。

不包含：

```text
enabled
```

是否启用 `CircleV2Scene` 由组合层决定。scene 一旦被调用，就默认参与本帧解释。`CIRCLE_V2_ENABLED` 只用于 scene registry / 启动期组合；不支持 active runtime 无声热切换。

### 5.4 CircleV2StepResult

`CircleV2Scene` 不直接输出 arbitration candidate，而是输出 circle reference plan：

```cpp
struct CircleV2ReferencePlan {
  CircleDir dir;
  CircleV2ReferenceRole role;
  port::BEVReferencePath reference_path;
};

enum class CircleV2TelemetryReason {
  kNone,
  kPhase1CueLeft,
  kPhase1CueRight,
  kEntryGateReached,
  kExitGateReached,
  kInnerTraceYawStalled,
  kExitHoldReleased,
  kGeometryUnavailable,
};

struct CircleV2Telemetry {
  CirclePhase frame_phase = CirclePhase::kIdle;
  CirclePhase next_phase = CirclePhase::kIdle;
  CircleDir dir = CircleDir::kNone;
  CircleV2ReferenceRole reference_role = CircleV2ReferenceRole::kNone;
  CircleV2TelemetryReason reason = CircleV2TelemetryReason::kNone;
};

struct CircleV2StepResult {
  CircleV2Memory next_memory{};
  std::optional<CircleV2ReferencePlan> reference_plan{};
  CircleV2Telemetry telemetry{};
};
```

`CircleV2TelemetryReason` 是稳定枚举。media / probe / overlay 若需要文本，由显示层统一把 enum 转成字符串；测试不应依赖自由文本。

`frame_phase` 表示本帧可见的 scene phase，`next_phase` 表示本帧结束后写入 `next_memory` 的 phase。例如 `ExitTrace` 的最后一帧：

```text
reference_plan.role = ExitTrace
telemetry.frame_phase = ExitTrace
telemetry.next_phase = Idle
```

`VisualReferenceAdapter` 负责把 `CircleV2ReferencePlan` 包装成现有系统候选：

```text
CircleV2ReferencePlan
-> VisualReferenceCandidate
```

这样 `CircleV2Scene` 只表达“我要走哪条路径”，不直接依赖 reference arbitration 的包装语义。

`CircleV2ReferencePlan` 不包含 confidence。状态机是否输出 `std::optional<CircleV2ReferencePlan>` 已经表达 scene 是否接管 reference；adapter 根据 `role` 固定映射 source 和 special candidate 语义，不再引入隐式打分策略。

`reference_plan` 的 absent 条件：

```text
Idle / Approach:
  reference_plan = nullopt。

InnerTrace:
  required inner geometry 可构造时输出 plan，否则 nullopt。

ExitTrace:
  required outer geometry 可构造时输出 plan，否则 nullopt。
```

`reference_plan = nullopt` 不反向影响 Reducer 的状态推进。Geometry 缺失不能重置 phase，不能回退 phase，不能触发 `EnterIdle()`。

## 6. 从摄像头到 Circle Candidate 的数据流

V2 的完整数据流到 circle candidate 为止：

```text
Camera Hardware
-> ICameraFrameSource
-> CameraFrameStore
-> PerceptionFrontend
   -> snapshot motion source
-> SteeringFramePerceptionPipeline
   -> ComputeOtsuThreshold()
   -> RunBEVSimplePerception()
      -> sparse_rows
      -> ordinary reference_path
   -> BuildOrdinaryRoadModel()
   -> BuildSceneFrameView()
   -> RunVisualElementPipeline()
      -> cross / non-circle evidence and candidates only
   -> CircleV2Scene::Step()
      -> CircleV2EventObserver
      -> CircleV2Reducer
      -> CircleV2GeometryObserver
      -> CircleV2ReferenceComposer
      -> optional CircleV2ReferencePlan
   -> VisualReferenceAdapter
      -> optional circle VisualReferenceCandidate
```

本数据流在 `circle VisualReferenceCandidate` 结束。`SelectVisualReference()`、reference usability、lateral error、readiness、safety 和 actuator 不属于本文的 circle candidate 生成范围。

### 6.1 Camera / Frame Store

camera 层只负责取帧、转灰度和保存帧事实：

```text
gray frame
frame_id
capture_time_ms
```

camera 层不判断环岛，不保存环岛状态，不生成 reference candidate。

### 6.2 Threshold

单帧感知先计算二值化阈值：

```text
gray frame
-> ComputeOtsuThreshold()
-> threshold
```

`threshold` 只用于后续 BEV 采样分类，不表达 circle 语义。

### 6.3 Sparse BEV Row Scan

BEV simple perception 产生公共视觉观测：

```text
gray frame + threshold + BEVProjector
-> RunBEVSimplePerception()
```

输出：

```text
sparse_rows
ordinary reference_path
ordinary reference_source
```

`ordinary reference_path` 可以继续用于构造普通 `line_candidate`，但 `CircleV2Scene` 不读取 `line_candidate`。它读取由普通路径抽出的 `OrdinaryRoadModel`。

### 6.4 OrdinaryRoadModel

`BuildOrdinaryRoadModel()` 从普通寻线结果和 BEV 几何参数构造 scene 所需道路模型：

```text
ordinary reference_path
BEV_GEOMETRY.NOMINAL_ROAD_HALF_WIDTH_M
-> OrdinaryRoadModel
```

`ordinary_road.center_path` 是当前普通中线。`ordinary_road.half_width` 是进入 scene 前已经完成的道路半宽事实，当前来自稳定配置参数，不随每帧 rows 宽度抖动。若组合层无法构造完整 `OrdinaryRoadModel`，则不得正常调用 `CircleV2Scene::Step()`；active scene 中出现该问题时必须显式 reset scene memory 或进入全局 fail-safe，不能让 geometry 层隐式兜底。

### 6.5 Element Evidence Pipeline

V2 后，element pipeline 的运行时 circle 职责为零。

允许：

```text
cross evidence
cross candidate
other non-circle visual element evidence
```

不允许：

```text
circle evidence record
circle_entry candidate
circle_entry diagnostics
rear / side-rear black frontier
```

如果某些测试或 probe 仍需要旧 circle 可视化，应改到 `CircleV2Telemetry`，而不是继续让 `RunVisualElementPipeline()` 产生 circle evidence。

### 6.6 MotionArcView

motion 输入由组合层适配成 `MotionArcView`：

```text
MotionHistory / yaw integration helper
-> MotionArcView
```

`CircleV2Scene` 不知道 `MotionHistory` 的具体结构。`ObserveCircleV2Events()` 只在需要判断 B -> C 时调用：

```text
motion_arc.YawDeltaRad(phase_enter_capture_time, current_capture_time)
```

生命周期约束：

```text
若 CircleV2Memory.phase == Idle 且组合层没有注册 CircleV2Scene，可以不调用 Step。
若 CircleV2Memory.phase != Idle，组合层必须持续调用 Step。
MotionArcView 必须能覆盖当前阶段 enter_capture_time_ms 到当前 capture_time_ms 的查询范围。
不能通过“本帧不调用 Step”来处理 motion arc 不可用。
```

### 6.7 CircleV2Scene

`CircleV2Scene` 的内部执行顺序：

```text
SceneFrameView + prior memory
-> ObserveCircleV2Events
   -> events
-> CircleV2Reducer
   -> next memory / current-frame reference context
-> ObserveCircleV2Geometry
   -> geometry for current-frame reference context
-> CircleV2ReferenceComposer
   -> optional CircleV2ReferencePlan
```

`CircleV2Scene` 不依赖：

```text
VisualReferenceCandidate
RunVisualElementPipeline circle evidence
MotionHistory concrete type
control tick implementation
reference arbitration
```

## 7. 状态定义

设：

```text
X = 环岛方向，left 或 right
O = X 的对侧
road_half_width = 道路半长
stage_enter_capture_time = 当前阶段进入时的图像时间
turn_angle_since_stage_enter = 从当前阶段进入时刻到当前图像时刻的累计转角
directed_turn_angle = 按环岛方向归一化后的累计转角
exit_yaw_threshold = 出环角度阈值，例如 330 度
exit_hold_frames = C 状态保持帧数
inner_trace_stall_timeout = InnerTrace 无明显转角退回的时间阈值
inner_trace_stall_yaw_min = InnerTrace 在超时窗口内必须达到的最小方向归一化转角
phase_frame_index = Step 开始时该 phase 已完整输出过的帧数；Reducer 内部进入新 phase 当帧为 0
```

yaw 符号约定必须在 motion 适配层与 circle V2 中保持一致。本项目当前
IMU / motion arc 约定左转 yaw 为负、右转 yaw 为正，因此：

```text
CircleTurnSign(left)  = -1
CircleTurnSign(right) = +1
```

若底层 motion arc 的 yaw 符号约定改变，唯一需要同步的是
`CircleV2EventObserver` 内的方向归一化符号；Reducer、GeometryObserver
和 ReferenceComposer 不读取原始 yaw，也不应感知该约定。

### 7.1 Idle

普通寻线状态。

动作：

```text
不生成 circle reference plan。
```

事件：

```text
detected_dir = detect_circle(X)
```

转移：

```text
detected_dir != none
=> dir = X
=> EnterPhase(Approach)
```

### 7.2 A / Approach

已经识别到环岛方向，但尚未接管特殊路径。

动作：

```text
不生成 circle reference plan。
继续让普通寻线作为默认候选。
```

事件：

```text
entry_gate_reached =
  bottom / near 连续 ROI 中，
  locked dir 侧边界沿前向持续外扩，
  且对侧底部边界近似直线
```

`entry_gate_reached` 是相对锁存方向 `dir` 的底部扩张事件：

```text
left circle:
  只消费 left-side bottom expansion。

right circle:
  只消费 right-side bottom expansion。
```

对侧异常扩张不得推动 `Approach -> InnerTrace`。

底部 ROI 是图像下方 / 近端的连续有效行，不是全局有效行的任意前几行。若近端支撑不足，或近端有效行与后续行之间存在明显断层，则不得跳过空洞去消费远端开口。锁存侧开口判定只看同一侧边界随前向的增长，不拿锁存侧 reach 与对侧 reach 做大小比较。

转移：

```text
entry_gate_reached
=> EnterPhase(InnerTrace)
```

约束：

```text
A 只能从 Idle 进入。
```

### 7.3 B / InnerTrace

入环和绕环状态。

动作：

```text
从当前中线向 dir 侧搜索边线。
选择离中线最近的 dir 侧边线。
将其视为内圆边线。
按 `inner_trace_path_offset_m` 生成 reference plan。
```

InnerTrace 不做半路宽偏移。偏移量只服务“离内圆边线多远”：

```text
inner_trace_path_offset_m = 0:
  贴内圆边线。

inner_trace_path_offset_m > 0:
  从内圆边线向道路内部偏移。
  左环岛向右偏。
  右环岛向左偏。

不使用 P_est + fixed_slope。
不通过 boundary override 修改普通边线。
不调用普通 path builder 做 patched-row 生成。
```

事件：

```text
exit_gate_reached =
  directed_turn_progress >= exit_yaw_threshold

inner_trace_stalled =
  !exit_gate_reached
  && InnerTrace elapsed >= inner_trace_stall_timeout
  && directed_turn_progress < inner_trace_stall_yaw_min
```

其中：

```text
directed_turn_angle =
  CircleTurnSign(dir) *
  motion_arc.YawDeltaRad(InnerTrace.enter_capture_time_ms, current_capture_time_ms)

directed_turn_progress =
  max(memory.clock.max_directed_turn_angle_rad, directed_turn_angle)
```

不使用 `abs(yaw_delta)`，因为反向回摆和震荡不应累计为绕环角度。

转移：

```text
exit_gate_reached
=> EnterPhase(ExitTrace)

inner_trace_stalled
=> EnterIdle()
```

`inner_trace_stalled` 是显式安全退回，不是几何不可用兜底。它只由时间和方向归一化转角进度决定；缺少内圆边线只会让本帧 `reference_plan = nullopt`，不能触发 `EnterIdle()`。

### 7.4 C / ExitTrace

离开环岛状态，同时承担防止再次进入同一环岛的职责。

动作：

```text
寻找 dir 对侧 opposite(dir) 的直线边线。
将其视为外侧边线。
沿外侧边线向 dir 侧平移 road_half_width，生成 reference plan。
忽略新的环岛触发。
```

左右环岛的偏移方向：

```text
左环岛：寻找右侧直线边线，路径向左平移 road_half_width。
右环岛：寻找左侧直线边线，路径向右平移 road_half_width。
```

阶段生命周期 gate：

```text
release_gate_reached = phase_frame_index + 1 >= exit_hold_frames
```

含义：

```text
当前这一帧输出 ExitTrace 后，C 已经保持满 exit_hold_frames 帧。
```

转移：

```text
release_gate_reached
=> EnterIdle()
```

约束：

```text
C 内不允许进入 A。
C 的保持时间同时承担 cooldown 职责。
```

## 8. 内部四段式

`CircleV2Scene` 内部拆成事件观察、状态规约、几何观察和 reference 组合四段，但这些模块不是 pipeline 级接口：

```text
CircleV2EventObserver
CircleV2Reducer
CircleV2GeometryObserver
CircleV2ReferenceComposer
```

### 8.1 CircleV2EventObserver

EventObserver 从 `SceneFrameView + prior memory + params` 中提取状态转移事件。

职责：

```text
用原 Phase1 判定语义推导 detected_dir。
判断锁存 `dir` 侧底部连续几行是否出现较大扩张，并翻译成 entry_gate_reached。
通过 MotionArcView 构造阶段 yaw delta。
按 dir 对 yaw delta 做方向归一化，并翻译成 exit_gate_reached。
在 InnerTrace 超过配置时间且方向归一化转角仍小于配置阈值时，翻译成 inner_trace_stalled。
```

EventObserver 必须按 `prior.phase` 产生事件：

```text
prior.phase == Idle:
  只允许产生 detected_dir。

prior.phase == Approach:
  只允许产生 entry_gate_reached。
  detected_dir 即使视觉上存在，也不得改变已锁存 dir。

prior.phase == InnerTrace:
  只允许产生 exit_gate_reached 或 inner_trace_stalled。

prior.phase == ExitTrace:
  不产生 detected_dir、entry_gate_reached 或 exit_gate_reached。
```

Reducer 之外的 telemetry 不应展示被当前 phase 禁用的事件。

EventObserver 不搜索内圆边线，不搜索外侧边线，不构造 reference path。

#### 8.1.1 边线 trace 语义

CircleV2EventObserver 内部的 side expansion observation 可以同时服务：

```text
Phase1 circle cue
Approach entry gate
entry point telemetry
```

但这些语义不能复用同一个布尔判定：

```text
Phase1 circle cue:
  使用完整连续 trace 的侧向开口 + 对侧直线约束。

Approach entry gate:
  只使用图像下方 / 近端连续行。
  必须同时满足锁存 dir 侧底部开口和对侧底部边线近似直线。
  底部行必须从近端开始连续取得，不能跨过缺失近端支撑去使用远端行。
  锁存侧开口 = 同侧边界 reach 沿前向持续增长，不是 locked-side reach > opposite-side reach。
  远端 Phase1 开口仍然可见时，不得直接触发 Approach -> InnerTrace。
```

它们只能消费同一份连续边线 trace：

```text
road-connected interval =
  与 ordinary_road.center_path 在该 row 上连接或最近的白区。

left boundary trace  = road-connected interval 的左边界
right boundary trace = road-connected interval 的右边界
```

边线属于左侧或右侧，由 row 内白区边界的物理顺序决定，不由当前 lateral 坐标符号决定。弯道中，真实左边界可以短暂出现在 `x >= 0`，真实右边界也可以短暂出现在 `x <= 0`；这些点仍然属于同一条物理边线，不得仅因符号变化被删除。

若同一 row 内存在与普通道路不连接的远端白块，Phase1 cue、Approach entry gate、对侧直线和 P 点估计都不得把它合并进 road-connected boundary trace。否则普通弯道上的远端离散白块会把 rightmost / leftmost 边界错误拉远，伪造成一侧开口。

禁止的内部实现：

```text
left trace  = rows where left_m < 0
right trace = rows where right_m > 0
right trace = max(right_m of all intervals in row)
left trace  = min(left_m of all intervals in row)
```

原因是这些做法会把普通弯道的中段删掉，或把不属于道路连接区域的远端白块合并进边界，使“完整弯曲边线”退化成“两段断开的近似直线”或“虚假的侧向开口”，从而错误通过 gate。`open`、`opposite straight` 和 `P` 点估计必须消费同一条 road-connected 连续 trace，不能各自维护一套筛选规则。

`Approach entry gate` 是对锁存方向的近端事件，不是 Phase1 的 full-trace open：

```text
left circle:
  只允许 left bottom rows expansion + right bottom rows straight
  推动 Approach -> InnerTrace。

right circle:
  只允许 right bottom rows expansion + left bottom rows straight
  推动 Approach -> InnerTrace。
```

若某一帧只满足“远端左/右开口 + 全局对侧直线”的 Phase1 cue，而底部连续行仍是普通道路宽度，则 Approach 必须保持，不得进入 InnerTrace。若底部锁存侧已开口，但底部对侧边线不是近似直线，也必须保持 `Approach`。

若近端可用行数量不足、近端行之间出现明显断层、或实现必须跳过近端空洞才能凑够支撑行，则 `entry_gate_reached` 必须保持 false。这样 `Approach -> InnerTrace` 只表达“入口已经到达画面下方”，不表达“远处还看见一个环岛开口”。

迁移后的 Phase1 helper 不再命名为 `DetectCircleElementEvidence`。推荐命名：

```text
ObserveCirclePhase1Cue
```

它表达：

```text
它不是 element evidence。
它是 CircleV2EventObserver 的 Phase1 cue。
```

### 8.2 CircleV2Reducer

Reducer 是纯状态机。

它只读取：

```text
prior CircleV2Memory
CircleV2Events
CaptureStamp
CircleV2Params
```

它不读取：

```text
rows
MotionHistory
MotionArcView
gyro_z
bottom_expansion
edge trace
VisualReferenceCandidate
```

Reducer 只决定：

```text
next memory
current-frame reference context
```

`current-frame reference context` 是内部类型，至少包含：

```text
role
dir
```

`current-frame reference context` 表示本帧要输出的 reference role 和 dir，不一定等于 `next_memory.phase`。例如 `ExitTrace` 的最后一帧应输出 `ExitTrace` reference plan，但本帧结束后的 `next_memory.phase` 可以已经是 `Idle`。

### 8.3 CircleV2GeometryObserver

GeometryObserver 从 `SceneFrameView + current-frame reference context + params` 中提取本帧路径几何。

职责：

```text
如果 role = InnerTrace：
  从当前中线向 dir 侧搜索最近边线，作为内圆边线。
  该几何直接服务 InnerTrace reference plan，不做 road_half_width 偏移。

如果 role = ExitTrace：
  在 dir 对侧寻找直线边线，作为外侧边线。
  读取 ordinary_road.half_width 作为本帧 road_half_width。
```

GeometryObserver 可以在内部保存或返回：

```text
CircleV2Geometry
CircleV2EdgeTrace
```

这些类型不得成为跨模块公共协议。

### 8.4 CircleV2ReferenceComposer

ReferenceComposer 只根据：

```text
current-frame reference context
CircleV2Geometry
CircleV2Params
```

生成：

```text
optional CircleV2ReferencePlan
```

它不决定状态转移。

## 9. CircleV2Scene 最小伪代码

```cpp
CircleV2StepResult CircleV2Scene::Step(const SceneFrameView& frame,
                                       const CircleV2Memory& prior,
                                       const CircleV2Params& params) {
  const CircleV2Events events =
      detail::ObserveCircleV2Events(frame, prior, params);

  const CircleV2Decision decision =
      detail::ReduceCircleV2(prior, events, frame.stamp, params);

  const CircleV2Geometry geometry =
      detail::ObserveCircleV2Geometry(frame,
                                      decision.reference,
                                      params);

  CircleV2StepResult result{};
  result.next_memory = decision.next_memory;
  result.telemetry =
      detail::BuildCircleV2Telemetry(decision, events, geometry);
  result.reference_plan =
      detail::ComposeCircleV2Reference(decision.reference,
                                       geometry,
                                       params);
  return result;
}
```

Reducer 的内部伪代码：

```cpp
CircleV2Decision ReduceCircleV2(const CircleV2Memory& prior,
                                const CircleV2Events& events,
                                CaptureStamp stamp,
                                const CircleV2Params& params) {
  CircleV2Memory current = prior;
  if (prior.phase == CirclePhase::kInnerTrace && events.motion_arc_available) {
    current.clock.max_directed_turn_angle_rad =
        max(current.clock.max_directed_turn_angle_rad,
            events.directed_turn_angle_rad);
  }

  switch (prior.phase) {
  case CirclePhase::kIdle:
    if (events.detected_dir != CircleDir::kNone) {
      current.dir = events.detected_dir;
      EnterPhase(current, CirclePhase::kApproach, stamp);
    }
    break;

  case CirclePhase::kApproach:
    if (events.entry_gate_reached) {
      EnterPhase(current, CirclePhase::kInnerTrace, stamp);
    }
    break;

  case CirclePhase::kInnerTrace:
    if (events.exit_gate_reached) {
      EnterPhase(current, CirclePhase::kExitTrace, stamp);
    } else if (events.inner_trace_stalled) {
      EnterIdle(current);
    }
    break;

  case CirclePhase::kExitTrace:
    break;
  }

  CircleV2ReferenceContext reference{};
  reference.dir = current.dir;
  if (current.phase == CirclePhase::kInnerTrace) {
    reference.role = CircleV2ReferenceRole::kInnerTrace;
  } else if (current.phase == CirclePhase::kExitTrace) {
    reference.role = CircleV2ReferenceRole::kExitTrace;
  }

  CircleV2Memory next = current;
  if (current.phase == CirclePhase::kExitTrace &&
      current.clock.phase_frame_index + 1 >= params.exit_hold_frames) {
    EnterIdle(next);
  } else if (current.phase != CirclePhase::kIdle) {
    next.clock.phase_frame_index = current.clock.phase_frame_index + 1;
  }

  return {.next_memory = next, .reference = reference};
}
```

`decision.reference` 表示本帧要输出的 reference context，不一定等于 `decision.next_memory.phase`。例如 `ExitTrace` 的最后一帧应输出 `ExitTrace` reference plan，但本帧结束后的 `next_memory.phase` 可以已经是 `Idle`。

Telemetry 必须同时暴露本帧 phase 和下一帧 phase：

```text
telemetry.frame_phase = decision.reference 对应的可见 phase；
telemetry.next_phase = decision.next_memory.phase。
```

阶段帧计数固定为：

```text
prior memory 中的 phase_frame_index 表示本帧开始时该 phase 已经完整输出过的帧数。
Reducer 内部 EnterPhase 后，current.phase_frame_index = 0，表示本帧是新 phase 的第 0 帧。
本帧使用 current.phase_frame_index。
如果本帧输出该 phase reference 且本帧结束后仍保持该 phase，next_memory.phase_frame_index = current.phase_frame_index + 1。
如果本帧切换阶段，新阶段 current.phase_frame_index = 0。
next_memory 保存的是下一帧开始时的 phase_frame_index。
ExitTrace 在 phase_frame_index + 1 >= exit_hold_frames 的当前帧结束后进入 Idle。
```

因此 `exit_hold_frames = 3` 的含义是：

```text
ExitTrace reference plan 正好输出 3 帧。
第 3 帧仍然输出 ExitTrace。
第 3 帧结束后 next phase = Idle。
```

## 10. 状态变量与参数

V2 最小状态机只锁存：

```text
phase
dir
clock.enter_capture_time_ms
clock.phase_frame_index
```

不得加入以下跨模块状态：

```text
left_open
right_open
bottom_expansion
inner_edge
outer_edge
road_half_width
yaw_acc_rad
gyro_delta_rad
```

`road_half_width` 属于 reference plan 构建所需的稳定道路几何量，由 `OrdinaryRoadModel` 提供，不作为 FSM 对外状态。当前组合层使用 `BEV_GEOMETRY.NOMINAL_ROAD_HALF_WIDTH_M` 构造 `ordinary_road.half_width`；`ObserveCircleV2Geometry()` 只在 `ExitTrace` 外侧边线偏移时消费它，不在 scene 内猜测道路半宽，也不从 rows 实时重算。

允许参数：

```text
CIRCLE_V2_ENABLED
CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG
CIRCLE_V2_EXIT_HOLD_FRAMES
CIRCLE_V2_INNER_TRACE_STALL_TIMEOUT_MS
CIRCLE_V2_INNER_TRACE_STALL_YAW_MIN_DEG
CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M
```

道路半宽不是 `CIRCLE_V2_*` 参数。当前活跃实现使用：

```text
BEV_GEOMETRY.NOMINAL_ROAD_HALF_WIDTH_M
```

组合层将它写入 `OrdinaryRoadModel.half_width`，CircleV2 只读取公共事实面。

`CIRCLE_V2_ENABLED` 属于组合层开关，不进入 `CircleV2Params`。该开关不支持 active runtime 无声热切换。

`CircleV2Params` 只接收：

```text
exit_yaw_threshold_rad
exit_hold_frames
inner_trace_stall_timeout_ms
inner_trace_stall_yaw_min_rad
inner_trace_path_offset_m
```

其中：

```text
exit_hold_frames >= 2
```

## 11. 状态转移全集

V2 只允许以下转移：

```text
Idle -> A
A    -> B
B    -> C
C    -> Idle
```

不存在：

```text
A -> Idle
B -> Idle
C -> A
Idle -> B
Idle -> C
独立 Cooldown 状态
```

## 12. 当前代码迁移边界

本节把 V2 定义映射到当前代码，明确哪些代码迁移、清理和更新。

### 12.1 当前活跃链路

当前活跃链路位于：

```text
new/code/runtime/steering_frame_perception_pipeline.cpp
new/code/legacy/steering_visual_element_pipeline.cpp
new/code/legacy/steering_circle_element_evidence.cpp
new/code/port/steering_state_types.hpp
new/code/port/visual_element_evidence_types.hpp
new/code/platform/param_store.cpp
new/code/platform/steering_media_protocol.cpp
new/config/default_params.json
new/config/default_params.md
new/user/CMakeLists.txt
```

当前 `SteeringFramePerceptionPipeline::ProcessFrame()` 已经具备 V2 所需的主要视觉输入：

```text
sparse_rows
ordinary reference_path
```

需要新增或调整：

```text
SceneFrameView
OrdinaryRoadModel
MotionArcView adapter
CircleV2Memory
CircleV2Scene::Step()
CircleV2EventObserver / Reducer / GeometryObserver / ReferenceComposer
CircleV2ReferencePlan
VisualReferenceAdapter
CIRCLE_V2_* params
```

### 12.2 保留与迁移

保留原始 Phase1 环岛判定语义，不保留旧 circle evidence 所属模块。

应迁移到 `CircleV2EventObserver` 私有 helper 的逻辑：

```text
CollectRows
BuildRowObservation
WidestInterval
BuildBoundaryTrace
SustainedGrowthEvidence
FitBoundaryLine
AssessSides
旧 DetectCircleElementEvidence 中的 Phase1 cue 语义
```

迁移后 helper 命名为：

```text
ObserveCirclePhase1Cue
```

迁移必须有 golden parity 约束：

```text
同一批 rows 输入下：
old Phase1 cue result == new ObserveCirclePhase1Cue result
```

迁移后的 helper 只服务：

```text
Idle -> A 的原始环岛方向判定
CircleV2Telemetry
```

不服务：

```text
RunVisualElementPipeline circle evidence
circle_entry candidate
旧 Phase2 后黑入口寻线
```

### 12.3 清理与下线

旧 Phase2 后黑链路应从运行时代码中下线：

```text
CircleEntryPathFacts
CircleEntryPipelineDiagnostics
BEVMetricClassSampler
HasRearSideBlack
FindRearFrontierPoint
RasterHasRearSideBlack
RasterFindRearFrontierPoint
BuildEntryFacts
BuildEntryFactsFromRaster
BuildCircleEntryPathFacts
BuildCircleEntryVisualReferenceCandidate
```

这些名称对应的语义是：

```text
rear / side-rear black frontier
entry takeover
frontier chain
circle_entry reference candidate
```

V2 中不再使用这些语义决定内圆、出环或 reference plan。不要为了旧兼容性保留废弃 telemetry；需要观察 V2 时使用 `CircleV2Telemetry`。

`RunVisualElementPipeline()` 中应清理：

```text
AppendCircleEvidence
MaybeBuildCircleCandidate
MakeEffectiveCircleRecord 的 circle 分支
DetectCircleElementEvidence 调用
circle_entry_diagnostics 输出
```

清理后它只产生 non-circle visual element evidence / candidates。

### 12.4 参数更新

旧参数不应复用为 V2 参数：

```text
CIRCLE_ENTRY_TAKEOVER_ENABLED
CIRCLE_ENTRY_MIN_FRONTIER_POINTS
CIRCLE_ENTRY_DIRECTION_MIN_LATERAL_M
CIRCLE_ENTRY_MAX_INTERPOLATION_GAP_M
CIRCLE_ENTRY_MAX_JOIN_JUMP_M
```

这些旧键应从默认配置、参数解析、media protocol 参数快照和测试期望中清理；不得把旧 `CIRCLE_ENTRY_*` 静默映射为 V2 语义。

V2 使用新的语义参数：

```text
CIRCLE_V2_ENABLED
CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG
CIRCLE_V2_EXIT_HOLD_FRAMES
CIRCLE_V2_INNER_TRACE_STALL_TIMEOUT_MS
CIRCLE_V2_INNER_TRACE_STALL_YAW_MIN_DEG
CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M
```

道路半宽使用 `BEV_GEOMETRY.NOMINAL_ROAD_HALF_WIDTH_M`，不作为 CircleV2 专属参数，也不从 rows 实时重算。

其中：

```text
CIRCLE_V2_ENABLED:
  scene registry / 启动期组合开关。

CIRCLE_V2_EXIT_YAW_THRESHOLD_DEG:
  B -> C 的阶段角度阈值，默认可按 330 度配置。

CIRCLE_V2_EXIT_HOLD_FRAMES:
  C 状态保持帧数，同时承担防止重复进入同一环岛的职责，合法范围 >= 2。

CIRCLE_V2_INNER_TRACE_STALL_TIMEOUT_MS:
  InnerTrace 无明显 yaw 积分时允许退回 Idle 的超时时间。

CIRCLE_V2_INNER_TRACE_STALL_YAW_MIN_DEG:
  判断“已有明显 yaw 积分”的方向归一化角度阈值。

CIRCLE_V2_INNER_TRACE_PATH_OFFSET_M:
  InnerTrace 路径从内圆边线向道路内部偏移的距离；0 表示贴内圆边线。
```

首选策略：

```text
CIRCLE_V2_ENABLED 只在启动期生效。
```

若现有参数系统无法避免运行时变更，则该变更必须被组合层转换为 scene lifecycle reset，不属于 `CircleV2Reducer` 的正常状态转移：

```text
CIRCLE_V2_ENABLED true -> false:
  组合层必须同步 ResetCircleV2Memory。

CIRCLE_V2_ENABLED false -> true:
  必须从 Idle memory 开始。
```

不得让 scene 停止 Step 但 memory 保留在 `Approach`、`InnerTrace` 或 `ExitTrace`。

### 12.5 状态更新

`SteeringPerceptionMemory` 需要新增：

```text
CircleV2Memory circle_v2
```

`CircleV2Memory` 字段为：

```text
phase
dir
clock.enter_capture_time_ms
clock.phase_frame_index
```

状态更新只能发生在 `CircleV2Reducer`。

### 12.6 Yaw 输入

当前 control loop 已维护运动历史。V2 不让 `CircleV2Scene` 直接读取具体 `MotionHistory` 类型，而是在组合层提供 `MotionArcView`：

```text
control tick records MotionHistory
PerceptionFrontend snapshots motion source
SteeringFramePerceptionPipeline builds MotionArcView
CircleV2EventObserver queries yaw delta through MotionArcView
CircleV2Reducer reads exit_gate_reached event for B -> C
```

`MotionArcView` 可以调用底层 yaw integration helper，把：

```text
from_capture_time_ms
to_capture_time_ms
```

转换为：

```text
yaw_delta_rad
```

`CircleV2EventObserver` 使用 `CircleTurnSign(dir)` 把 `yaw_delta_rad` 归一化为 `directed_turn_angle`，再判断 `exit_gate_reached`。FSM 不读取 `MotionHistory`、`gyro_z`、`gyro_delta` 或 IMU valid。FSM 甚至不直接读取角度，只读取 `exit_gate_reached` event。

active lifecycle 约束：

```text
CircleV2Memory.phase != Idle 时，组合层必须持续调用 Step。
MotionArcView 不可用不能通过跳过 Step 处理。
```

### 12.7 新文件与接入点

建议新增：

```text
new/code/runtime/steering_scene_frame_view.hpp
new/code/runtime/steering_circle_v2_scene.hpp
new/code/runtime/steering_circle_v2_scene.cpp
new/code/runtime/steering_circle_v2_reference_adapter.hpp
new/code/runtime/steering_circle_v2_reference_adapter.cpp

new/code/runtime/detail/steering_circle_v2_internal.hpp
new/code/runtime/detail/steering_circle_v2_event_observer.cpp
new/code/runtime/detail/steering_circle_v2_geometry_observer.cpp
new/code/runtime/detail/steering_circle_v2_reducer.cpp
new/code/runtime/detail/steering_circle_v2_composer.cpp
```

并加入当前 active build 的 source list。

公开入口只允许是：

```text
runtime/steering_scene_frame_view.hpp
runtime/steering_circle_v2_scene.hpp
runtime/steering_circle_v2_reference_adapter.hpp
```

`SteeringFramePerceptionPipeline` 不应 include：

```text
runtime/detail/steering_circle_v2_internal.hpp
```

`detail` 头只服务 circle V2 内部 `.cpp` 和必要的内部单测。这样文件可以拆开，但内部 events、geometry、edge trace、reference context 不泄漏成系统协议。

接入顺序：

```text
RunBEVSimplePerception()
BuildOrdinaryRoadModel()
BuildSceneFrameView()
RunVisualElementPipeline()   // non-circle only
CircleV2Scene::Step()
AdaptCircleV2ReferencePlan()
SelectVisualReference()
```

adapter 产出的 candidate 使用现有 visual reference arbitration：

```text
kind = kCircleLeft / kCircleRight
source = circle_v2_inner / circle_v2_exit
mode = kIntervalCenter
```

adapter 不从 `CircleV2ReferencePlan` 读取 confidence，也不重新解释内部证据分数；只根据 `role` 和 `dir` 固定包装为 special candidate。

### 12.8 测试更新

需要更新的测试面：

```text
visual_element_evidence_test:
  删除 circle evidence / circle_entry candidate 期望。
  删除或改写 BuildCircleEntryPathFacts / frontier 测试。
  验证 RunVisualElementPipeline 不再产生 circle evidence / circle candidate / circle diagnostics。

steering_circle_v2_event_observer_test:
  新增 ObserveCirclePhase1Cue / CircleV2EventObserver 测试。
  新增 ObserveCirclePhase1Cue golden parity 测试，确保迁移前后 left / right / none 完全一致。
  新增普通右弯反例测试：右侧有扩张，但完整左边界为弯曲 trace 时，不得输出 right circle cue。
  新增边线 trace 测试，确认 left/right trace 不按 lateral 符号过滤。
  新增 disconnected far-side artifact 测试，确认远端离散白块不会被合并进 road-connected boundary trace，也不会制造 right / left circle cue。
  新增 Approach entry gate 测试，确认 entry gate 使用底部近端连续 ROI、锁存侧同侧增长和同 ROI 对侧直线，不使用全局远端开口或 locked/opposite reach 大小比较。
  新增近端支撑缺失 / 底部行断层测试，确认 Approach 不跨空洞消费远端开口。
  新增 per-prior-phase event gating 测试。
  新增 directed yaw 测试，覆盖本项目 yaw 约定下的左环岛负向、右环岛正向和反向回摆不累计。

steering_circle_v2_geometry_observer_test:
  新增 CircleV2GeometryObserver role-specific 几何测试。
  覆盖 InnerTrace 直接输出锁存方向侧内圆边线，不再使用 P_est、固定斜率或 boundary override。

steering_circle_v2_reducer_test:
  新增 CircleV2Reducer 纯状态序列测试。
  覆盖 Idle dir=None、非 Idle dir=left/right 的状态不变量。
  覆盖 exit_hold_frames 非法值回退或拒绝，确保合法值 >= 2。
  覆盖 ExitTrace 最后一帧输出 plan、下一帧 memory 进入 Idle 的 off-by-one 语义。
  覆盖 reference_plan = nullopt 不改变 Reducer 状态推进。

steering_circle_v2_scene_test:
  新增 CircleV2Scene facade 测试。
  覆盖 InnerTrace 生成 `circle_v2_inner` reference plan，ExitTrace 保持外侧边线半宽偏移。

steering_circle_v2_reference_adapter_test:
  验证 CircleV2ReferencePlan 到 VisualReferenceCandidate 的固定映射。

runtime_parameter_defaults_test:
  从 CIRCLE_ENTRY_* 默认值切换到 CIRCLE_V2_* 默认值。

param_store_load_runtime_parameters_test:
  验证 CIRCLE_V2_* 解析和非法值回退，尤其 CIRCLE_V2_EXIT_HOLD_FRAMES >= 2。

scene_runtime_lifecycle_test:
  验证 CIRCLE_V2_ENABLED 热切换时 reset memory，或验证该开关只在启动期生效。
  验证 active phase 中不会因为 MotionArcView 缺失而静默跳过 Step。

run_scene_overlay_probe_authority_baseline_test:
  不再期待旧 circle_entry.left.present=true。
  如需观测 V2，应使用 CircleV2Telemetry 和 circle_v2_* source。

run_host_capture_selftest:
  不再把 reference mode 固定为 circle_entry。
```

### 12.9 Archive 代码边界

`new/code/archive/bev_topology_pre_simple_rewrite` 中的旧 scene FSM 和 reference policy 不应整体迁移。它们属于旧拓扑系统，与当前 sparse BEV simple perception 链路不一致。

只允许借鉴概念：

```text
方向锁存
基于进入时刻的角度阈值判定
exit / release hold counter
debug source 命名
```

不迁移：

```text
SpecialSceneKind
ReferencePolicyState
trusted reference
topology scene FSM
旧 circle_entry signal
跨场景 reference blending / latch machinery
```

## 13. 推荐落地顺序

V2 的实现顺序应保持可验证：

```text
1. 新增 CIRCLE_V2_* 参数和 CircleV2Memory，不改变行为。
2. 新增强契约 SceneFrameView、完整 OrdinaryRoadModel、MotionArcView 边界。
3. 新增 CircleV2Scene facade 和 detail 四子模块骨架。
4. 把原 Phase1 circle cue 语义迁移到 ObserveCirclePhase1Cue 私有 helper，并做 golden parity。
5. 新增 CircleV2Reducer 纯单元测试，覆盖 Idle/Approach/InnerTrace/ExitTrace/Idle、EnterIdle 不变量和 C hold off-by-one。
6. 接入 MotionArcView，并在 CircleV2EventObserver 内构造 directed_turn_angle / exit_gate_reached。
7. 接入 active scene lifecycle 约束，确保非 Idle 阶段持续 Step。
8. 接入 role-specific CircleV2GeometryObserver。
9. 接入 B/C CircleV2ReferencePlan 和 VisualReferenceAdapter。
10. 接入 SelectVisualReference。
11. 从 RunVisualElementPipeline 下线 circle evidence / circle_entry 运行时逻辑。
12. 删除旧 CIRCLE_ENTRY_* 参数、测试期望和 media/probe 旧字段。
```

完成后，运行时 circle reference 的唯一来源应为：

```text
CircleV2Scene::Step()
```
