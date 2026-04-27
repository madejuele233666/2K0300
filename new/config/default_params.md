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

- `Speed_base = 100.0`
  - 默认基准速度。动态调参和正式跑车都会围绕它展开。
- `see_max = 100.0`
  - 视觉向上扫描上限。值越小，越偏近场；值越大，越看远处行。
- `P_Mode = 3`
  - 启动关键字段，相机/旧链路兼容模式之一。非必要不要改。
- `exp_light = 65`
  - 启动关键字段，当前推荐基线曝光。

### 2.2 外环相机转向 PID

- `PID_TURN_CAMERA.USE_FUZZY = 0`
  - 兼容保留字段；BEV 预瞄曲率主链不再用它缩放几何命令。
- `PID_TURN_CAMERA.P = 3000.0`
  - 兼容/诊断字段；BEV 主增益已迁移到 `BEV_CONTROL_MODEL.CURVATURE_TO_W_TARGET_GAIN`。
- `PID_TURN_CAMERA.P_SCALE = 1.0`
  - 兼容保留字段；主链不读取。
- `PID_TURN_CAMERA.D = 0.0`
  - 当前仅为兼容旧参数文件保留，不参与 `LegacyPidControl::ComputeTurnTarget()` 的相机外环输出。
  - 缺省即可；建议固定为 `0.0`。若配置成非零，运行时会提示该值已忽略。
  - 现场不要把它当成主要抑摆旋钮；要抑制摆动，优先看 BEV 几何证据、`CURVATURE_TO_W_TARGET_GAIN`、`PID_TURN_GYRO_CAMERA.D` 和 `raw_turn_output_limit`。

### 2.3 内环陀螺转向 PID

- `PID_TURN_GYRO_CAMERA.P = 0.5`
  - 陀螺内环比例。
- `PID_TURN_GYRO_CAMERA.I = 0.0`
  - 陀螺内环积分。默认关闭。
- `PID_TURN_GYRO_CAMERA.D = 0.0`
  - 陀螺内环微分。当前必填。

### 2.4 轮速 PID

- `LEFT_WHEEL_PID.{P,I,D} = 84.0 / 2.4 / 0.75`
- `RIGHT_WHEEL_PID.{P,I,D} = 96.0 / 2.2 / 0.2`
- `INTEGRAL_LIMIT = 5000.0`
  - 左右轮积分上限。
- `MEASUREMENT_FILTER_ALPHA = 0.4`
  - 轮速测量低通。

这组参数主要影响“速度跟随稳定性”，不是场景识别问题的首选调节面。

### 2.5 执行器与运动约束

- `pwm_limit = 5000`
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
- `wheel_turn_target_scale = 100.0`

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

当前普通 `straight / bend` 主几何已经走 BEV sparse geometry + reference path 链路，底部连通追踪字段不再作为运行时或协议真相。也就是说：

- `default_params.json` 里控制主参数集中在 `BEV_PROJECTOR / BEV_GEOMETRY / BEV_TOPOLOGY_SAMPLER / BEV_CORRIDOR_GRAPH / BEV_TOPOLOGY_EVIDENCE / BEV_REFERENCE_POLICY / BEV_CONTROL_MODEL`。`BEV_SCENE_FSM` 仍保留为兼容配置面，但不应继续作为 topology formal authority。
- `near_lateral_error / far_heading_error / preview_curvature` 仅是过渡期 BEV 调试派生量，不再参与控制混合，后续稳定后继续清除。
- 普通弯道参考线不对时，优先排查 BEV projector、BEV geometry 和 reference path 证据，不是先改 `circle / cross` 门槛。

看 `control.steering_snapshot` 或 steering-media 元数据时，普通弯道现在优先关注这些字段：

- `track_confidence`
  - 主轨迹整体置信度。低了先怀疑图像可见性、单边补线占比或历史守护。
- `lookahead_distance_m / lookahead_lateral_error / lookahead_heading_error`
  - BEV reference path 的预瞄控制证据。
