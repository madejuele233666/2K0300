# Visual Element Sparse Circle V1 架构

本文档定义 cross/circle 视觉元素下一版 runtime 架构。它细化并取代根目录 `README.md` 中 circle 仍以 full `BEVElementRasterFrame` 作为首选输入的旧实施表述；长期原则仍以根目录 `README.md` 的“大道至简”“互不知晓”“事实优先”为准。

## 1. 目标

V1 的目标是把 circle/cross 识别从 full element raster 热路径迁到 sparse BEV row facts，并只在 circle 入口寻线需要二维邻接事实时做局部二次扫描。

必须达成：

- Phase1 识别只消费当前帧 sparse BEV row facts。
- cross detector 不读取 circle 结果，circle detector 不读取 cross 结果。
- cross 对 circle 的压制仍只发生在 `steering_visual_element_pipeline.*` 聚合层。
- Phase2 只在 effective `circle_left/right` 成立且 circle takeover 显式开启时运行。
- Phase2 使用 ROI 二次扫描确认内圆后方黑白边线，不再依赖每帧 full element raster。
- ordinary line reference、hold、safety、yaw、actuator 的职责边界不变；新增 reference time alignment 只做时间坐标对齐，不参与视觉识别或控制决策。

不做：

- 不实现完整环岛状态机、绕行记忆、出口识别或基于 yaw/encoder 的环岛策略。
- 不用人为 lateral offset 强行转向。
- 不用“旧感知降权”掩盖延迟；旧 reference 必须先按 ego-motion 对齐到当前车身坐标。
- 不把 debug overlay、media、assistant telemetry 反向作为 runtime 输入。
- 不为了 circle/cross 重建旧 topology / trusted path / scene FSM / opening score 体系。

## 2. 总体流水线

V1 runtime single-frame perception 流水线：

```text
camera frame
-> Otsu threshold
-> sparse BEV row scan
   ├─ line reference builder
   │  -> line VisualReferenceCandidate
   ├─ cross Phase1 detector
   │  -> cross_exit evidence
   └─ circle Phase1 detector
      -> circle_left_raw / circle_right_raw evidence

cross + raw circle evidence
-> visual element pipeline aggregation
   ├─ keep raw circle records
   └─ produce effective circle_left / circle_right
      (cross present suppresses effective circle only)

effective circle + takeover enabled
-> circle Phase2 ROI sampler
   -> rear-side black-white frontier facts
   -> circle entry VisualReferenceCandidate

line candidate + enabled element candidates
-> visual reference orchestration
-> reference continuity / hold
-> reference time alignment
   (reference path at capture time -> reference path at control time)
-> reference usability
-> reference lateral error
-> reference-control readiness
-> safety gate
-> yaw / actuator
```

关键变化：

- `BEVSimpleRowScan` 是 line、cross、circle Phase1 的共同事实面。
- `BEVElementRasterFrame` 不再是 runtime circle/cross 识别热路径的必要输入。
- 二维邻接事实由 Phase2 ROI sampler 按需提供，而不是全帧 raster 预先提供。

## 3. 事实面

### 3.1 Sparse Row Facts

`steering_bev_simple_perception.*` 产出的 `BEVSimpleRowScan` 是 V1 的基础视觉事实：

- `valid`
- `forward_m`
- `sampleable_count`
- `white_count`
- `black_count`
- `unknown_count`
- `unavailable_count`
- `sampleable_left_m`
- `sampleable_right_m`
- `sampleable_width_m`
- `intervals[]`

其中 `intervals[]` 的每个 `BEVSimpleWhiteInterval` 表示当前 BEV 前向采样行上的白色连通区间：

- `left_m`
- `right_m`
- `center_m`
- `width_m`
- `left_px`
- `right_px`

V1 detector 只能从这些字段推导 Phase1 视觉事实。`OutsideFrame`、`ProjectionFailed`、不可采样、unknown、图像边界和 FOV 边界都不能冒充 opening、frontier、edge 或 path。

### 3.2 ROI Metric Sampler

Phase2 需要二维邻接事实时，新增局部 sampler 概念：

```text
BEVMetricClassSampler
input:
  LegacyCameraFrameView
  threshold
  RuntimeParameters.BEV_CLASSIFICATION
  BEVProjector

Sample(BEVPoint):
  -> projection state
  -> BEVElementRasterCellClass-like class
```

它只回答一个 metric point 在当前帧中是否 sampleable，以及分类为 white / black / unknown / invalid。它不构建 full raster，不保存整帧 class 面，不生成 reference，不读取 cross/circle/pipeline/safety/control 状态。

### 3.3 Motion History Facts

Reference time alignment 使用控制侧已经采样到的运动事实，而不是视觉或 actuator 意图：

```text
MotionHistorySample:
  time_ms
  imu_valid
  gyro_z
  encoder_valid
  left_encoder_delta
  right_encoder_delta
```

motion history 由 control tick 记录到固定容量 ring buffer。它只记录传感器事实，不读取 reference、cross、circle、safety gate、yaw target 或 motor command。

第一版积分只需要两个输出：

```text
delta_s_m   = capture_time_ms -> control_time_ms 的前进距离
delta_yaw_rad = capture_time_ms -> control_time_ms 的车身 yaw 变化
```

距离来自 encoder 里程积分，yaw 来自 `gyro_z` 时间积分。若某一类传感器事实缺失，可以 fail closed，或在明确配置下只启用 yaw-only 对齐；不得用 yaw controller target、applied PWM 或视觉曲率反推车辆运动。

## 4. Phase1 识别

### 4.1 Cross

cross 继续从 sparse rows 识别。语义保持：

```text
cross = 两侧开口 + 宽白行足够严格
```

宽白行必须满足已有宽度、双侧 reach、unknown ratio、连续行数等 fail-closed 条件，并满足：

```text
white_count / sampleable_count >= BEV_ELEMENT.CROSS_WIDE_ROW_WHITE_RATIO_MIN
```

默认阈值仍为 `0.95`，后续可通过离线 authority-baseline 评估是否提高到 `0.98`。

### 4.2 Circle

circle Phase1 改为从 sparse rows 识别，不再从 `BEVElementRasterFrame` 收集行观测。

每一行选择用于元素判断的白色区间。V1 默认使用最宽白色区间作为 row boundary observation，因为 cross/circle 的 Phase1 语义关注“道路白区整体左右边界”，不是普通 line reference 追踪的单个中心白点。

