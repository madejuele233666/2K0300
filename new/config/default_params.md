# `default_params.json` 说明与调参攻略

本文对应当前仓库里的 [`new/config/default_params.json`](./default_params.json)。目标不是解释控制理论，而是回答三个现场问题：

1. 这个文件里的键各自控制什么。
2. 哪些键改坏了会直接影响启动或联调。
3. 遇到具体症状时，应该按什么顺序调。

## 1. 文件角色与加载语义

运行时参数由 `new/code/platform/param_store.cpp` 读取。

- 缺文件：系统会回退到内建默认值，并打 `params.missing`。
- JSON 非法、必填字段缺失、字段类型错误：系统会回退到内建默认值，并打 `params.parse`。
- `P_Mode` 和 `exp_light` 是启动关键字段：
  - `P_Mode` 必须在 `0..4`
  - `exp_light` 必须在 `0..2500`
  - 两者非法时会打 `params.critical.apply` fail-safe，执行器不会进入正常解锁路径。
- `exp_light != 65` 虽然允许，但会额外打 `params.critical.exp_light` 警告；当前 direct-match 相机路径对非默认曝光并不天然稳妥。

当前代码中的必填键：

- `Speed_base`
- `see_max`
- `PID_TURN_GYRO_CAMERA.D`
- `P_Mode`
- `exp_light`
- `LEFT_WHEEL_PID.{P,I,D,INTEGRAL_LIMIT}`
- `RIGHT_WHEEL_PID.{P,I,D,INTEGRAL_LIMIT}`
- `assistant_tcp.{host,port}`

其余键按“可选覆盖”处理；缺省时使用 `RuntimeParameters` 里的内建值。

兼容保留键：

- `PID_TURN_CAMERA.D`
  - 当前仅为兼容旧参数文件保留。
  - 缺省时回退到内建值 `0.0`。
  - 非零时会打 `params.deprecated.pid_turn_camera_d` 警告，但不会参与当前控制输出。

## 2. 当前参数面速查

### 2.1 车速与视觉基础

- `Speed_base = 150.0`
  - 默认基准速度。动态调参和正式跑车都会围绕它展开。
- `see_max = 24.0`
  - 视觉向上扫描上限。值越小，越偏近场；值越大，越看远处行。
- `P_Mode = 3`
  - 启动关键字段，相机/旧链路兼容模式之一。非必要不要改。
- `exp_light = 65`
  - 启动关键字段，当前推荐基线曝光。

### 2.2 外环相机转向 PID

- `PID_TURN_CAMERA.USE_FUZZY = 0`
  - 是否启用 fuzzy 外环。
- `PID_TURN_CAMERA.P = 60.0`
  - 相机外环主比例项。决定看见偏差后“先打多少方向”。
- `PID_TURN_CAMERA.P_SCALE = 1.0`
  - 外环 P 的额外缩放。
- `PID_TURN_CAMERA.D = 0.0`
  - 当前仅为兼容旧参数文件保留，不参与 `LegacyPidControl::ComputeTurnTarget()` 的相机外环输出。
  - 缺省即可；建议固定为 `0.0`。若配置成非零，运行时会提示该值已忽略。
  - 现场不要把它当成主要抑摆旋钮；要抑制摆动，优先看 `PID_TURN_CAMERA.P`、`PID_TURN_GYRO_CAMERA.D`、`raw_turn_output_limit` 和几何证据稳定性。

### 2.3 内环陀螺转向 PID

- `PID_TURN_GYRO_CAMERA.P = 1.0`
  - 陀螺内环比例。
- `PID_TURN_GYRO_CAMERA.I = 0.0`
  - 陀螺内环积分。默认关闭。
- `PID_TURN_GYRO_CAMERA.D = 0.0`
  - 陀螺内环微分。当前必填。

### 2.4 轮速 PID

- `LEFT_WHEEL_PID.{P,I,D} = 84.0 / 2.4 / 0.75`
- `RIGHT_WHEEL_PID.{P,I,D} = 96.0 / 2.2 / 0.2`
- `INTEGRAL_LIMIT = 2200.0`
  - 左右轮积分上限。
- `MEASUREMENT_FILTER_ALPHA = 0.4`
  - 轮速测量低通。