- `reference_curvature / curvature_command`
  - 当前控制主几何；`curvature_command` 是进入 PID 的唯一 BEV 几何命令。
- `near_lateral_error / far_heading_error / preview_curvature`
  - 仅供调试对照，不参与新控制混合，后续稳定后继续清除。
- `sign_flip_blocked`
  - 方向翻转保护是否介入。它为 `true` 时，说明系统在阻止“突然反打到另一侧”。
- `gyro_heading_delta_deg / gyro_consistency_score`
  - 陀螺短时连续性约束的观测量和一致性评分。它们只参与降权和守护，不单独造轨迹。
- `imu_grace_active`
  - IMU 短时失效宽限是否生效。为 `true` 时，本帧属于纯视觉保守运行窗口。
- `phase / raw_turn_output / applied_turn_output`
  - 静态板端测试里若是 `DISARMED`，看到 `raw_turn_output=0` 和 `applied_turn_output=0` 是正常现象，不代表几何链路失效。

### 2.7 图像几何

- `camera_frame_width = 320`
- `camera_frame_height = 240`

这是当前算法假设的编译期兼容分辨率。除非同步改适配层和验证，否则不要改。

### 2.8 `BEV_TOPOLOGY_*`

`bev-corridor-topology-perception` 引入四组 topology 参数。它们是新的正式几何/拓扑权威面，必须随 `config_snapshot` 一起保留。

- `BEV_TOPOLOGY_SAMPLER`
  - `FORWARD_SAMPLES_M`
    - 稀疏 BEV 前向采样距离。当前覆盖 `0.30m` 到 `4.50m`，共 12 层。
  - `LATERAL_MIN_M / LATERAL_MAX_M / LATERAL_STEP_M`
    - 每个前向层的横向采样范围和步长。invalid outside image 不能计为 opening。
  - `SAMPLE_PATCH_RADIUS_PX`
    - 采样原图 patch 半径，只影响 sample confidence，不产生 scene 语义。
  - `DRIVABLE_CONFIDENCE_MIN / UNKNOWN_CONFIDENCE_MIN`
    - 四类样本分类阈值。低于 unknown 门槛的是 unknown，不能当 background；投影出图的是 invalid，不能当 opening。
- `BEV_CORRIDOR_GRAPH`
  - `NOMINAL_LANE_WIDTH_M`
    - 默认复用 `BEV_GEOMETRY.NOMINAL_LANE_WIDTH_M`。
  - `MIN_INTERVAL_WIDTH_M / MAX_INTERVAL_WIDTH_M`
    - corridor interval 接受宽度范围。
  - `MAX_CENTER_JUMP_M / MAX_WIDTH_CHANGE_M / MAX_CURVATURE_ABS`
    - ordinary chain DAG/DP 的连续性约束。
  - `PRIOR_CARRY_CONFIDENCE_SCALE`
    - 历史 carry 只能降置信使用，不能制造高置信 geometry。
- `BEV_TOPOLOGY_EVIDENCE`
  - `*_ENTER_SCORE / *_RELEASE_SCORE`
    - cross/circle/zebra 进入和释放 hysteresis 分数。
  - `ORDINARY_RELEASE_SCORE`
    - 回 ordinary 的最低拓扑分数。
  - `EVIDENCE_DECAY`
    - evidence accumulator 的衰减。
- `BEV_REFERENCE_POLICY`
  - `HOLD_LAST_MAX_CYCLES`
    - cross/zebra/lost 可保持上一次 reference 的最大周期。
  - `BLEND_MIN_CYCLES`
    - 出环/重捕获 blend 的最小周期。
  - `ARC_FOLLOW_CONFIDENCE_MIN / STABLE_BOUNDARY_CONFIDENCE_MIN`
    - circle arc-follow 和 stable-boundary-offset 的最低置信度。

调参顺序固定为：先看 sparse samples 是否分类正确，再看 intervals/graph ordinary chain，最后才调 evidence/FSM/reference。不要用 `width_expand_ratio / open_score / bottom_transition_density` 重新建立场景真相。

### 2.9 `BEV_SCENE_FSM`