对每个 row observation 计算：

```text
left_reach  = max(0, -interval.left_m)
right_reach = max(0,  interval.right_m)
white_ratio = white_count / sampleable_count
```

开口判断使用窗口净变化，不要求逐行单调：

```text
left_open:
  later sustained left_reach >= anchor left_reach * (1 + CIRCLE_OPENING_EXPANSION_RATIO_MIN)
  and delta >= CIRCLE_OPEN_EXPANSION_MIN_M

right_open:
  later sustained right_reach >= anchor right_reach * (1 + CIRCLE_OPENING_EXPANSION_RATIO_MIN)
  and delta >= CIRCLE_OPEN_EXPANSION_MIN_M
```

这允许 `0.10, 0.30, 0.29` 仍判定为开口。

对侧稳定/内缩判断使用同一组 row observation 的简单边界拟合：

```text
opposite_straight:
  fitted residual <= CIRCLE_OPPOSITE_STRAIGHT_DRIFT_MAX_M
  and not opposite_shrink

opposite_shrink:
  fitted reach decreases by at least CIRCLE_OPPOSITE_SHRINK_RATIO_MIN
```

最终语义：

```text
circle_left  = left_open  + right_straight
circle_right = right_open + left_straight
bend         = one side open + opposite side shrink
cross        = both sides open + saturated wide white rows
line/bend    = neither side open, or circle fail-closed reason
```

两侧都开口时，circle detector 只输出 `both_sides_open` / non-present raw circle；是否 cross present 由 cross detector 自己判断，是否压制 effective circle 由 pipeline 判断。

### 4.3 Generic Records

circle 仍固定输出四条 generic records：

- `circle_left_raw`
- `circle_right_raw`
- `circle_left`
- `circle_right`

raw records 表示 detector 原始事实。effective records 由 pipeline 生成，用于 Phase2 和后续 arbitration。

当 `cross_exit.present=true` 时：

- raw circle records 保留。
- effective `circle_left/right.present=false`。
- effective reason 为 `suppressed_by_cross_exit`。
- 不运行 circle Phase2。
- 不 push circle candidate。

## 5. Phase2 入口寻线

Phase2 只在以下条件同时成立时运行：

```text
effective circle_left/right.present == true
BEV_ELEMENT.CIRCLE_ENTRY_TAKEOVER_ENABLED == true
```

takeover disabled 时，runtime 不运行 ROI scan，effective record 的 candidate summary 应表达：

```text
built=false
takeover_enabled=false
included_in_arbitration=false
reason=takeover_disabled
```

离线 probe 如果要观测 Phase2，可用显式参数打开 takeover，或使用独立 probe-only 路径；runtime 默认不为了 debug 扫描 ROI。

### 5.1 Near-Connected White Component

Phase2 从 Phase1 使用的 sparse row observations 中提取 near-connected component：

- 从最近端有效 row 开始。
- 相邻 row 的 white interval 必须在 lateral 方向有重叠或小容差重叠。
- 一旦不连接就停止。
- 只使用当前连续 component，不跨 gap，不跳到远端白块。

近端道路半宽来自 component 前 `CIRCLE_MIN_SUPPORT_ROWS` 个有效 row width 的中位数：

```text
road_half_width_m = median(width_m) / 2
```

不足则 fail closed。

### 5.2 Rear-Side Frontier

Phase2 不寻找“内圆黑块”，而是寻找白区后方的黑白边线。

左环岛：

```text
frontier chain 整体向左上延伸
centerline = frontier + road_half_width
```

右环岛：

```text
frontier chain 整体向右上延伸
centerline = frontier - road_half_width
```

候选 frontier point 必须满足：

- 当前 metric point sampleable 且为 white。
- rear / side-rear metric point sampleable 且为 black。
- rear / side-rear 的间距来自 BEV geometry，而不是 raster/FOV 边界。
- unknown、invalid、outside frame、projection failed 不能成为 black frontier 支撑。

方向判断：

```text
left circle:
  chain.last.forward_m > chain.first.forward_m
  chain.last.lateral_m - chain.first.lateral_m <= -CIRCLE_ENTRY_DIRECTION_MIN_LATERAL_M

right circle:
  chain.last.forward_m > chain.first.forward_m
  chain.last.lateral_m - chain.first.lateral_m >=  CIRCLE_ENTRY_DIRECTION_MIN_LATERAL_M
```

第一版只用首尾净变化，不要求逐点单调，不引入 PCA 或复杂拟合。

### 5.3 Reference Candidate

circle candidate builder 只消费：

- effective circle record
- Phase2 entry facts
- runtime parameters

它不重新判断 Phase1，不读取 cross raw，不读取 safety/IMU/encoder/yaw/actuator。

构造路径规则：

- 近端段使用 near-connected component centerline。
- frontier 可观察后切到 `frontier ± road_half_width` centerline。
- 只在同一 frontier chain 内允许小 gap 线性插值。
- 不跨 unknown / unavailable / outside frame / projection failed。
- 不倒补 index 0。
- 输出必须从 index 0 开始连续、finite，并通过现有 visual reference validation。

## 6. 附加模块：Reference Time Alignment

Reference time alignment 解决的不是 circle/cross 识别问题，而是控制时刻与视觉时刻不一致的问题。视觉 reference 的真实语义是：

```text
reference_path expressed in vehicle frame at capture_time_ms
```

control tick 使用它时，车辆已经发生前进和 yaw 旋转。如果直接把这条 path 当作当前车身坐标下的路，急弯场景会出现旧大误差拖尾。V1 的优雅解法不是调小转向、降低旧感知权重或增加场景 if，而是把 reference path 从 capture-time 车身坐标重投影到 control-time 车身坐标。

### 6.1 边界

新增模块建议命名为：

```text
steering_reference_time_alignment.*
```

它只消费：

- selected 或 held `BEVReferencePath`
- reference source time：`reference_capture_time_ms`
- control tick 当前时间：`control_time_ms`
- motion history facts
- 少量 alignment 参数

它只输出：

- aligned `BEVReferencePath`
- `age_ms`
- `delta_s_m`
- `delta_yaw_rad`
- `valid`
- `reason`

它不消费：

- camera frame / threshold / sparse rows / ROI sampler
- cross / circle raw evidence
- circle Phase2 frontier facts
- visual reference arbitration 内部评分
- safety gate 结果
- yaw target / wheel target / PWM / actuator command