这组参数主要影响“速度跟随稳定性”，不是场景识别问题的首选调节面。

### 2.5 执行器与运动约束

- `pwm_limit = 3000`
  - PWM 上限。
- `raw_turn_output_limit = 8000`
  - 原始转向输出上限。
- `pwm_floor = 0`
  - 非零命令的最小 PWM。当前关闭。
- `prohibit_reverse_pwm = 1`
  - 禁止反转 PWM。
- `prohibit_reverse_pwm_step_limit = 280`
  - 反转保护步长限制。
- `motion_unveto_confirm_cycles = 3`
- `motion_spinup_ms = 600`
- `motion_turn_limit_spinup = 0.35`
- `motion_pwm_step_limit = 280`
- `motion_stop_ms = 300`
- `motion_stop_encoder_threshold = 8`
- `motion_fault_rearm_hold_ms = 600`
- `wheel_turn_target_scale = 32.0`

这组参数影响“车能不能温和起步、转向时会不会把轮速目标拉得太狠”。除非你在处理动力/震荡问题，否则不要和视觉参数一起混调。

### 2.6 观测与联调

- `control_snapshot_emit_interval_ms = 100`
  - `control.steering_snapshot` 的板端输出周期。
- `assistant_enabled = 1`
- `assistant_waveform_publish_interval_ms = 40`
- `assistant_image_publish_interval_ms = 80`
- `assistant_tcp.host = 10.100.170.115`
- `assistant_tcp.port = 8888`
- `steering_media_enabled = 1`
- `steering_media_port = 8890`
- `steering_media_publish_interval_ms = 120`

这组参数主要服务联调和证据采集。它们不直接改变算法结论，但会影响你能不能看清问题。

当前普通 `straight / bend` 主几何已经走“底部连通追踪 + 陀螺连续性约束”链路，但这套链路没有额外开放 runtime 参数。也就是说：

- `default_params.json` 里没有专门给 bottom tracker / gyro continuity 的新旋钮。
- 这部分策略常量目前固化在代码里；现场先看证据，不要试图从 `SCENE_WIDE_CLASSIFIER` 里绕着调。
- 普通弯道参考线不对时，优先排查 `exp_light / see_max / PID_TURN_CAMERA / PID_TURN_GYRO_CAMERA`，不是先改 `circle / cross` 门槛。

看 `control.steering_snapshot` 或 steering-media 元数据时，普通弯道现在优先关注这些字段：

- `track_confidence`
  - 主轨迹整体置信度。低了先怀疑图像可见性、单边补线占比或历史守护。
- `track_seed_col / track_seed_score`
  - 底部主种子及其排序分数。适合排查“为什么这帧从这里起追踪”。
- `lateral_error / heading_error / curvature`
  - 当前控制主几何。
- `track_sign`
  - 当前帧主轨迹方向符号。负值可视作偏左弯，正值可视作偏右弯，零表示方向不够明确。
- `sign_flip_blocked`
  - 方向翻转保护是否介入。它为 `true` 时，说明系统在阻止“突然反打到另一侧”。
- `gyro_heading_delta_deg / gyro_consistency_score`
  - 陀螺短时连续性约束的观测量和一致性评分。它们只参与降权和守护，不单独造轨迹。
- `imu_grace_active`
  - IMU 短时失效宽限是否生效。为 `true` 时，本帧属于纯视觉保守运行窗口。
- `track_source`
  - 主轨迹来源：
  - `bottom_connected`：底部连通双边证据足够，主链路最理想。
  - `single_edge_completed`：单边真实可见，另一侧用最近稳定宽度补全；普通弯道允许，但它不是 `circle / cross` 证明。
  - `history_guarded`：当前帧图像证据不足，短时靠历史守护保守维持。
  - `pure_visual_fallback`：IMU grace 内取消陀螺加权，仅保留纯视觉保守输出。
- `phase / raw_turn_output / applied_turn_output`
  - 静态板端测试里若是 `DISARMED`，看到 `raw_turn_output=0` 和 `applied_turn_output=0` 是正常现象，不代表几何链路失效。

### 2.7 图像几何

- `camera_frame_width = 320`
- `camera_frame_height = 240`