特殊场景现在只由 BEV sparse geometry 生成候选，再由 FSM 管理 candidate / confirm / progress / release。旧 `SCENE_WIDE_CLASSIFIER`、`CIRCLE_ENTRY`、`CIRCLE_EXIT`、`CIRCLE_FALLBACK` 像素参数已经从正式参数面删除。

边界先说清楚：

- 十字、环岛、斑马、普通弯都只消费 `BEVSceneObservation` 和 `VehicleContext`。
- FSM 只决定 `active_module / scene_phase`，不直接生成电机控制量。
- reference policy 只根据 FSM state 和 BEV track 选择 `centerline / inner_offset / blend / hold_last` reference path。
- PID 只消费 `curvature_command`，不读场景参数。

当前字段：

- `BEND_SEVERITY_CONFIRM`
  - 普通弯 veto 门槛，来自 BEV heading / curvature / lateral 综合严重度。
- `CROSS_EXPAND_RATIO_MIN`
  - 十字候选的 BEV 可行驶宽度扩张比例门槛。
- `CROSS_BILATERAL_OPEN_MIN_M`
  - 十字候选左右同时开口的最小 BEV 米制证据。
- `CROSS_CONFIRM_CYCLES / CROSS_HOLD_CYCLES`
  - 十字确认与保持周期。
- `ZEBRA_TRANSITION_DENSITY_MIN / ZEBRA_HOLD_CYCLES`
  - 斑马线候选与保持周期；这是唯一仍用原始图像行 transition 的场景派生量。
- `CIRCLE_OPEN_SCORE_MIN`
  - 环岛入口单侧开口 BEV 米制证据门槛。
- `CIRCLE_CONTRACT_SCORE_MIN`
  - 预留的收缩证据门槛，当前不作为主判据。
- `CIRCLE_OPPOSITE_HEADING_ABS_MAX`
  - 环岛入口对侧边界近似直线的最大 BEV heading。
- `CIRCLE_CONFIRM_CYCLES / CIRCLE_RELEASE_CYCLES`
  - 环岛确认、内部保持、退出释放周期。
- `RELEASE_TRACK_CONFIDENCE_MIN`
  - 环岛退出时回普通轨迹所需的最小 BEV track 置信度。

现场调参顺序建议固定：

1. 先看 `width_expand_ratio / cross_bilateral_open_score_m / left_open_score / right_open_score` 是否符合画面。
2. 如果 BEV observation 错，先修 `BEV_PROJECTOR / BEV_GEOMETRY`。
3. observation 对但确认抖动时，再改 `*_CONFIRM_CYCLES / *_RELEASE_CYCLES`。
4. reference path 不合理时看 `reference_mode` 和 BEV 边界，不新增像素补线参数。

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
- `see_max = 100.0`
- `Speed_base = 100.0`
- `PID_TURN_CAMERA.USE_FUZZY = 0`
- `PID_TURN_CAMERA.P = 3000.0`（兼容/诊断，不再缩放 BEV 主链）
- `PID_TURN_CAMERA.P_SCALE = 1.0`（兼容/诊断，不再缩放 BEV 主链）
- `PID_TURN_CAMERA.D = 0.0`
- `PID_TURN_GYRO_CAMERA.P = 0.5`
- `PID_TURN_GYRO_CAMERA.I = 0.0`
- `PID_TURN_GYRO_CAMERA.D = 0.0`
- `raw_turn_output_limit = 8000`
- `motion_turn_limit_spinup = 0.35`
- `wheel_turn_target_scale = 100.0`
基线原则：

- `PID_TURN_CAMERA.D` 当前是兼容保留键，不是有效控制旋钮；保持 `0.0` 即可。
- `PID_TURN_GYRO_CAMERA.I` 从零开始必须保持 `0.0`，不要用积分补几何偏差。
- `USE_FUZZY` 从零开始保持 `0`，不要让旧 fuzzy 外环介入 BEV 主链。
- BEV 回正力度通过 `BEV_CONTROL_MODEL.CURVATURE_TO_W_TARGET_GAIN` 调整；`PID_TURN_CAMERA.P` 仅作兼容/诊断。
- `raw_turn_output_limit` 先保持宽松，等方向链路调顺后再收限幅。
- `Speed_base` 是外环速度归一化基准，不是轮速 PID；从零调方向时先不要频繁改它。