因此模块关系保持：

```text
vision:          生成过去时刻的路
motion history: 记录传感器运动事实
alignment:      只做时间坐标变换
controller:     控制当前时刻的路
```

### 6.2 时间戳语义

必须区分三个时刻：

```text
capture_time_ms: 图像事实对应的时间
publish_time_ms: perception result 可被控制侧读取的时间
control_time_ms: 当前 control tick 时间
```

alignment 的积分区间是：

```text
reference_capture_time_ms -> control_time_ms
```

不是 `publish_time_ms -> control_time_ms`。`publish_time_ms` 只说明结果何时发布，不能代表图像内容是什么时候发生的。

第一版时间戳建议：

```text
capture_wait_start_ms = NowMs() before wait_image_refresh()
capture_wait_end_ms   = NowMs() after wait_image_refresh()
capture_time_ms       = hardware frame timestamp if available
                    else capture_wait_end_ms
```

当前若没有硬件帧时间戳，使用 `capture_wait_end_ms` 是保守且简单的选择。它不假设 frame 在等待开始前已经存在，也避免把相机阻塞等待时间误算成图像已经发生后的运动补偿时间。

如果后续确认供应商 UVC 库暴露 frame sequence 或硬件 timestamp，应把 `capture_time_ms` 改为真实帧时间；`capture_wait_start_ms/end_ms` 仍可作为 debug facts。

### 6.3 Ego-Motion 积分

control tick 每周期记录一个 motion history sample。alignment 在 `[reference_capture_time_ms, control_time_ms]` 区间上积分：

```text
delta_yaw_rad = integral(gyro_z dt)
delta_s_m     = integral(encoder forward distance)
```

第一版不需要完整状态估计器，不预测未来，不使用控制目标。实现上只要能回答“从拍到这条 reference 到现在，车实际转了多少、走了多少”。

若 encoder 距离标定暂时不够可信，可以先以 yaw-only 对齐作为诊断阶段，但这必须显式记录在 `reason` 或 debug facts 中。生产控制路径最终应使用 yaw + distance，因为只旋转不平移会让近端 reference 在高速时仍残留几何误差。

### 6.4 坐标变换

BEV path point 仍使用现有坐标约定：

```text
p_capture = (forward_m, lateral_m)
```

alignment 输出当前车身坐标下的点：

```text
p_now = R(-delta_yaw_rad) * (p_capture - [delta_s_m, 0])
```

其中 `R()` 的符号必须由现有 gyro/BEV 坐标约定的单测锁定。文档只规定语义：车已经左转后，旧的左侧急弯 reference 应该自然回到更接近当前中心的位置，而不是继续保持原始大 lateral error。

变换后：

- `forward_m <= 0` 的点不再作为前方 reference。
- 剩余点保持原采样顺序。
- 不插入凭空观测点。
- 不跨越缺失段补点。
- aligned path 再进入 usability、lateral error、readiness。

### 6.5 Fail-Closed 条件

alignment 只需要几何适用范围，不需要场景补丁：

- motion history 覆盖不到 `reference_capture_time_ms`。
- `[reference_capture_time_ms, control_time_ms]` 中 IMU/encoder 存在不可接受 gap。
- `age_ms > REFERENCE_TIME_ALIGNMENT.MAX_AGE_MS`。
- `abs(delta_yaw_rad) > REFERENCE_TIME_ALIGNMENT.MAX_DELTA_YAW_RAD`。
- 对齐后 leading samples 不足。
- 对齐后坐标非 finite。

这些条件表达“这条过去的路已经无法可靠转换成当前的路”。它们不判断 circle、cross、bend，也不根据转向输出大小做特判。

### 6.6 下游计算位置

V1 的目标语义是：

```text
selected / held reference at capture time
-> reference time alignment
-> reference usability
-> reference lateral error
-> reference-control readiness
-> safety gate
-> yaw / actuator
```

这意味着 lateral error 和 readiness 不应长期作为 perception publish 后的冻结控制量。它们应基于 aligned reference 在 control-time 计算，或在过渡阶段同时保留 perception-time debug 值与 control-time aligned 值，控制侧只使用 aligned 值。

hold 需要携带 reference source time：如果 hold 复用上一条 reference，它的 `reference_capture_time_ms` 也必须来自上一条 reference，而不是当前帧时间。这样 held path 会自然随年龄增长而 fail closed，不需要额外 hold 场景规则。

### 6.7 Debug Facts

建议 debug / media / assistant 只序列化 alignment 结果，不反向影响运行：

```text
reference_time_alignment:
  enabled
  valid
  reason
  age_ms
  reference_capture_time_ms
  control_time_ms
  delta_s_m
  delta_yaw_rad
  input_sample_count
  aligned_sample_count
```

这些字段用于解释急弯延迟和过冲，不参与视觉 detector，也不让 yaw controller 重新解释 reference。

## 7. 附加模块：YUYV Camera Frame Source / Store

Camera capture 解耦解决的是 runtime scheduling 问题，不改变 circle/cross 视觉语义。视觉栈继续只接收稳定的 gray frame：

```text
LegacyCameraFrameView:
  gray
  width / height / stride
  frame_id
  capture_time_ms
```

完整抽象层如下：

```text
camera hardware
-> Linux V4L2 /dev/video0
-> ICameraFrameSource
   -> V4l2YuyvFrameSource
   -> VendorUvcFrameSource fallback
   -> NullCameraFrameSource test/disabled
-> YuyvToGray
-> CameraCaptureWorker
-> CameraFrameStore
   -> PerceptionFrontend
   -> SteeringMediaService
-> SteeringFramePerceptionPipeline
-> PerceptionResult
-> control loop
```

只有 camera source 知道 V4L2 / YUYV / supplier backend。BEV、circle、cross、visual reference、media protocol、control 都不读取 `/dev/video0`、V4L2 buffer metadata、OpenCV `Mat` 或 supplier `rgay_image`。

### 7.1 Camera Source

`ICameraFrameSource` 只隐藏具体相机后端：

```text
Start(config)
Stop()
WaitRawFrame(timeout)
Health()
```

`V4l2YuyvFrameSource` 是主路径，负责：

