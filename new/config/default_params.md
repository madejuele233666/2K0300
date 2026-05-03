# `default_params.json` 当前参数合同

本文只描述当前 active runtime 仍会读取和发布的参数。BEV 参考路径与控制闭环固定为：

```text
frame -> sparse BEV reference facts -> reference usability -> reference curvature
-> reference-control readiness -> safety gate -> yaw target -> actuator
```

旧复杂寻线、元素识别、拓扑、roadblock 占位和历史 PID 命名已归档，不再作为运行时、协议或调参依据。

## 1. 加载语义

运行时由 `new/code/platform/param_store.cpp` 读取 `new/config/default_params.json`。

- 缺文件：回退到 `RuntimeParameters` 内建默认值，并发布 `params.missing`。
- JSON 非法、必填字段缺失、字段类型错误：回退到内建默认值，并发布 `params.parse`。
- `default_params.json` 是人工编辑的默认合同；`RuntimeParameters` 内建默认值是缺文件或解析失败时使用的 fallback 镜像，必须与 JSON 保持一致。
- `exp_light` 是启动关键字段，必须在 `0..2500`；非法时触发 fail-safe。
- `exp_light != 65` 允许加载，但会发布曝光告警；当前相机基线仍以 `65` 为默认。

当前必填键：

- `RUNNING_SPEED_TARGET`
- `YAW_RATE_PID.D`
- `exp_light`
- `LEFT_WHEEL_PID.{P,I,D,INTEGRAL_LIMIT}`
- `RIGHT_WHEEL_PID.{P,I,D,INTEGRAL_LIMIT}`
- `assistant_tcp.{host,port}`

其余键都是可选覆盖；缺省时使用内建默认值。

## 2. 参数面

### 2.1 速度、相机与执行器

- `RUNNING_SPEED_TARGET`：运行轮速目标单位；当前不是 m/s 标定值。
- `camera_frame_width / camera_frame_height`：当前相机帧尺寸，默认 `320 x 240`。
- `exp_light`：启动关键字段，除非同步验证相机链路，否则不要改。
- `pwm_limit / raw_turn_output_limit / pwm_floor`：执行器输出限制。
- `prohibit_reverse_pwm` 与相关 step limit：反转保护。
- `motion_*`：运动状态机的起步、停止和解除否决节奏。
- `wheel_turn_target_scale`：转向输出转轮速差的外部缩放。

### 2.2 Yaw-rate 与轮速 PID

- `YAW_RATE_PID.{P,I,D}`：gyro yaw-rate 内环参数。
- `LEFT_WHEEL_PID`、`RIGHT_WHEEL_PID`：左右轮速度 PID。

调车时先区分问题来源：

- 白点位置错误：先看 BEV 图、分类图、row intervals。
- 白点合理但车转向幅度不合适：再看 `BEV_CONTROL_MODEL`、yaw-rate 内环和执行器限制。
- 轮速跟随差：再调左右轮 PID。

### 2.3 BEV 投影

`BEV_PROJECTOR` 保留现有四点标定和纵向尺度修正。

- `SOURCE_ROW_* / SOURCE_COL_*`：原图中的四个标定点。
- `TARGET_FORWARD_* / TARGET_LATERAL_*`：车辆坐标系中的四个目标点。
- `DEBUG_GRID_WIDTH / DEBUG_GRID_HEIGHT`：调试图尺寸。
- `PROJECTOR_ID / PROJECTOR_HASH`：标定版本标识。

运行时寻线层消费 sparse BEV metric samples；dense BEV 图和分类图只是调试产物。
metric 到原图的反投影只属于 BEV sparse sample LUT 和 dense debug 图的构建细节。

### 2.4 BEV 几何

`BEV_GEOMETRY` 只保留三个含义：

- `FORWARD_SAMPLE_0..23`
  - reference_path 的 24 个前向白点采样距离。
  - 当前默认覆盖 `0.061m..1.500m`。
- `SEARCH_LATERAL_LIMIT_M`
  - BEV 后图像横向扫描范围。
  - 它定义扫描面板宽度，不是原图投影有效性裁剪。
- `LATERAL_STEP_M`
  - BEV 后图像横向像素/分类采样步长。
  - 行扫描、白点连续性阈值都从这个量派生。

### 2.5 BEV 分类

`BEV_CLASSIFICATION` 是当前视觉事实层的阈值面。

- `WHITE_CONFIDENCE_MIN`：中心像素相对阈值足够亮时才分类为 white。
- `UNKNOWN_CONFIDENCE_MIN`：中心像素离阈值太近时分类为 unknown。
- `HOLD_LAST_MAX_CYCLES`：当前帧没有连续视觉证据时，允许显式 hold 上一次白点路径的最大周期数。