这是当前算法假设的编译期兼容分辨率。除非同步改适配层和验证，否则不要改。

### 2.8 `SCENE_WIDE_CLASSIFIER`

这组是当前 `special_wide / circle_entry / cross` 判别的核心参数。

边界先说清楚：

- 它继续服务 `special_wide / circle_entry / cross` 的严格图像证据链。
- 它不再负责普通 `straight / bend` 的主参考线。
- 如果问题是“远处亮块把普通弯道参考线带跑了”或“明明左弯却翻成右打”，先看主轨迹链路证据，不要先动这里。

观测窗口：

- `LOWER_ROW_START/END = 156 / 184`
- `MIDDLE_ROW_START/END = 120 / 148`
- `UPPER_ROW_START/END = 80 / 112`
- `ROW_STEP = 4`
- `EDGE_MARGIN_PX = 12`
- `UPPER_FULL_SPAN_WIDTH_RATIO = 0.95`

宽场景前提：

- `SPECIAL_WIDE_LOWER_WIDTH_MIN_RATIO = 0.38`
- `SPECIAL_WIDE_VALID_ROWS_MIN = 10`

边线几何：

- `EDGE_MOTION_MIN_PX = 8`
  - 判定“边线确实在移动”的最小位移量。
- `EDGE_CURVATURE_MIN_PX = 6`
  - 判定“边线确实在弯”的最小曲率量。
- `OPPOSITE_EDGE_STRAIGHT_MAX_CURVATURE_PX = 5`
  - 对侧还能算“近直线”的最大曲率。
- `OPPOSITE_EDGE_BORDER_TOUCH_MAX_RATIO = 0.45`
  - 对侧贴边过多时，不认为它是可信的“直线”。

环岛/十字门槛：

- `CIRCLE_OPEN_MIN_PX = 24`
- `CIRCLE_CONTRACT_MIN_PX = 14`
- `CROSS_UPPER_FULL_SPAN_CONSEC_ROWS_MIN = 3`
- `CROSS_UPPER_FULL_SPAN_MIN_RATIO = 0.45`
- `TO_CROSS_MARGIN = 0.2`
- `TO_CIRCLE_MARGIN = 0.2`
- `TO_CIRCLE_OVER_BEND_MARGIN = 2.0`
  - 新增的 `circle vs ordinary bend` 仲裁边界。
  - 当图像同时像“普通弯道”和“弱环岛入口”时，只有 `circle_score` 明显压过当前普通弯道证据，才允许进入 `special_wide -> circle_entry`。
  - 这是 generic wide-scene 分类参数，不会改 `circle_entry` 里的补线偏移、环内参考或出环接管。
- `ENTER_CONFIRM_CYCLES = 2`
- `EXIT_CONFIRM_CYCLES = 2`

评分权重：

- `CROSS_WEIGHT_FULL_SPAN = 1.25`
- `CROSS_WEIGHT_BOTH_OPEN = 0.4`
- `CIRCLE_CURVE_WEIGHT = 1.2`
- `CIRCLE_OPPOSITE_STRAIGHT_WEIGHT = 1.0`
- `CIRCLE_WEIGHT_OPEN = 0.25`
- `CIRCLE_WEIGHT_CONTRACT = 0.2`

经验上，这组里最该优先看的是门槛，不是权重。先确定“像不像”，再调整“同类排序”。

### 2.9 环岛参数分组

现在环岛 steering 参数已经从 `SCENE_WIDE_CLASSIFIER` 里拆开。调参边界固定如下：

- `SCENE_WIDE_CLASSIFIER`
  - 只负责 `special_wide / circle_entry / cross` 候选确认和左右环候选打分。
- `CIRCLE_SCENE`
  - 只放环岛全过程的通用激活下限，例如最小有效行数和最小 track 置信度。
- `CIRCLE_ENTRY`
  - 只管入口补线、内侧偏移、`repairing -> entry_settle` 判据。
- `CIRCLE_INTERIOR`
  - 只管环内 `inner_offset / dual_edge_blend`。
- `CIRCLE_EXIT`
  - 只管对侧直线接管、handover、release 和出环完成判据。