- `open("/dev/video0", O_RDWR | O_NONBLOCK)`
- `VIDIOC_QUERYCAP`
- `VIDIOC_S_FMT` with `V4L2_PIX_FMT_YUYV`
- `VIDIOC_S_PARM`
- `VIDIOC_REQBUFS`
- `VIDIOC_QUERYBUF` / `mmap`
- `VIDIOC_QBUF`
- `VIDIOC_STREAMON`
- `poll`
- `VIDIOC_DQBUF`
- `VIDIOC_STREAMOFF`
- `munmap`
- `close`

在一次 `WaitRawFrame(timeout)` 中，source 可以 drain 当前 ready 的 V4L2 buffers，并返回本次最后 dequeued 的 raw frame。这是 raw source 层的选择，不是全局 latest 语义。全局 latest / history 只属于 `CameraFrameStore`。

`VendorUvcFrameSource` 只是 fallback。它可以调用 supplier `wait_image_refresh()`，但不能把 `rgay_image` 暴露给上层；supplier/global pointer 必须在 backend 边界内转成提交给 frame store 的 frame fact。

### 7.2 YUYV To Gray

`YuyvToGray` 是纯像素格式适配：

```text
YUYV input bytes: Y0 U0 Y1 V0
gray output:      Y0 Y1
```

它必须尊重 V4L2 `bytesperline`，不能假设每行一定是 `width * 2` 字节。它不计算 threshold、BEV projection、sparse rows、circle/cross evidence 或 debug overlay color。

### 7.3 Camera Capture Worker

`CameraCaptureWorker` 是 blocking producer only。它拥有 capture thread，循环执行：

```text
raw_frame = source.WaitRawFrame(timeout)
gray_frame = YuyvToGray(raw_frame)
frame_store.Submit(gray_frame, metadata)
```

它不拥有：

- latest frame selection
- frame history
- frame id deduplication
- consumer selection
- perception skip policy
- media drop policy
- safety / control decision

这样长等待只停留在 capture thread 内。camera 慢、backend 阻塞或 supplier fallback 慢，都不能通过 worker 阻塞 main loop、perception tick、media tick 或 control timer callback。

### 7.4 Camera Frame Store

`CameraFrameStore` 拥有 frame fact storage 和 latest 语义：

```text
Submit(gray_frame, metadata)
TryGetLatestAfter(last_seen_frame_id)
FindExact(frame_id, capture_time_ms)
LatestHandle()
Health()
```

它负责：

- writable slot selection
- owned gray frame slots
- frame id / generation update
- latest handle
- recent history
- overwritten / dropped / skipped counters
- media exact lookup

它可以复用现有 `CameraFrameHandle`、`OwnedCameraFrameSlot`、`CameraCaptureHistory` 概念，但写入者应从 `PerceptionFrontend` 前移到 `CameraCaptureWorker`。Perception 和 media 都是 store consumers。

### 7.5 Consumers

`PerceptionFrontend` 变成非阻塞消费者：

```text
handle = frame_store.TryGetLatestAfter(last_processed_frame_id)
if no handle:
  return
frame = frame_store.View(handle)
ProcessFrame(frame)
```

它不调用 V4L2、supplier UVC、OpenCV 或 `wait_image_refresh()`。

`SteeringMediaService` 只按 handle 消费 frame history。frame 已被覆盖、网络慢或编码慢时，跳过该 media frame；media 不能长期 pin slot 反向阻塞 capture。若所有 slot 都不可写，store 可以丢弃新 frame 并记录事实。

`ControlLoop` 不消费 camera frame。它消费 `PerceptionResult`，并在 reference time alignment 后消费 control-time reference facts。

### 7.6 Camera Timestamp And Health Facts

camera path 应发布 timestamp 和 health facts，但不改变视觉语义：

```text
camera_frame:
  source
  frame_id
  capture_time_ms
  dequeue_time_ms
  width
  height
  stride
  v4l2_sequence
  v4l2_timestamp_valid
  drained_buffer_count
  overwritten_frame_count
  dropped_frame_count
  poll_wait_us
  dequeue_us
  yuyv_to_gray_us
  store_submit_us
```

`capture_time_ms` 优先使用 V4L2 buffer timestamp。若 driver timestamp 不可用或不可信，退回 dequeue time，并在 debug facts 中显式记录 fallback。这些是调度和诊断事实，不是 circle/cross/bend evidence。

## 8. Runtime 模块边界

### `steering_bev_simple_perception.*`

- 负责 sparse BEV row scan、white intervals、line reference。
- 产出的 rows 是 line/cross/circle Phase1 的公共事实面。
- 不 include circle/cross detector。
- 不判断 cross/circle present。
- 不执行 Phase2 ROI scan。

### `steering_cross_exit_element_evidence.*`

- 只消费 sparse rows 和参数。
- 只输出 cross evidence 和可选 candidate。
- 不读取 circle。

### `steering_circle_element_evidence.*`

- Phase1 消费 sparse rows 和参数，输出 raw circle records。
- Phase2 在 takeover enabled 且 effective circle present 后，通过 ROI sampler 生成 entry facts 和 circle candidate。
- 不读取 cross。
- 不读取 hold、safety、IMU、encoder、yaw、actuator。

### `steering_visual_element_pipeline.*`

- 负责注册 cross/circle detectors。
- 负责 cross suppression circle effective records。
- 负责决定是否调用 circle Phase2 candidate builder。
- 不执行 hold，不判断 usability/lateral error/readiness/safety/yaw。

### `steering_bev_element_raster.*`

- V1 不再位于 runtime circle/cross 热路径。
- 可保留给 debug overlay、authority-baseline probe、后续 ML/roadblock 或特殊离线验证。
- 不应为了 circle Phase2 在每帧构建 full raster。

### `steering_frame_perception_pipeline.*`

- 调用 `RunBEVSimplePerception()` 得到 row facts 和 line candidate。
- 调用 visual element pipeline。
- 只有在 pipeline 需要时才提供 ROI sampler 所需的 frame/projector/threshold 上下文。
- 不再无条件调用 `element_raster_builder_.Build()` 作为 circle/cross 前置步骤。
- 发布 selected 或 held reference 时保留 reference source time；若 hold 复用旧 reference，不改写为当前帧时间。

### `camera_frame_source.*`

- 拥有具体 camera backend：V4L2 YUYV、supplier fallback、disabled/test source。
- 输出 raw camera frame facts 和 backend health。
- 不发布 `PerceptionResult`。
- 不构建 BEV rows、circle/cross evidence、media packets 或 control decisions。