### 4.2 先做静态证据检查

先不开电机，采直道、左弯、右弯、十字、环岛入口的静态帧。重点看 `control.steering_snapshot` 和 steering-media 元数据。

静态通过标准：

- 直道时 `curvature_command` 接近 0，`lookahead_lateral_error` 和 `lookahead_heading_error` 不应持续偏一侧。
- 左弯/右弯时 `curvature_command` 方向稳定，不能连续左右翻。
- `track_confidence` 不能长期很低。
- `sign_flip_blocked` 不能高频触发；高频触发说明几何证据在左右跳。

如果静态不过，先不要调 PID：

- 画面过暗、过曝、线断：先回到 `exp_light = 65`，再小步改曝光。
- 算法明显只看近处、入弯前没有远场趋势：小步增 `see_max`。
- 远处噪声、亮块、场外线把主轨迹带跑：小步降 `see_max`。
- `track_confidence` 低或 `visible_range_m` 过短：先处理图像/BEV 几何证据，不要加转向增益。

建议步长：

- `exp_light` 每次改 `5..10`，改完必须重新看画面质量。
- `see_max` 每次改 `2..4`，先在 `18..32` 范围内找可用窗口。

### 4.3 低速直道：先调到“不自己找事”

动态第一轮只跑低速直道或很缓的 S 线，不跑急弯，不跑特殊场景。目的不是最快，而是确认车在几何偏差很小时不会自己放大误差。

观察字段：

- `lateral_error`
- `heading_error`
- `curvature`
- `curvature_command`
- `lookahead_distance_m`
- `raw_turn_output`
- `applied_turn_output`
- `gyro_z`
- `gyro_error`

通过标准：

- 直道能保持在赛道中间附近，不出现持续左右摆。
- `raw_turn_output` 不应长期打到 `raw_turn_output_limit`。
- `curvature_command` 过零后车身能收住，不连续反打。
- 小扰动后能回到中线附近，而不是越修越大。

调法：

- 直道左右摆：先降 `CURVATURE_TO_W_TARGET_GAIN` 或 `CURVATURE_COMMAND_LIMIT`，小步改。
- 修正太钝、车慢慢偏出中线：升 `CURVATURE_TO_W_TARGET_GAIN`，小步改。
- 输出偶尔尖峰但车身没有真实需要：先确认几何字段是否跳变，再考虑降低 `raw_turn_output_limit`。
- 低速起步阶段方向过猛：先降 `motion_turn_limit_spinup`，不要改轮速 PID。

停止条件：

- 如果 BEV reference path 或 `curvature_command` 本身在跳，停止调控制增益，回到静态证据检查。
- 如果 `raw_turn_output` 明显合理但车轮执行不跟手，转向链路先暂停，去查动力/轮速。

### 4.4 低速弯道：再调“进弯”和“回正”

直道不过不要进入这一轮。弯道调参只看普通弯，不要混入十字和环岛。

优先调 `BEV_CONTROL_MODEL`：

- 入弯慢、总是压线后才补方向：先确认 `lookahead_distance_m`，再加 `CURVATURE_TO_W_TARGET_GAIN`。
- 弯中能跟住但回正过冲：先减 `CURVATURE_TO_W_TARGET_GAIN` 或 `CURVATURE_FEEDFORWARD_GAIN`。
- 直道刚好但弯道不够：优先检查 lookahead 与 reference curvature，再动增益。
- 左弯和右弯差异很大：先看 BEV 几何证据和轮速执行，不要用单一增益硬补。

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

- 同样的 `CURVATURE_TO_W_TARGET_GAIN` 会显得更激进。
- 入弯提前量可能不够，需要略增 `see_max`。
- 回正过冲更明显，可能需要略降 `CURVATURE_TO_W_TARGET_GAIN` 或略增 `PID_TURN_GYRO_CAMERA.D`。
- 输出更容易碰限，可能需要重新看 `raw_turn_output_limit`。