- `CIRCLE_FALLBACK`
  - 只管 `fixSteer` 兜底偏置，不影响正常 `outer_offset` 主路径。

当前字段：

- `CIRCLE_SCENE.ACTIVE_VALID_ROWS_MIN = 8`
- `CIRCLE_SCENE.MINIMUM_TRACK_CONFIDENCE = 0.3`
- `CIRCLE_ENTRY.ENTRY_INNER_OFFSET_NEAR_PX = 48`
- `CIRCLE_ENTRY.ENTRY_INNER_OFFSET_FAR_PX = 28`
- `CIRCLE_ENTRY.ENTRY_REPAIR_OVER_DEG = 45.0`
- `CIRCLE_ENTRY.ENTRY_SETTLE_CONFIRM_CYCLES = 3`
- `CIRCLE_ENTRY.ENTRY_RELEASE_LOSS_CYCLES = 2`
- `CIRCLE_INTERIOR.INTERIOR_INNER_OFFSET_PX = 40`
- `CIRCLE_INTERIOR.INTERIOR_BLEND_ENABLE = 1`
- `CIRCLE_INTERIOR.INTERIOR_BLEND_MIN_CONFIDENCE = 0.55`
- `CIRCLE_EXIT.EXIT_OUTER_OFFSET_NEAR_PX = 34`
- `CIRCLE_EXIT.EXIT_OUTER_OFFSET_FAR_PX = 20`
- `CIRCLE_EXIT.EXIT_HANDOVER_START_DEG = 180.0`
- `CIRCLE_EXIT.HANDOVER_CONFIRM_CYCLES = 2`
- `CIRCLE_EXIT.HANDOVER_RAMP_CYCLES = 4`
- `CIRCLE_EXIT.EXIT_RELEASE_CYCLES = 3`
- `CIRCLE_EXIT.EXIT_COMPLETE_DEG = 300.0`
- `CIRCLE_EXIT.EXIT_OPPOSITE_EDGE_STRAIGHT_CONFIRM_CYCLES = 2`
- `CIRCLE_EXIT.EXIT_OPPOSITE_EDGE_MAX_CURVATURE_PX = 5`
- `CIRCLE_EXIT.EXIT_OPPOSITE_EDGE_MIN_VISIBLE_ROWS = 3`
- `CIRCLE_EXIT.EXIT_FIXSTEER_START_DEG = 235.0`
- `CIRCLE_EXIT.EXIT_FALLBACK_MAX_CYCLES = 6`
- `CIRCLE_FALLBACK.FIXSTEER_BIAS_SCALE = 0.55`

现场调参顺序建议固定：

1. 先用 `SCENE_WIDE_CLASSIFIER` 确认能不能稳进 `circle_entry`。
2. 再只动 `CIRCLE_ENTRY`，让 `repairing / entry_settle` 平稳。
3. 环内参考线不顺时只动 `CIRCLE_INTERIOR`。
4. 出环接不上直线时只动 `CIRCLE_EXIT`。
5. 只有正常出环还会丢对侧边时，最后才动 `CIRCLE_FALLBACK`。

## 3. 安全调参 workflow

推荐的最小闭环：

```bash
cd new/user
rtk env BOARD_IP=10.100.170.226 ./debug.sh assistant local 8888 8890
rtk env BOARD_IP=10.100.170.226 ./debug.sh build
rtk env BOARD_IP=10.100.170.226 ./debug.sh remote restart normal
rtk env BOARD_IP=10.100.170.226 ./debug.sh steering --duration-s 20
```

只改参数、不重新编译代码时可用：

```bash
cd new/user
rtk env BOARD_IP=10.100.170.226 ./start_with_params_upload.sh
```

调参原则：

1. 一次只动一组参数。
2. 一次只验证一个假设。
3. 每次都保留 evidence bundle，不靠记忆比较。
4. 不要同时改视觉、转向、轮速三类参数。
5. `assistant` 子命令会改写同一个 `default_params.json`，不要并行开多个调试流程。

## 4. 转向从零开始调参总流程

本节只覆盖转向相关参数，不覆盖 `LEFT_WHEEL_PID / RIGHT_WHEEL_PID`。如果车轮速度本身跟不住、左右轮同命令不同速、或者电机高频抖动，先按轮速问题单独处理；不要把轮速环问题伪装成转向问题来调。