### `camera_capture_worker.*`

- 拥有 blocking capture thread。
- 把 raw YUYV frame 转成 gray frame submission。
- 不拥有 latest/history 语义；只向 `CameraFrameStore` 提交 frame facts。

### `camera_frame_store.*`

- 拥有 owned gray frame slots、latest handle、frame history、`(frame_id, capture_time_ms)` lookup。
- 向 perception/media 提供非阻塞 consumer API。
- 不读取 V4L2、supplier globals、BEV facts 或 control state。

### `perception_frontend.*`

- 从 `CameraFrameStore` 消费 latest unprocessed gray frame。
- 只有存在新 frame 时才运行 frame perception pipeline。
- 不阻塞 camera capture，也不拥有 media frame history。

### `steering_reference_time_alignment.*`

- 只消费 selected/held reference、reference source time、control time、motion history 和 alignment 参数。
- 只输出 aligned reference 与 alignment debug facts。
- 不读取 camera frame、sparse rows、circle/cross evidence、safety gate、yaw target 或 actuator command。
- 不选择 reference，不改变 reference source，不做 hold，不判断视觉元素。

### `control_loop.*`

- 每个 control tick 记录 motion history sample。
- 在使用 reference 计算 lateral error 前调用 reference time alignment。
- 基于 aligned reference 计算或刷新 usability、lateral error、reference-control readiness。
- safety gate、yaw controller、actuator 继续只消费控制侧事实，不反向修改视觉结果。

## 9. Performance Contract

V1 的性能目标是移除普通帧上的 full element raster 热路径：

```text
before:
  perception.element_raster.cells ~= 11ms / frame

after:
  ordinary frame:
    no full element raster build
  circle effective + takeover enabled:
    circle.phase2.roi_scan only scans local support points
```

建议 perf stage：

- `circle.phase1.rows`
- `circle.phase2.roi_scan`
- `circle.phase2.reference_build`
- `visual.element_pipeline`
- `motion.history_record`
- `reference.time_alignment`
- `camera.v4l2_poll`
- `camera.v4l2_dequeue`
- `camera.yuyv_to_gray`
- `camera.store_submit`
- `camera.frame_age`

`camera.capture` 阻塞等待与 `main.loop` 的解耦属于独立 runtime scheduling 工作，不混入 circle/cross 识别实现。reference time alignment 解决的是残余 capture-to-control 时延的坐标语义，即使后续调度加速，它仍然是控制侧应有的时间对齐层。

### 9.1 当前实测基线

以下耗时来自最近一次带细分 perf 的板端 run：

```text
new/verification/perf-instrumented-drive-20260520T131637Z/new_runtime.log
stable window timestamp: 3230733
unit: ms / frame unless noted
```

这些数字描述的是 V1 实现前的当前代码。V1 估算采用当前参数和当前 perf 成本做线性上界，不把估算写成实测：

```text
forward rows = 24
lateral samples = floor((2 * 0.65) / 0.02) + 1 = 66
sparse entries per frame = 24 * 66 = 1584
full raster cells = 320 * round(1.5 * 320 / 1.3) = 320 * 369 = 118080

sparse scan cost = 1.49ms / 1584 ~= 0.94us / sparse entry
old full raster classify cost = 11.39ms / 118080 ~= 0.096us / raster cell
old visual pipeline scan cost proxy = 7.23ms / 118080 ~= 0.061us / raster cell
```