建议顺序：

1. 先升速度，不改 BEV 控制参数。
2. 如果只是不够提前，先小步增 `see_max`。
3. 如果整体反应不足，再小步增 `CURVATURE_TO_W_TARGET_GAIN`。
4. 如果速度越高越抖，先小步降 `CURVATURE_TO_W_TARGET_GAIN`，再小步加 `PID_TURN_GYRO_CAMERA.D`。
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

- `curvature_command / lookahead_lateral_error / lookahead_heading_error` 本身左右跳：几何问题，回到 BEV projector/geometry 证据。
- 几何稳定但 `raw_turn_output` 左右大幅反打：控制增益过高。
- `raw_turn_output` 平顺但车身左右抖：执行/轮速问题，不在本节解决。

优先动作：

- 降 `CURVATURE_TO_W_TARGET_GAIN` 或 `CURVATURE_COMMAND_LIMIT`。
- 若内环已加 D 且 IMU 噪声明显，降 `PID_TURN_GYRO_CAMERA.D`。
- 若只在起步阶段摆，降 `motion_turn_limit_spinup`。

不要先动：

- `BEV_SCENE_FSM`
- `PID_TURN_CAMERA.D`
- `LEFT_WHEEL_PID / RIGHT_WHEEL_PID`

### 5.2 入弯慢，弯心前补一大把方向

优先动作：

- 确认弯道前 `see_max` 覆盖到了足够远的线。
- 增 `CURVATURE_TO_W_TARGET_GAIN`。
- 若 `w_target` 合理但 `gyro_z` 跟随慢，增 `PID_TURN_GYRO_CAMERA.P`。
- 若急弯总是顶限，检查 `raw_turn_output_limit` 是否太低。

不要用 `PID_TURN_GYRO_CAMERA.I` 补入弯慢。入弯慢通常是几何提前量、外环 P、内环 P 或限幅问题。

### 5.3 回正过冲

优先动作：

- 降 `CURVATURE_TO_W_TARGET_GAIN` 或 `CURVATURE_FEEDFORWARD_GAIN`。
- 增 `PID_TURN_GYRO_CAMERA.D`，每次 `+0.05..0.2`。
- 如果过冲只发生在升速后，先退一档速度确认低速基线仍然成立。

补充判断：

- 如果 `curvature_command` 在弯道出口来回翻，先修 BEV 几何连续性。
- 如果 `sign_flip_blocked` 高频触发，说明系统在防止反打，不要继续加 P。

### 5.4 弯中贴内线或贴外线

贴内线：

- 可能是 `CURVATURE_TO_W_TARGET_GAIN` 或 `CURVATURE_FEEDFORWARD_GAIN` 太大。
- 可能是 `see_max` 过大，看到了不该提前处理的远处结构。
- 可能是普通弯被误判为 `circle_entry`。

贴外线：

- 可能是 `CURVATURE_TO_W_TARGET_GAIN` 太小。
- 可能是 `see_max` 太小，入弯提前量不足。
- 可能是 `raw_turn_output_limit` 太低。

处理顺序：

1. 先看 `reference_mode / scene / curvature_command` 是否正确。
2. 再看 `visible_range_m` 和 `lookahead_distance_m` 是否给了正确提前量。
3. 再改 `BEV_CONTROL_MODEL` 的预瞄曲率参数。
4. 最后才改 `raw_turn_output_limit` 或 `wheel_turn_target_scale`。

### 5.5 输出经常打满

先区分两类情况：

- 好帧急弯打满且车仍进不去：限幅可能太低，或差速尺度太小。
- 普通直道/缓弯打满：不要放大限幅，先查几何跳变和 BEV 控制增益过高。

优先动作：

- 看 `raw_turn_output` 是否在正常场景长期贴 `raw_turn_output_limit`。
- 如果贴限来自错误帧，修 BEV geometry/reference path 证据。
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