从零开始时，不要一上来追求最高速度。目标顺序固定为：

1. 先让图像证据稳定。
2. 再让低速直道不摆。
3. 再让低速弯道能进弯、能回正。
4. 再升速度并收敛输出限幅。
5. 最后处理十字、环岛、宽场景等特殊场景。

### 4.1 建立基线参数

从当前 JSON 基线开始：

- `P_Mode = 3`
- `exp_light = 65`
- `see_max = 24.0`
- `Speed_base = 150.0`
- `PID_TURN_CAMERA.USE_FUZZY = 0`
- `PID_TURN_CAMERA.P = 60.0`
- `PID_TURN_CAMERA.P_SCALE = 1.0`
- `PID_TURN_CAMERA.D = 0.0`
- `PID_TURN_GYRO_CAMERA.P = 1.0`
- `PID_TURN_GYRO_CAMERA.I = 0.0`
- `PID_TURN_GYRO_CAMERA.D = 0.0`
- `raw_turn_output_limit = 8000`
- `motion_turn_limit_spinup = 0.35`
- `wheel_turn_target_scale = 32.0`

基线原则：

- `PID_TURN_CAMERA.D` 当前是兼容保留键，不是有效控制旋钮；保持 `0.0` 即可。
- `PID_TURN_GYRO_CAMERA.I` 从零开始必须保持 `0.0`，不要用积分补几何偏差。
- `USE_FUZZY` 从零开始保持 `0`，先用固定 P 把行为调明白。
- `raw_turn_output_limit` 先保持宽松，等方向链路调顺后再收限幅。
- `Speed_base` 是外环速度归一化基准，不是轮速 PID；从零调方向时先不要频繁改它。

### 4.2 先做静态证据检查

先不开电机，采直道、左弯、右弯、十字、环岛入口的静态帧。重点看 `control.steering_snapshot` 和 steering-media 元数据。

静态通过标准：

- 直道时 `lateral_error` 接近 0，`heading_error` 和 `curvature` 不应持续偏一侧。
- 左弯/右弯时 `track_sign` 方向稳定，不能连续左右翻。
- `track_confidence` 不能长期很低。
- 普通直道/弯道的 `track_source` 优先是 `bottom_connected` 或 `single_edge_completed`。
- `history_guarded` 和 `pure_visual_fallback` 只能短时出现，不能作为常态。
- `sign_flip_blocked` 不能高频触发；高频触发说明几何证据在左右跳。

如果静态不过，先不要调 PID：

- 画面过暗、过曝、线断：先回到 `exp_light = 65`，再小步改曝光。
- 算法明显只看近处、入弯前没有远场趋势：小步增 `see_max`。
- 远处噪声、亮块、场外线把主轨迹带跑：小步降 `see_max`。
- `track_confidence` 低且 `track_source` 常掉历史：先处理图像/几何证据，不要加转向增益。

建议步长：

- `exp_light` 每次改 `5..10`，改完必须重新看画面质量。
- `see_max` 每次改 `2..4`，先在 `18..32` 范围内找可用窗口。

### 4.3 低速直道：先调到“不自己找事”

动态第一轮只跑低速直道或很缓的 S 线，不跑急弯，不跑特殊场景。目的不是最快，而是确认车在几何偏差很小时不会自己放大误差。

观察字段：

- `lateral_error`
- `heading_error`
- `curvature`
- `raw_turn_output`
- `applied_turn_output`
- `gyro_z`
- `gyro_error`

通过标准：

- 直道能保持在赛道中间附近，不出现持续左右摆。
- `raw_turn_output` 不应长期打到 `raw_turn_output_limit`。
- `lateral_error` 过零后车身能收住，不连续反打。
- 小扰动后能回到中线附近，而不是越修越大。

调法：

- 直道左右摆：先降 `PID_TURN_CAMERA.P`，每次降 `5..10`。
- 修正太钝、车慢慢偏出中线：升 `PID_TURN_CAMERA.P`，每次升 `5`。
- 输出偶尔尖峰但车身没有真实需要：先确认几何字段是否跳变，再考虑降低 `raw_turn_output_limit`。
- 低速起步阶段方向过猛：先降 `motion_turn_limit_spinup`，不要改轮速 PID。