| V1 流水线步骤 | 当前 perf stage | 当前实测 | V1 严谨估算 | 标注 |
| --- | --- | ---: | ---: | --- |
| `camera frame` | `camera.capture` | `13.73` | `13.73` | 当前 camera capture/decode/gray conversion 会阻塞 foreground perception path；YUYV capture decoupling 把该等待移到 capture thread。 |
| `Otsu threshold` | `perception.otsu` | `0.20` | `0.20` | 保持现状。 |
| `sparse BEV row scan` 总计 | `bev.simple` | `1.52` | `1.52` | V1 公共事实面，line/cross/circle Phase1 共用。 |
| sparse LUT 检查 | `bev.simple.lut` | `0.01` | `0.01` | 保持现状。 |
| sparse row 扫描 | `bev.simple.scan_rows` | `1.49` | `1.49` | V1 cross/circle Phase1 不应重复 camera -> BEV 投影。 |
| line reference builder | `bev.simple.build_reference` | `0.01` | `0.01` | 保持现状。 |
| line candidate 包装 | `visual.line_candidate` | `0.01` | `0.01` | 保持现状。 |
| cross Phase1 detector | included in `visual.element_pipeline` | 未隔离 | `<0.05` | 只遍历 24 个 row 和少量 intervals；即使按 O(rows + intervals) 保守估算，也应低于 0.05ms。实现后加 stage 验证。 |
| circle Phase1 detector | included in `visual.element_pipeline` | 未隔离 | `0.10-0.20` | 用旧 visual scan 每 cell 成本估算：`1584 * 0.061us ~= 0.10ms`，加 row fitting/sort 固定开销后取 `0.20ms` 上界。 |
| 旧 full element raster 总计 | `perception.element_raster` | `11.52` | `0.00` | V1 ordinary frame 删除该热路径。 |
| 旧 full raster LUT | `perception.element_raster.lut` | `0.00` | `0.00` | V1 ordinary frame 不运行。 |
| 旧 full raster storage | `perception.element_raster.storage` | `0.00` | `0.00` | V1 ordinary frame 不运行。 |
| 旧 full raster class table | `perception.element_raster.class_table` | `0.10` | `0.00` | V1 ordinary frame 不运行。 |
| 旧 full raster cell sampling | `perception.element_raster.cells` | `11.39` | `0.00` | V1 ordinary frame 删除的主要确定收益。 |
| visual element pipeline 总计 | `visual.element_pipeline` | `7.23` | `0.20-0.35` | 当前包含 cross、旧 circle raster-row evidence、aggregation；V1 ordinary frame 只剩 sparse Phase1 + aggregation。 |
| cross + raw circle aggregation | included in `visual.element_pipeline` | 未隔离 | `<0.02` | 只处理固定 records 和 candidate summary。 |
| effective circle suppression | included in `visual.element_pipeline` | 未隔离 | `<0.01` | 只在 pipeline 层改 effective record，接近常数级。 |
| circle Phase2 ROI scan, takeover disabled | not implemented | `0.00` | `0.00` | 默认 `CIRCLE_ENTRY_TAKEOVER_ENABLED=0`，runtime 不运行 ROI scan。 |
| circle Phase2 ROI scan, takeover enabled typical | not implemented | 待测 | `0.10-0.50` | 假设 12-24 rows、每行边界向内 4-8 个 lateral samples、rear/side-rear 两次采样：`24*8*2*0.94us ~= 0.36ms`，加固定开销取 `0.50ms`。 |
| circle Phase2 ROI scan, takeover enabled conservative worst | not implemented | 待测 | `3.0-6.0` | 最坏扫描完整 interval：`24*66*2*0.94us ~= 2.98ms`；若不用既有 LUT、投影开销按 2x 放大，上界约 `6ms`，仍低于旧 full raster cell sampling `11.39ms`。 |
| circle entry reference build | not implemented / included in candidate builder | 待测 | `<0.10` | 支撑点数量最多约 24，线性插值到 24 个 reference samples。 |
| visual reference selection | `visual.reference_select` | `0.29` | `0.20-0.35` | candidate 数保持很小，预计接近当前值；开启 circle candidate 后继续观测。 |
| reference continuity / hold | `reference.hold` when used | 约 `0.20` | `0.20` | 只在 current visual reference unusable 时运行；held reference 必须保留原 source time。 |
| reference time alignment | not implemented | `0.00` | `0.02-0.05` | 控制侧对最多 24 个 reference samples 做一次平移+旋转；积分使用 control tick 已记录 motion history。 |
| reference usability | `reference.usability` | `0.00` | `0.00` | V1 目标是在 aligned reference 上计算；成本保持接近当前。 |
| reference lateral error | `reference.lateral_error` | `0.01` | `0.01` | V1 目标是在 aligned reference 上计算；成本保持接近当前。 |
| reference-control readiness | `reference.control_readiness` | `0.00` | `0.00` | V1 目标使用 aligned usability/lateral error；成本保持接近当前。 |
| perception result build | `perception.result_build` | `0.09` | `0.09` | 保持现状。 |
| perception publish | `perception.publish` | `0.19` | `0.19` | 保持现状。 |
| full perception frame, ordinary frame | `perception.frame` | `35.60` | `~17.1` | `35.60 - 11.52 - (7.23 - 0.30) ~= 17.15ms`；仍包含 `camera.capture ~=13.73ms`。 |
| main loop, ordinary frame | `main.loop` | `38.24` | `~19.8` | `38.24 - 11.52 - (7.23 - 0.30) ~= 19.79ms`；达到完整 10ms 周期仍需要 runtime scheduling 解耦。 |
| full perception frame, circle takeover typical | `perception.frame` | `35.60` | `~17.6` | ordinary estimate + typical ROI `0.50ms`。 |
| main loop, circle takeover typical | `main.loop` | `38.24` | `~20.3` | ordinary estimate + typical ROI `0.50ms`。 |
| control loop tick | `control.tick` | `2.20` | `~2.25` | 追加 motion history record + reference time alignment，主要成本仍是 IMU read。 |
| motion history record | not implemented / included in `control.tick` | `0.00` | `<0.01` | ring buffer 写入一个小结构。 |
| IMU read | `control.imu_read` | `1.85` | `1.85` | 控制侧独立优化点。 |
| encoder read | `control.encoder_read` | `0.09` | `0.09` | 保持现状。 |
| control decision | `control.decision` | `0.00` | `0.00` | 保持现状。 |
| actuator apply | `control.apply` | `0.03` | `0.03` | 保持现状。 |

当前可确认的确定收益：

```text
ordinary frame removes:
  perception.element_raster.cells ~= 11.39ms
  perception.element_raster total ~= 11.52ms
```

当前不能直接确认的收益：

- `visual.element_pipeline` 中旧 circle evidence 扫描占比尚未独立埋点，但按 cell-count 上界可估算 V1 ordinary frame 约 `0.20-0.35ms`。
- V1 `circle.phase1.rows` 尚未实现，估算 `0.10-0.20ms`，需实现后用 stage 实测确认。
- V1 `circle.phase2.roi_scan` 尚未实现；默认 takeover disabled 为 `0ms`，开启后 typical 估算 `0.10-0.50ms`，conservative worst 估算 `3.0-6.0ms`。

### 9.2 按完整流程标注

下面是更接近阅读和排查时使用的顺序时间线。`+` 后面的值是该步骤自身耗时，括号内是从进入该流程开始的累计耗时。当前实测仍来自同一 perf window；V1 为上节估算。

#### 当前实测流程

```text
main.loop start
  -> camera.capture                         +13.73ms  (13.73ms)
  -> Otsu threshold                          +0.20ms  (13.93ms)
  -> sparse BEV row scan total               +1.52ms  (15.45ms)
       -> sparse LUT                         +0.01ms
       -> scan rows                          +1.49ms
       -> line reference build               +0.01ms
  -> line candidate                          +0.01ms  (15.46ms)
  -> full element raster                     +11.52ms  (26.98ms)
       -> LUT                                +0.00ms
       -> storage                            +0.00ms
       -> class table                        +0.10ms
       -> cell sampling/classification       +11.39ms
  -> visual element pipeline                  +7.23ms  (34.21ms)
       -> cross Phase1                       included
       -> old circle raster-row evidence     included
       -> aggregation / candidate summaries  included
  -> visual reference selection               +0.29ms  (34.50ms)
  -> reference usability                      +0.00ms  (34.50ms)
  -> reference lateral error                  +0.01ms  (34.51ms)
  -> reference-control readiness              +0.00ms  (34.51ms)
  -> perception result build                  +0.09ms  (34.60ms)
  -> perception publish                       +0.19ms  (34.79ms)
  -> assistant/media/main overhead            +3.45ms  (38.24ms main.loop)
```

这里的 `perception.frame` 实测为 `35.60ms`，略高于上面按可见 stage 相加的 `34.79ms`，差值来自未单独展开的小段开销和计时窗口差异。

#### V1 Ordinary Frame 估算

ordinary frame 指没有 circle takeover，且不构建 full element raster 的普通帧。

