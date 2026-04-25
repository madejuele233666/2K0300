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
- `PID_TURN_CAMERA.D`
- `PID_TURN_GYRO_CAMERA.D`
- `P_Mode`
- `exp_light`
- `LEFT_WHEEL_PID.{P,I,D,INTEGRAL_LIMIT}`
- `RIGHT_WHEEL_PID.{P,I,D,INTEGRAL_LIMIT}`
- `assistant_tcp.{host,port}`

其余键按“可选覆盖”处理；缺省时使用 `RuntimeParameters` 里的内建值。

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
- `PID_TURN_CAMERA.P = 58.0`
  - 相机外环主比例项。决定看见偏差后“先打多少方向”。
- `PID_TURN_CAMERA.P_SCALE = 1.0`
  - 外环 P 的额外缩放。
- `PID_TURN_CAMERA.D = 10.0`
  - 相机外环微分项。主要用来抑制摆动和回正过冲。

### 2.3 内环陀螺转向 PID

- `PID_TURN_GYRO_CAMERA.P = 1.0`
  - 陀螺内环比例。
- `PID_TURN_GYRO_CAMERA.I = 0.0`
  - 陀螺内环积分。默认关闭。
- `PID_TURN_GYRO_CAMERA.D = 0.2`
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

### 2.7 图像几何

- `camera_frame_width = 320`
- `camera_frame_height = 240`

这是当前算法假设的编译期兼容分辨率。除非同步改适配层和验证，否则不要改。

### 2.8 `SCENE_WIDE_CLASSIFIER`

这组是当前 `special_wide / circle_entry / cross` 判别的核心参数。

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

## 4. 推荐调参顺序

### 4.1 先保证“能看清”

优先确认：

- `exp_light`
- `see_max`
- `P_Mode`

典型症状与动作：

- 画面太黑、线断、远处信息少：优先看 `exp_light`
- 画面能看见，但算法只看近处：调 `see_max`
- 改了曝光后相机链异常：先退回 `exp_light = 65`

### 4.2 再调外环，再调内环

先动：

- `PID_TURN_CAMERA.P`
- `PID_TURN_CAMERA.D`

再动：

- `PID_TURN_GYRO_CAMERA.P`
- `PID_TURN_GYRO_CAMERA.D`

经验规则：

- 入弯反应慢、总是压线后才纠正：先小步加 `PID_TURN_CAMERA.P`
- 左右来回摆、回正过冲：先小步加 `PID_TURN_CAMERA.D`
- 外环已经不算慢，但方向执行发飘、车头跟随差：再看陀螺内环
- `PID_TURN_GYRO_CAMERA.I` 默认保持 `0.0`，除非你确认存在稳定静差

### 4.3 动力问题单独处理

只有在出现下面问题时，才进入轮速/执行器参数面：

- 起步抖
- 同速左右轮不一致
- 给了转向后车速被拖垮
- 高频振荡来自速度环而不是视觉环

对应优先级：

1. `LEFT_WHEEL_PID / RIGHT_WHEEL_PID`
2. `wheel_turn_target_scale`
3. `pwm_limit / motion_pwm_step_limit`

## 5. 按症状调参数

### 5.1 普通直道/弯道都在左右摆

优先尝试：

- 增 `PID_TURN_CAMERA.D`
- 若已经很大仍抖，再略减 `PID_TURN_CAMERA.P`

不要先动：

- `SCENE_WIDE_CLASSIFIER`
- 轮速 PID

### 5.2 入弯慢，弯心前总要补一大把方向

优先尝试：

- 增 `PID_TURN_CAMERA.P`
- 仍不够时，少量增 `PID_TURN_GYRO_CAMERA.P`

### 5.3 十字误判成环岛，或环岛误判成十字

优先尝试 `SCENE_WIDE_CLASSIFIER`：