停止条件：

- 如果 `track_source` 本身在跳，停止调 P，回到静态证据检查。
- 如果 `raw_turn_output` 明显合理但车轮执行不跟手，转向链路先暂停，去查动力/轮速。

### 4.4 低速弯道：再调“进弯”和“回正”

直道不过不要进入这一轮。弯道调参只看普通弯，不要混入十字和环岛。

优先调 `PID_TURN_CAMERA.P`：

- 入弯慢、总是压线后才补方向：加 `PID_TURN_CAMERA.P`，每次 `+5`。
- 弯中能跟住但回正过冲：先减 `PID_TURN_CAMERA.P`，每次 `-5`。
- 直道刚好但弯道不够：优先小幅增加 `see_max`，再加 P。
- 左弯和右弯差异很大：先看几何证据和轮速执行，不要用单一 P 硬补。

再调 `PID_TURN_GYRO_CAMERA.P`：

- `w_target` 方向合理，但 `gyro_z` 跟随明显慢：加 `PID_TURN_GYRO_CAMERA.P`，每次 `+0.2..0.5`。
- 车头响应发冲、弯中抖动来自角速度跟随：降 `PID_TURN_GYRO_CAMERA.P`，每次 `-0.2`。
- `gyro_error` 长期同号但几何正常：可以少量加内环 P，不要开 I。

最后调 `PID_TURN_GYRO_CAMERA.D`：

- 车头角速度跟随有过冲：加 `PID_TURN_GYRO_CAMERA.D`，每次 `+0.05..0.2`。
- 加 D 后输出尖峰、执行抖：减 `PID_TURN_GYRO_CAMERA.D` 或先检查 IMU 噪声。
- `PID_TURN_GYRO_CAMERA.D` 只处理内环误差变化，不修正相机几何错误。

### 4.5 升速度：一次只升一个档

低速普通直道和普通弯道通过后，才提高 `Speed_base` 或实际目标速度。每次只升一个小档，保留上一档 evidence bundle。

升速后常见变化：

- 同样的 P 会显得更激进。
- 入弯提前量可能不够，需要略增 `see_max`。
- 回正过冲更明显，可能需要略降 `PID_TURN_CAMERA.P` 或略增 `PID_TURN_GYRO_CAMERA.D`。
- 输出更容易碰限，可能需要重新看 `raw_turn_output_limit`。

建议顺序：

1. 先升速度，不改 P。
2. 如果只是不够提前，先小步增 `see_max`。
3. 如果整体反应不足，再小步增 `PID_TURN_CAMERA.P`。
4. 如果速度越高越抖，先小步降 `PID_TURN_CAMERA.P`，再小步加 `PID_TURN_GYRO_CAMERA.D`。
5. 如果只有起步阶段猛打，调 `motion_turn_limit_spinup`，不要污染正常巡航参数。

### 4.6 收输出限幅和差速尺度

方向链路稳定后，再看执行器相关的转向参数。

`raw_turn_output_limit`：

- 作用：限制转向控制输出进入差速混合前的幅度。
- 太大：错误帧或过冲时容易给出过猛方向。
- 太小：急弯会顶限，车进不去弯。
- 调法：先看正常好帧中 `raw_turn_output` 的峰值，再把限幅设到“好帧峰值略上方”，不要靠拍脑袋压低。

`wheel_turn_target_scale`：

- 作用：把转向输出换成左右轮目标差的尺度。
- 太大：转向时一侧轮目标变化过大，车可能拖速、甩头。
- 太小：`raw_turn_output` 已经足够，但实际车头不转。
- 调法：只有在 `raw_turn_output/applied_turn_output` 看起来合理，而车身执行明显过强或过弱时才改。

`motion_turn_limit_spinup`：

- 作用：起步阶段临时限制转向。
- 太大：起步刚解锁就可能猛打。
- 太小：起步需要带方向时会反应慢。
- 调法：只处理起步前几百毫秒问题，不用它修正常巡航。

## 5. 后续微调：按症状改