```text
main.loop start
  -> camera.capture                         +13.73ms  (13.73ms)
  -> Otsu threshold                          +0.20ms  (13.93ms)
  -> sparse BEV row scan total               +1.52ms  (15.45ms)
       -> sparse LUT                         +0.01ms
       -> scan rows                          +1.49ms
       -> line reference build               +0.01ms
  -> line candidate                          +0.01ms  (15.46ms)
  -> full element raster                      +0.00ms  (15.46ms)
       -> V1 ordinary frame skips full raster
  -> visual element pipeline                  +0.30ms  (15.76ms)
       -> cross Phase1                       <0.05ms
       -> circle Phase1 rows                  0.10-0.20ms
       -> aggregation / suppression           <0.03ms
  -> visual reference selection               +0.29ms  (16.05ms)
  -> reference continuity / hold              +0.00ms  (16.05ms typical)
       -> held reference keeps original source time
  -> perception result build                  +0.09ms  (16.15ms)
  -> perception publish                       +0.19ms  (16.34ms)
  -> assistant/media/main overhead            +3.45ms  (19.79ms estimated main.loop)
```

V1 ordinary frame 的核心收益来自两处：

```text
remove full element raster:     -11.52ms
replace visual.element_pipeline:  7.23ms -> ~0.30ms
net reduction:                 ~18.45ms
```

因此按当前同步 camera 模型估算：

```text
perception.frame: 35.60ms -> ~17.1ms
main.loop:        38.24ms -> ~19.8ms
```

#### V1 Circle Takeover Typical 估算

circle takeover typical 指 effective circle present 且 `CIRCLE_ENTRY_TAKEOVER_ENABLED=1`，Phase2 ROI scan 按 typical 规模运行。

```text
main.loop start
  -> camera.capture                         +13.73ms  (13.73ms)
  -> Otsu threshold                          +0.20ms  (13.93ms)
  -> sparse BEV row scan total               +1.52ms  (15.45ms)
  -> line candidate                          +0.01ms  (15.46ms)
  -> full element raster                      +0.00ms  (15.46ms)
  -> visual element pipeline Phase1           +0.30ms  (15.76ms)
  -> circle Phase2 ROI scan                   +0.50ms  (16.26ms)
       -> typical: 12-24 rows, 4-8 lateral probes per row, rear/side-rear checks
  -> circle entry reference build             +0.10ms  (16.36ms)
  -> visual reference selection               +0.29ms  (16.65ms)
  -> reference continuity / hold              +0.00ms  (16.65ms typical)
  -> result build + publish                   +0.28ms  (16.93ms)
  -> assistant/media/main overhead            +3.45ms  (20.39ms estimated main.loop)
```

conservative worst 下，如果 ROI 退化到完整 sparse interval 探测且投影成本按 2x 放大：

```text
circle Phase2 ROI scan: 3.0-6.0ms
main.loop estimate:     ~22.8-25.8ms
```

这仍低于保留旧 full raster 热路径的当前 `38.24ms main.loop`，但不能满足完整 `10ms` 周期。`10ms` 目标需要把 `camera.capture ~= 13.73ms` 从 main/control 同步路径中解耦。

#### V1 Control Tick Reference Alignment 估算

control tick 不等待相机，也不重跑视觉。它使用最新发布的 selected/held reference，并在使用前对齐到当前时刻：

```text
control.tick start
  -> IMU read                                +1.85ms
  -> encoder read                            +0.09ms
  -> motion history record                   +<0.01ms
  -> copy latest perception/reference         included
  -> reference time alignment                +0.02-0.05ms
       -> integrate capture_time_ms -> now_ms
       -> transform up to 24 BEV path samples
  -> aligned reference usability              +0.00ms
  -> aligned reference lateral error          +0.01ms
  -> aligned reference-control readiness      +0.00ms
  -> safety gate / yaw / actuator             remaining control.tick cost
```

该路径的目标不是降低 perception frame 耗时，而是消除“旧车身坐标下的大误差被当作当前误差继续控制”的语义错误。

#### V1 YUYV Camera Capture 解耦估算

YUYV camera path 改变的是 capture cost 的归属：

```text
capture thread:
  -> V4L2 poll / DQBUF wait                 outside main/control path
  -> drain ready buffers                    outside main/control path
  -> YUYV -> gray 320x240                   ~0.08-0.25ms estimate
  -> CameraFrameStore submit                ~0.05-0.15ms estimate

main/perception thread:
  -> TryGetLatestAfter                      ~0.00ms
  -> ProcessFrame only when new frame exists

control.tick:
  -> never waits for camera frame
```

目标不是让物理 camera 每 10ms 产生一张新图，而是防止 camera wait、backend buffering、media send 或 frame copy 阻塞 control-time work。

## 10. 参数语义

现有 `BEV_ELEMENT` 参数继续使用，但部分说明需要随实现更新：

- `CIRCLE_MIN_SUPPORT_ROWS`：从 dense raster row 数改为 sparse BEV support row 数。
- `CIRCLE_MIN_SAMPLEABLE_PER_ROW`：从 raster cell 数改为 sparse lateral sample 数。
- `CIRCLE_OPEN_EXPANSION_MIN_M`：仍表示开口最小绝对外扩量。
- `CIRCLE_OPENING_EXPANSION_RATIO_MIN`：仍表示窗口净外扩比例。
- `CIRCLE_OPPOSITE_STRAIGHT_DRIFT_MAX_M`：仍表示对侧简单拟合残差上限。
- `CIRCLE_OPPOSITE_SHRINK_RATIO_MIN`：仍表示对侧内缩比例阈值。
- `CIRCLE_ENTRY_TAKEOVER_ENABLED`：关闭时 runtime 不运行 Phase2 ROI scan。
- `BEV_ELEMENT_RASTER.ENABLED`：不再控制 circle/cross V1 runtime recognition；只控制 full raster debug/legacy/未来消费者。

默认值先保持不变，除非 authority-baseline 重放显示 sparse row 语义下需要调整。

reference time alignment 使用独立参数族，避免混入 `BEV_ELEMENT`：

- `REFERENCE_TIME_ALIGNMENT.ENABLED`：是否让控制侧使用 aligned reference。迁移阶段可先 diagnostics-only，验证通过后再进入控制路径。
- `REFERENCE_TIME_ALIGNMENT.MAX_AGE_MS`：reference 从 capture time 到 control time 的最大可对齐年龄。
- `REFERENCE_TIME_ALIGNMENT.MAX_INTEGRATION_GAP_MS`：motion history 中允许的最大采样空洞。
- `REFERENCE_TIME_ALIGNMENT.MAX_DELTA_YAW_RAD`：单次对齐允许的最大 yaw 积分量，超过说明 reference 已太旧或传感器异常。
- `REFERENCE_TIME_ALIGNMENT.MIN_ALIGNED_SAMPLES`：对齐后仍需保留的前方 reference 样本数。