- 十字抓不住：
  - 降 `CROSS_UPPER_FULL_SPAN_CONSEC_ROWS_MIN`
  - 降 `CROSS_UPPER_FULL_SPAN_MIN_RATIO`
- 普通宽场景误进 `cross`：
  - 升 `CROSS_UPPER_FULL_SPAN_CONSEC_ROWS_MIN`
  - 升 `TO_CROSS_MARGIN`

### 5.4 普通弯道误判成环岛

优先尝试：

- 升 `EDGE_CURVATURE_MIN_PX`
- 降 `OPPOSITE_EDGE_STRAIGHT_MAX_CURVATURE_PX`
- 降 `OPPOSITE_EDGE_BORDER_TOUCH_MAX_RATIO`
- 必要时升 `TO_CIRCLE_MARGIN`

原则：

- 先收紧“对侧必须够直”的条件；
- 再收紧落判 margin；
- 最后才动 `CIRCLE_WEIGHT_OPEN/CONTRACT` 这类辅证权重。

### 5.5 真环岛入口抓不住

优先尝试：

- 降 `EDGE_MOTION_MIN_PX`
- 降 `EDGE_CURVATURE_MIN_PX`
- 升 `CIRCLE_CURVE_WEIGHT`
- 升 `CIRCLE_OPPOSITE_STRAIGHT_WEIGHT`

不要先做的事：

- 直接把 `SPECIAL_WIDE_LOWER_WIDTH_MIN_RATIO` 放得很宽

因为那样通常先带来更多普通弯道误判。

### 5.6 画面和板端判断对不上

优先检查：

- `control_snapshot_emit_interval_ms`
- `steering_media_publish_interval_ms`
- `assistant_tcp.host`
- `steering_media_enabled`

这是证据链问题，不是算法参数问题。

## 6. `SCENE_WIDE_CLASSIFIER` 实战攻略

建议固定顺序：

1. 先用 9 张静态基线看分类是否分开。
2. 先调门槛：
   - `CROSS_*`
   - `EDGE_*`
   - `OPPOSITE_*`
   - `TO_*_MARGIN`
3. 再调权重：
   - `CROSS_WEIGHT_*`
   - `CIRCLE_*_WEIGHT`
4. 最后才调状态机节奏：
   - `ENTER_CONFIRM_CYCLES`
   - `EXIT_CONFIRM_CYCLES`

推荐的排错顺序：

- `cross` 抖动：先看横带门槛，再看 `ENTER_CONFIRM_CYCLES`
- `circle_entry` 误判：先看对侧直线门槛，再看 `TO_CIRCLE_MARGIN`
- `special_wide` 进得太频繁：先收紧 `SPECIAL_WIDE_LOWER_WIDTH_MIN_RATIO` 和 `SPECIAL_WIDE_VALID_ROWS_MIN`

## 7. 现场操作建议

- 每次改 `default_params.json` 前，先保存一份带时间戳的副本到 `new/verification/params/`
- 调参记录至少包含：
  - 修改前文件
  - 修改后文件
  - 测试场景
  - 结论
- 改 `P_Mode`、`exp_light` 前先准备回滚版本
- 处理场景误判时，优先做静态无电机采图；处理转向稳定性时，再做动态跑车

## 8. 一个够用的最小攻略

如果你只想快速开始，按这个顺序走：

1. 先保持 `P_Mode=3`、`exp_light=65` 不动。
2. 先调 `PID_TURN_CAMERA.P/D`，把直道和普通弯道跑顺。
3. 再单独调 `SCENE_WIDE_CLASSIFIER`，只处理 `cross / circle_entry / 普通弯道`。
4. 最后才碰轮速 PID 和执行器约束。

简单说：

- 画面不对，先看 `exp_light / see_max`
- 方向不稳，先看 `PID_TURN_CAMERA`
- 场景误判，先看 `SCENE_WIDE_CLASSIFIER`
- 动力发抖，最后看轮速和 PWM