### 5.1 直道左右摆

先判断摆动来源：

- `lateral_error / heading_error / curvature` 本身左右跳：几何问题，回到 `exp_light / see_max / track_source`。
- 几何稳定但 `raw_turn_output` 左右大幅反打：控制增益过高。
- `raw_turn_output` 平顺但车身左右抖：执行/轮速问题，不在本节解决。

优先动作：

- 降 `PID_TURN_CAMERA.P`，每次 `-5..-10`。
- 若内环已加 D 且 IMU 噪声明显，降 `PID_TURN_GYRO_CAMERA.D`。
- 若只在起步阶段摆，降 `motion_turn_limit_spinup`。

不要先动：

- `SCENE_WIDE_CLASSIFIER`
- `PID_TURN_CAMERA.D`
- `LEFT_WHEEL_PID / RIGHT_WHEEL_PID`

### 5.2 入弯慢，弯心前补一大把方向

优先动作：

- 确认弯道前 `see_max` 覆盖到了足够远的线。
- 增 `PID_TURN_CAMERA.P`，每次 `+5`。
- 若 `w_target` 合理但 `gyro_z` 跟随慢，增 `PID_TURN_GYRO_CAMERA.P`。
- 若急弯总是顶限，检查 `raw_turn_output_limit` 是否太低。

不要用 `PID_TURN_GYRO_CAMERA.I` 补入弯慢。入弯慢通常是几何提前量、外环 P、内环 P 或限幅问题。

### 5.3 回正过冲

优先动作：

- 降 `PID_TURN_CAMERA.P`，每次 `-5`。
- 增 `PID_TURN_GYRO_CAMERA.D`，每次 `+0.05..0.2`。
- 如果过冲只发生在升速后，先退一档速度确认低速基线仍然成立。

补充判断：

- 如果 `track_sign` 在弯道出口来回翻，先修几何连续性。
- 如果 `sign_flip_blocked` 高频触发，说明系统在防止反打，不要继续加 P。

### 5.4 弯中贴内线或贴外线

贴内线：

- 可能是 `PID_TURN_CAMERA.P` 太大。
- 可能是 `see_max` 过大，看到了不该提前处理的远处结构。
- 可能是普通弯被误判为 `circle_entry`。

贴外线：

- 可能是 `PID_TURN_CAMERA.P` 太小。
- 可能是 `see_max` 太小，入弯提前量不足。
- 可能是 `raw_turn_output_limit` 太低。

处理顺序：

1. 先看 `track_source / track_sign / scene` 是否正确。
2. 再看 `see_max` 是否给了正确提前量。
3. 再改 `PID_TURN_CAMERA.P`。
4. 最后才改 `raw_turn_output_limit` 或 `wheel_turn_target_scale`。

### 5.5 输出经常打满

先区分两类情况：

- 好帧急弯打满且车仍进不去：限幅可能太低，或差速尺度太小。
- 普通直道/缓弯打满：不要放大限幅，先查几何跳变和 P 过高。

优先动作：

- 看 `raw_turn_output` 是否在正常场景长期贴 `raw_turn_output_limit`。
- 如果贴限来自错误帧，修 `exp_light / see_max / track_source`。
- 如果贴限来自合理急弯，适当升 `raw_turn_output_limit` 或 `wheel_turn_target_scale`。

### 5.6 画面和板端判断对不上

优先检查：

- `control_snapshot_emit_interval_ms`
- `steering_media_publish_interval_ms`
- `assistant_tcp.host`
- `steering_media_enabled`
- evidence bundle 中帧号和时间戳是否对齐

这是证据链问题，不是转向参数问题。证据没对齐时，不要继续调控制参数。

## 6. 特殊场景参数调法

`SCENE_WIDE_CLASSIFIER` 只服务 `special_wide / circle_entry / cross` 的严格图像证据链。普通 `straight / bend` 主参考线不顺时，先不要动这里。

### 6.1 十字

十字抓不住：

- 降 `CROSS_UPPER_FULL_SPAN_CONSEC_ROWS_MIN`。
- 降 `CROSS_UPPER_FULL_SPAN_MIN_RATIO`。
- 必要时降 `TO_CROSS_MARGIN`。