这些参数只定义坐标变换的有效范围，不表达 circle/cross/bend 语义，也不作为转向限幅或旧感知降权旋钮。

camera source 使用独立参数族，避免混入 `BEV_ELEMENT`：

- `CAMERA_SOURCE.BACKEND`：主路径先定义为 `v4l2_yuyv`，fallback 可为 `vendor_uvc`。
- `CAMERA_SOURCE.DEVICE`：默认 `/dev/video0`。
- `CAMERA_SOURCE.WIDTH`：默认 `320`。
- `CAMERA_SOURCE.HEIGHT`：默认 `240`。
- `CAMERA_SOURCE.FPS`：默认 `60`。
- `CAMERA_SOURCE.BUFFER_COUNT`：默认 `3`。
- `CAMERA_SOURCE.POLL_TIMEOUT_MS`：capture thread 内的 bounded wait。
- `CAMERA_SOURCE.DRAIN_READY_BUFFERS`：V4L2 backend 是否在一次 wait 中 drain 当前 ready raw buffers。
- `CAMERA_SOURCE.FALLBACK_BACKEND`：primary startup validation 失败时使用的 fallback backend。

这些参数描述 frame acquisition 和 scheduling，不改变 threshold、BEV geometry、circle/cross detection、safety gate 或 yaw control。

## 11. Verification

本架构对应的实现验证应覆盖：

- circle Phase1 sparse 单测：
  - 左侧开口 + 右侧稳定 -> `circle_left_raw.present=true`
  - 右侧开口 + 左侧稳定 -> `circle_right_raw.present=true`
  - 一侧外扩 + 对侧内缩 -> bend / non-circle
  - 两侧外扩 -> both_sides_open / non-circle
  - `0.10, 0.30, 0.29` 净外扩成立
  - support rows 不足、sampleable 不足、low confidence fail closed

- pipeline 单测：
  - cross 和 circle raw 互不知晓
  - cross suppress 只影响 effective circle
  - takeover disabled 不运行 Phase2、不 build candidate
  - takeover enabled 且 entry facts valid 时 circle candidate 进入 arbitration

- Phase2 ROI 单测：
  - left frontier 左上、right frontier 右上
  - unknown / invalid / outside frame / projection failed 不作为 black frontier
  - frontier 点数不足 fail closed
  - direction delta 小于阈值 fail closed
  - join jump / interpolation gap 超限 fail closed

- authority-baseline 重放：
  - `circle-1/2/3.raw`：cross false，circle_left effective true
  - takeover enabled 时 circle candidate 可被选择
  - `cross-1/2/3.raw`：cross true，circle effective false
  - `bend-1/2/3.raw`：cross false，circle false

- perf 回归：
  - ordinary frame 不出现 full `perception.element_raster.cells` 热路径
  - circle takeover disabled 时不出现 `circle.phase2.roi_scan`
  - circle takeover enabled 时 ROI scan 时间可解释且低于 full raster 热路径
  - reference time alignment 单次耗时在 `0.02-0.05ms` 量级或有可解释原因

- reference time alignment 单测：
  - `delta_s=0, delta_yaw=0` 时 aligned path 与输入 path 一致
  - 正 yaw 积分的符号与 BEV/yaw 坐标约定一致
  - 前进距离会把近端点移动到更小 `forward_m`，车后点被丢弃
  - missing motion history、history gap、age 超限、delta yaw 超限 fail closed
  - held reference 使用原 source time，不能被当前帧时间刷新年龄

- YUYV camera source / store 单测：
  - YUYV `Y0 U0 Y1 V0` 只把 Y samples 写入 gray output
  - conversion 尊重 `bytesperline`
  - unsupported format、unsupported geometry、mmap buffer unavailable fail closed
  - `CameraCaptureWorker` 只 submit frame facts，不直接更新 latest
  - `CameraFrameStore` 拥有 latest handle、history lookup、generation check、overwrite counters
  - `PerceptionFrontend` 在没有新 frame 时非阻塞返回
  - media lookup miss 或 slow send 只跳过 media frame，不阻塞 capture

## 12. Migration Steps

建议实现顺序：

1. 抽出 circle Phase1 row observation helper，使其从 sparse rows 产生 opening/straight/shrink facts。
2. 修改 `DetectCircleElementEvidence()` 输入为 sparse rows。
3. 修改 `RunVisualElementPipeline()`，让 circle Phase1 与 cross 使用同一份 rows。
4. 移除 `SteeringFramePerceptionPipeline::ProcessFrame()` 中无条件 full element raster build。
5. 增加 ROI metric sampler，作为 Phase2 builder 的局部工具。
6. 把 Phase2 调用移动到 effective circle + takeover enabled 之后。
7. 修正 camera capture timestamp：优先硬件帧时间，否则用 `wait_image_refresh()` 返回后的时间。
8. 在 control tick 中记录 motion history ring buffer。
9. 新增 `steering_reference_time_alignment.*`，先 diagnostics-only 输出 aligned facts。
10. 将 control-side usability、lateral error、readiness 切到 aligned reference；保留 perception-time facts 仅作 debug 对照。
11. 更新参数文档与 perf stage。
12. 运行单测、authority-baseline、perf build 和板端 no-motion/drive 验证。
13. 新增 `ICameraFrameSource`，先实现 `V4l2YuyvFrameSource` startup probe，并把 supplier fallback 保持在同一接口后面。
14. 新增 `YuyvToGray` 单测，再接入 runtime。
15. 新增 `CameraCaptureWorker` 和 `CameraFrameStore`，确保 latest/history 只由 Frame Store 拥有。
16. 将 `PerceptionFrontend` 从 blocking capture 改为 non-blocking `TryGetLatestAfter()`。
17. 将 steering media 改为从 `CameraFrameStore` history 查帧，lookup miss 或发送慢时丢 media frame。
18. 增加 camera perf facts，先做 board no-motion capture 验证，再进入 drive 验证。

每一步都应保持旧消费者可编译。full element raster 类型可以先保留，避免把架构迁移和 debug/overlay 清理混在同一次改动里。