分类语义固定为：

- `white`：可作为 row white interval 的图像事实。
- `black`：背景事实。
- `unknown`：不参与 white interval。
- `invalid`：BEV 图生成阶段明确无可靠像素；不能自动成为 edge 或元素证据。

### 2.6 BEV 控制模型

`BEV_CONTROL_MODEL` 只把 selected reference path 变成 curvature command，并提供 curvature 到 gyro yaw-rate target 单位的增益。

- `LOOKAHEAD_VISIBLE_RANGE_RATIO`：前视距离按可用 reference path 前向范围比例选择。
- `LOOKAHEAD_MIN_M / LOOKAHEAD_MAX_M`：前视距离上下限。
- `PURE_PURSUIT_GAIN`：pure-pursuit 曲率增益。
- `CURVATURE_COMMAND_LIMIT`：曲率命令限幅。
- `CURVATURE_TO_YAW_RATE_TARGET_GAIN`：曲率到 yaw-rate target 单位的缩放；当前不声明为真实 rad/s。
- `MIN_LEADING_REFERENCE_SAMPLES`：从 reference path index 0 开始连续 present 白点的最小 usable 数量，默认 `3`；小于插值数学下限时按 `2` 处理。

控制调试输出采用分组合同：

- `perception_health.{projector_ok,reason}`：感知健康事实。
- `reference.{mode,source}`：白点事实来源。
- `eligibility.{usable,leading_usable_samples,leading_min_forward_m,leading_max_forward_m,lookahead_distance_m,reason}`：reference facts 是否足够连续。
- `curvature.{computed,lookahead_distance_m,curvature_command,reason}`：几何曲率计算结果。
- `reference_control.{ready,reason}`：selected reference 与 curvature 是否足够进入控制。
- `safety_gate.{veto_active,reason}`：唯一安全 gate 输出，独占低电压、感知健康、stale、IMU、encoder 否决。
- `degraded.{active,reason}`：可控但降级的原因。
- `yaw_control.{yaw_rate_target}`：gyro yaw-rate 目标单位。
- `actuator.{raw_turn_output,applied_turn_output}`：转向执行输出。

### 2.7 调试发布

- `control_snapshot_emit_interval_ms`
- `assistant_enabled`
- `assistant_tcp.{host,port}`
- `steering_media_enabled`
- `steering_media_port`
- `steering_media_publish_interval_ms`

assistant 只发布 command/ACK/telemetry；图像调试统一走 steering media。
这些字段只影响联调链路，不改变 BEV 图像事实或 reference path 几何。

### 2.8 低电压与性能计时

- `low_voltage_sample_interval_ms`
  - 运行期低电压采样周期，默认 `1000ms`。
  - 启动阶段仍立即采样一次；进入主循环后只有低频 sampler 会继续采样，控制环和感知链路只读缓存状态。
- `low_voltage_raw_threshold`
  - ADC raw 低电压阈值，默认 `400`。
  - 该阈值由 power adapter 配置并由 low-voltage sampler 记录实际使用值；`LS2K_LOW_VOLTAGE_RAW_THRESHOLD` 环境变量只作为硬件调试 override，优先级高于 JSON 参数。

性能计时由构建开关控制，不属于 JSON 参数：

- `LS2K_PERF_ENABLED`：默认 `OFF`。
- `LS2K_PERF_USE_CYCLE_COUNTER`：默认 `ON`。

## 3. 现场验证顺序

推荐闭环：

```bash
cd new/user
rtk env BOARD_IP=10.100.170.226 ./debug.sh assistant local 8888 8890
rtk env BOARD_IP=10.100.170.226 ./debug.sh build
rtk env BOARD_IP=10.100.170.226 ./debug.sh remote restart normal
rtk env BOARD_IP=10.100.170.226 ./debug.sh steering --duration-s 20
```

最小离线回归：

```bash
rtk bash new/verification/tests/run_bev_simple_perception_test.sh
rtk bash new/verification/tests/run_reference_usability_curvature_test.sh
rtk bash new/verification/tests/run_steering_media_selftest.sh
rtk bash new/verification/tests/run_perf_counter_test.sh
rtk bash new/verification/tests/run_bev_simple_residual_check.sh
```

排查顺序固定为：

1. 看原始 BEV 图是否符合标定预期。
2. 看 BEV 分类图是否只把真实白线标为 white。
3. 看每行 white intervals 是否来自真实连续白色区域。
4. 看 reference_path 每个白点的 `source`。
5. 白点合理后，再看 `curvature_command / yaw_rate_target / actuator`。

运行时分层与 include 边界见 `new/code/port/README.md`。`PerceptionResult` is a runtime transport snapshot, not a dependency shortcut.