特殊场景只调 `BEV_SCENE_FSM`。旧像素场景参数已经删除；不要把环岛、十字问题重新引回 `highest_line / steering_reference_col / LaneMetrics`。

### 6.1 十字

十字抓不住：

- 确认 `width_expand_ratio` 和 `cross_bilateral_open_score_m` 在入口帧确实升高。
- 证据不足时先修 BEV geometry，不先改 FSM。
- 证据稳定但不确认时，降 `CROSS_EXPAND_RATIO_MIN` 或 `CROSS_BILATERAL_OPEN_MIN_M`。
- 确认太慢时，降 `CROSS_CONFIRM_CYCLES`。

普通宽场景误进 `cross`：

- 升 `CROSS_EXPAND_RATIO_MIN`。
- 升 `CROSS_BILATERAL_OPEN_MIN_M`。
- 需要更稳时升 `CROSS_CONFIRM_CYCLES`。

### 6.2 环岛入口

真环岛入口抓不住：

- 确认 `left_open_score / right_open_score` 和对侧 `*_boundary_heading_abs_rad` 是否符合画面。
- 证据不足时先修 BEV geometry 的边界采样和单边重建。
- 证据稳定但不确认时，降 `CIRCLE_OPEN_SCORE_MIN` 或适当放宽 `CIRCLE_OPPOSITE_HEADING_ABS_MAX`。
- 确认太慢时，降 `CIRCLE_CONFIRM_CYCLES`。

普通弯道误判成环岛：

- 升 `CIRCLE_OPEN_SCORE_MIN`。
- 收紧 `CIRCLE_OPPOSITE_HEADING_ABS_MAX`。
- 需要更稳时升 `CIRCLE_CONFIRM_CYCLES`。

原则：

- 先确认 BEV observation，再调 FSM 门槛。
- 先收紧“对侧必须够直”，再调确认周期。
- 不新增像素补线或宽场景评分参数。

### 6.3 状态机节奏

只在分类分数已经对，但状态进出抖动时才动：

- `CROSS_CONFIRM_CYCLES / CROSS_HOLD_CYCLES`：十字确认和保持帧数。
- `CIRCLE_CONFIRM_CYCLES / CIRCLE_RELEASE_CYCLES`：环岛确认和释放帧数。
- `ZEBRA_HOLD_CYCLES`：斑马线保持帧数。

调法：

- 误进场景：优先升对应 `*_CONFIRM_CYCLES`。
- 退出太慢：优先降对应 hold / release cycles。
- 分类本身错了：不要先改确认帧数，先回到 BEV observation 和门槛。

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
2. 静态采图，先让 `track_confidence / visible_range_m / curvature_command` 稳定。
3. 用 `see_max` 找到能提前看弯、又不被远处噪声带跑的窗口。
4. 低速直道调 `CURVATURE_TO_W_TARGET_GAIN`，目标是不左右摆。
5. 低速普通弯继续调 BEV lookahead/curvature 参数，目标是能进弯、能回正。
6. 用 `PID_TURN_GYRO_CAMERA.P` 修角速度跟随，用 `PID_TURN_GYRO_CAMERA.D` 抑制内环过冲。
7. 升速度，每升一档只做小步修正。
8. 最后收 `raw_turn_output_limit / wheel_turn_target_scale / motion_turn_limit_spinup`。
9. 普通转向稳定后，再单独调 `BEV_SCENE_FSM`。

一句话判断：

- 看不清或看错：`exp_light / see_max`
- 几何跳：先修 `curvature_command / reference_mode / track_confidence`
- 反应慢：`CURVATURE_TO_W_TARGET_GAIN` 或 `PID_TURN_GYRO_CAMERA.P`
- 回正过冲：降 `CURVATURE_TO_W_TARGET_GAIN` 或加 `PID_TURN_GYRO_CAMERA.D`
- 输出过猛：先查几何，再收 `raw_turn_output_limit`
- 特殊场景误判：先查 BEV observation，最后调 `BEV_SCENE_FSM`