普通宽场景误进 `cross`：

- 升 `CROSS_UPPER_FULL_SPAN_CONSEC_ROWS_MIN`。
- 升 `CROSS_UPPER_FULL_SPAN_MIN_RATIO`。
- 升 `TO_CROSS_MARGIN`。

### 6.2 环岛入口

真环岛入口抓不住：

- 降 `EDGE_MOTION_MIN_PX`。
- 降 `EDGE_CURVATURE_MIN_PX`。
- 升 `CIRCLE_CURVE_WEIGHT`。
- 升 `CIRCLE_OPPOSITE_STRAIGHT_WEIGHT`。

普通弯道误判成环岛：

- 升 `EDGE_CURVATURE_MIN_PX`。
- 降 `OPPOSITE_EDGE_STRAIGHT_MAX_CURVATURE_PX`。
- 降 `OPPOSITE_EDGE_BORDER_TOUCH_MAX_RATIO`。
- 升 `TO_CIRCLE_MARGIN`。

原则：

- 先调门槛，再调权重。
- 先收紧“对侧必须够直”，再收紧落判 margin。
- 不要直接把 `SPECIAL_WIDE_LOWER_WIDTH_MIN_RATIO` 放得很宽；它通常会先增加普通弯道误判。

### 6.3 状态机节奏

只在分类分数已经对，但状态进出抖动时才动：

- `ENTER_CONFIRM_CYCLES`：进场景确认帧数。
- `EXIT_CONFIRM_CYCLES`：出场景确认帧数。

调法：

- 误进场景：优先升 `ENTER_CONFIRM_CYCLES`。
- 退出太慢：优先降 `EXIT_CONFIRM_CYCLES`。
- 分类本身错了：不要先改确认帧数，先回到门槛和权重。

## 7. 现场记录和回滚

每轮调参至少记录：

- 修改前 `default_params.json`。
- 修改后 `default_params.json`。
- 测试场景和速度档。
- evidence bundle 路径。
- 结论：通过、失败、失败症状、下一步假设。

建议文件命名：

```bash
ts=$(rtk date -u +%Y%m%dT%H%M%SZ)
rtk mkdir -p new/verification/params
rtk cp new/config/default_params.json "new/verification/params/default_params-${ts}.json"
```

现场规则：

- 一次只动一组参数。
- 一次只验证一个假设。
- 调普通转向时不要混入十字/环岛。
- 调特殊场景时不要同时改外环 P。
- 改 `P_Mode`、`exp_light` 前先准备回滚版本。
- evidence 没采完整，不要把体感结论写成最终结论。

## 8. 最小可执行攻略

只想从零开始快速跑通时，按这个顺序：

1. 固定 `P_Mode=3`、`exp_light=65`、`USE_FUZZY=0`、`PID_TURN_GYRO_CAMERA.I=0.0`。
2. 静态采图，先让 `track_confidence / track_source / track_sign` 稳定。
3. 用 `see_max` 找到能提前看弯、又不被远处噪声带跑的窗口。
4. 低速直道调 `PID_TURN_CAMERA.P`，目标是不左右摆。
5. 低速普通弯继续调 `PID_TURN_CAMERA.P`，目标是能进弯、能回正。
6. 用 `PID_TURN_GYRO_CAMERA.P` 修角速度跟随，用 `PID_TURN_GYRO_CAMERA.D` 抑制内环过冲。
7. 升速度，每升一档只做小步修正。
8. 最后收 `raw_turn_output_limit / wheel_turn_target_scale / motion_turn_limit_spinup`。
9. 普通转向稳定后，再单独调 `SCENE_WIDE_CLASSIFIER`。

一句话判断：

- 看不清或看错：`exp_light / see_max`
- 几何跳：先修 `track_source / track_sign / track_confidence`
- 反应慢：`PID_TURN_CAMERA.P` 或 `PID_TURN_GYRO_CAMERA.P`
- 回正过冲：降 `PID_TURN_CAMERA.P` 或加 `PID_TURN_GYRO_CAMERA.D`
- 输出过猛：先查几何，再收 `raw_turn_output_limit`
- 特殊场景误判：最后调 `SCENE_WIDE_CLASSIFIER`
