# `default_params.json` 调参指南

本文只描述当前 active runtime 仍会读取和发布的参数。旧复杂寻线、元素识别、拓扑、roadblock 占位和历史 PID 命名已归档，不再作为运行时、协议或调参依据。

当前闭环固定为：

```text
frame -> sparse BEV reference facts -> reference usability -> reference lateral error
-> reference-control readiness -> safety gate -> turn-output target -> actuator
```

`new/config/default_params.json` 是人工编辑的运行默认合同。`RuntimeParameters` 内建默认值只用于缺文件或解析失败时的 fallback 镜像，必须通过 `run_runtime_parameter_defaults_test.sh` 保持同步。

## 1. 调参前提

每次只调整一个明确参数族，并先写清楚要验证的假设。不要把曝光、BEV 标定、reference usability、lateral-error、yaw PID、轮速 PID 和运动状态机混在一次修改里。

推荐现场闭环：

```bash
cd new/user
rtk env BOARD_IP=10.100.170.226 ./debug.sh assistant local 8888 8890
rtk env BOARD_IP=10.100.170.226 ./debug.sh build
rtk env BOARD_IP=10.100.170.226 ./debug.sh remote restart normal
rtk env BOARD_IP=10.100.170.226 ./debug.sh steering --duration-s 20
```

证据先看这些分组：

- `perception_health.{projector_ok,reason}`：投影和感知健康。
- `reference.{mode,source}`：白点事实来源。
- `eligibility.{usable,leading_usable_samples,leading_min_forward_m,leading_max_forward_m,reason}`：reference facts 是否足够连续。
- `lateral_error.{computed,weighted_lateral_error_m,weighted_sample_count,weight_sum,reason}`：按近端更高权重计算的横向误差事实。
- `reference_control.{ready,reason}`：reference + lateral error 是否可进入控制。
- `safety_gate.{veto_active,reason}`：唯一安全 gate，独占低电压、感知健康、stale、IMU、encoder 否决。
- `yaw_control.{turn_output_target}`：weighted lateral error 生成的 turn-output 目标，单位与左右轮速半差一致。
- `actuator.{raw_turn_output,applied_turn_output}`：最终 turn-output，直接作为左右轮速半差。

最小离线回归：

```bash
rtk bash new/verification/tests/run_runtime_parameter_defaults_test.sh
rtk bash new/verification/tests/run_power_adapter_threshold_test.sh
rtk bash new/verification/tests/run_startup_low_voltage_order_test.sh
rtk bash new/verification/tests/run_bev_simple_perception_test.sh
rtk bash new/verification/tests/run_visual_reference_orchestration_test.sh
rtk bash new/verification/tests/run_reference_usability_lateral_error_test.sh
rtk bash new/verification/tests/run_assistant_telemetry_selftest.sh
rtk bash new/verification/tests/run_steering_media_selftest.sh
rtk bash new/verification/tests/run_perf_counter_test.sh
rtk bash new/verification/tests/run_bev_simple_residual_check.sh
```

## 2. 加载语义

运行时由 `new/code/platform/param_store.cpp` 读取 `new/config/default_params.json`。

- 缺文件：回退到内建默认值，并发布 `params.missing`。
- JSON 非法、必填字段缺失、字段类型错误：回退到内建默认值，并发布 `params.parse`。
- `exp_light` 是启动关键字段，必须在 `0..2500`；非法时触发 fail-safe。
- `exp_light != 65` 允许加载，但会发布曝光告警；当前相机基线仍以 `65` 为默认。

当前必填键：

- `RUNNING_SPEED_TARGET`
- `YAW_RATE_PID.D`
- `exp_light`
- `LEFT_WHEEL_PID.{P,I,D,INTEGRAL_LIMIT}`
- `RIGHT_WHEEL_PID.{P,I,D,INTEGRAL_LIMIT}`
- `assistant_tcp.{host,port}`

其余键都是可选覆盖；缺省时使用内建默认值。可选键格式错误仍会触发整体 parse fallback。

## 3. 诊断到参数的顺序

1. 没有 host 连接或数据很少：先看 `assistant_tcp.*`、`assistant_enabled`、`steering_media_*`，再看板端 `assistant.backoff`、`steering_media.backoff`、`steering_media.summary`。
2. 白点不对：先看 `exp_light`、`BEV_PROJECTOR`、`BEV_GEOMETRY`、`BEV_CLASSIFICATION`。
3. 白点对但 `eligibility.usable=false`：看 `BEV_CLASSIFICATION.HOLD_LAST_MAX_CYCLES`、`BEV_CONTROL_MODEL.MIN_LEADING_REFERENCE_SAMPLES`、`BEV_GEOMETRY.FORWARD_SAMPLE_*`。
4. 白点对但 `lateral_error` 不合理：看 row intervals、leading reference path 和 `BEV_CONTROL_MODEL.LATERAL_ERROR_FAR_WEIGHT`。
5. lateral error 合理但转向幅度不对：看 `BEV_CONTROL_MODEL.LATERAL_ERROR_TO_WHEEL_DELTA_GAIN`、`YAW_RATE_PID.*`、`raw_turn_output_limit`。
6. 直行速度或左右轮跟随不对：看 `RUNNING_SPEED_TARGET`、`LEFT_WHEEL_PID.*`、`RIGHT_WHEEL_PID.*`。
7. 起步、停止、fail-safe 恢复节奏不对：看 `motion_*`、`pwm_limit`、`pwm_floor`、反转保护和低电压参数。

## 4. 速度、yaw 和轮速 PID

| 参数 | 当前 JSON 值 | 作用层 | 调参方法与证据 |
| --- | ---: | --- | --- |
| `RUNNING_SPEED_TARGET` | `140.0` | motion supervisor / yaw speed scale | 运行轮速目标单位，不是 m/s。增大后车速更高，yaw target 也会按 speed scale 变化。看 `effective_speed_target`、左右 `*_speed_target`、encoder measured。先用低值确认闭环再上调。 |
| `YAW_RATE_PID.P` | `0.5` | gyro feedback | gyro yaw-rate 对 turn-output 的反馈修正增益。它不承担 lateral-error 前馈幅度；摆动或 raw turn 频繁反向时先看它，单纯欠转先看 `LATERAL_ERROR_TO_WHEEL_DELTA_GAIN`。 |
| `YAW_RATE_PID.I` | `0.0` | gyro feedback | gyro 反馈积分。当前默认不用。只有长期同向 gyro 偏差且 P/D 不能解决时小幅增加；积分过大会拖尾。 |
| `YAW_RATE_PID.D` | `0.0` | gyro feedback | 抑制 gyro 反馈误差变化。抖动和过冲明显时增加；过大时转向变钝。 |
| `LEFT_WHEEL_PID.P` | `84.0` | 左轮速度 PID | 左轮速度误差主增益。左轮跟随慢增大；PWM 抖或超调减小。看 `left_speed_target`、`left_measured_speed`、`left_pwm_command`。 |
| `LEFT_WHEEL_PID.I` | `2.4` | 左轮速度 PID | 左轮长期误差积分。稳态低于目标时增大；起步后拖尾或积累过冲时减小。 |
| `LEFT_WHEEL_PID.D` | `0.75` | 左轮速度 PID | 左轮速度变化阻尼。速度抖动可增大；响应迟钝可减小。 |
| `LEFT_WHEEL_PID.INTEGRAL_LIMIT` | `5000.0` | 左轮速度 PID | 左轮积分上限。积分饱和导致恢复慢时减小；长期负载跟不上且 I 有效时可增大。 |
| `LEFT_WHEEL_PID.MEASUREMENT_FILTER_ALPHA` | `0.4` | 左轮速度测量滤波 | 越大越信当前测量，响应快但噪声多；越小越平滑但滞后。看 measured speed 噪声和 PWM 震荡。 |
| `RIGHT_WHEEL_PID.P` | `96.0` | 右轮速度 PID | 右轮速度误差主增益，方法同左轮。左右默认不同，不要为了对称而强行改成一样。 |
| `RIGHT_WHEEL_PID.I` | `2.2` | 右轮速度 PID | 右轮长期误差积分，方法同左轮。 |
| `RIGHT_WHEEL_PID.D` | `0.2` | 右轮速度 PID | 右轮速度变化阻尼，方法同左轮。 |
| `RIGHT_WHEEL_PID.INTEGRAL_LIMIT` | `5000.0` | 右轮速度 PID | 右轮积分上限，方法同左轮。 |
| `RIGHT_WHEEL_PID.MEASUREMENT_FILTER_ALPHA` | `0.4` | 右轮速度测量滤波 | 右轮测量滤波，方法同左轮。 |

## 5. 相机与基础时序

| 参数 | 当前 JSON 值 | 作用层 | 调参方法与证据 |
| --- | ---: | --- | --- |
| `exp_light` | `65` | camera startup critical | 曝光/亮度基线。白线整体偏暗可上调，背景变白或阈值混乱则下调。改动后必须看原始 raw、分类结果和 `reference` 白点；不要用控制结果倒推曝光。 |
| `camera_frame_width` | `320` | camera frame contract | 相机输入宽度合同。只在相机源真的改变分辨率时改；改错会让 frame 几何 fail-closed。 |
| `camera_frame_height` | `240` | camera frame contract | 相机输入高度合同。只在相机源真的改变分辨率时改。 |
| `control_period_ms` | `5` | control timer | 控制 tick 周期。减小会提高 CPU/IO 压力；增大会降低控制响应。看 perf、`control.tick` 和实际电机稳定性。 |
| `perception_stale_ms` | `120` | safety gate | 最新 perception 超过该时间即 stale。摄像头偶发慢帧可适当增大；过大则会让旧白点继续影响控制。看 `safety_gate.reason=perception_stale`。 |
| `control_snapshot_emit_interval_ms` | `100` | debug reporter | 板端 `control.snapshot` 与 `control.steering_snapshot` 输出周期。只影响日志密度，不改变控制。 |

## 6. 执行器与运动状态机

| 参数 | 当前 JSON 值 | 作用层 | 调参方法与证据 |
| --- | ---: | --- | --- |
| `pwm_limit` | `5000` | actuator safety | 左右轮 PWM 绝对限幅。车无力且 PID 未饱和时不要先改它；只有确认输出长期被限幅且硬件允许时上调。 |
| `raw_turn_output_limit` | `100` | turn output safety | turn-output 绝对限幅，单位与左右轮速半差一致。它是兜底边界，不是常规转向幅度调参旋钮；满幅时目标大约是 `speed ± 100`。 |
| `pwm_floor` | `0` | actuator shaping | 非零 PWM 的最小地板。低速克服静摩擦可小幅上调；过高会让轻微控制也变成突跳。 |
| `prohibit_reverse_pwm` | `1` | actuator safety | 禁止输出反向 PWM。赛道调参默认保持开启；关闭会扩大硬件风险。 |
| `prohibit_reverse_pwm_step_limit` | `280` | actuator safety | 反转保护/输出变化步进限制。反向突变风险高时减小；输出响应太慢且无反向风险时增大。 |
| `motion_unveto_confirm_cycles` | `3` | motion supervisor | safety gate 解除后需要连续干净周期数。误解除风险高时增大；恢复太慢时减小。 |
| `motion_spinup_ms` | `800` | motion supervisor | 起步速度爬升时间。起步打滑或冲击大时增大；起步太慢时减小。 |
| `motion_turn_limit_spinup` | `0.35` | motion supervisor | 起步阶段转向限幅比例。起步时转向过猛减小；起步弯道跟不上增大。 |
| `motion_pwm_step_limit` | `280` | motion supervisor | motion 阶段 PWM 步进限制。输出突变大时减小；响应太慢时增大。 |
| `motion_stop_ms` | `300` | motion supervisor | stop 阶段速度衰减时间。停车太急增大；停车拖尾减小。 |
| `motion_stop_encoder_threshold` | `8` | motion supervisor | 判定停止的 encoder 阈值。车已停但不退出 STOPPING 可增大；未停就退出可减小。 |
| `motion_fault_rearm_hold_ms` | `600` | motion supervisor | fail-safe latch 后允许 rearm 前的保持时间。现场排障保守时增大；恢复流程过慢时减小。 |

## 7. Low Voltage 与调试传输

| 参数 | 当前 JSON 值 | 作用层 | 调参方法与证据 |
| --- | ---: | --- | --- |
| `low_voltage_raw_threshold` | `400` | power adapter / safety gate | ADC raw 低电压阈值。实际使用值记录在 `LowVoltageSample.threshold`；`LS2K_LOW_VOLTAGE_RAW_THRESHOLD` 环境变量优先。误报低电压时先查 ADC raw，再谨慎下调；不设上限，超大正数会更保守。 |
| `low_voltage_sample_interval_ms` | `1000` | low-voltage sampler | 运行期低电压采样周期。默认 1Hz；降低会增加 IO，升高会降低低电压发现速度。 |
| `assistant_enabled` | `1` | assistant TCP | 是否启用 command/ACK/telemetry 链路。连接调试时保持开启；纯离线运行可关闭。 |
| `assistant_tcp.host` | `10.100.170.115` | assistant TCP | 板端主动连接的 host 地址。必须等于 `ip route get <BOARD_IP>` 的 `src`；错误时板端会 `assistant.backoff Connection refused/timeout`。 |
| `assistant_tcp.port` | `8888` | assistant TCP | host assistant listener 端口。必须和 `debug.sh assistant local` / `tune_speed.py` / `tune_steering.py` 一致。 |
| `steering_media_enabled` | `1` | steering media TCP | 是否启用图像和 steering snapshot side channel。调视觉/白点时保持开启；带宽或 CPU 排查时可临时关闭。 |
| `steering_media_port` | `8890` | steering media TCP | host media listener 端口。必须和 `--media-listen-port` 一致。 |
| `steering_media_publish_interval_ms` | `120` | steering media service | 图像发布间隔。`120ms` 理论上约 `8.3fps`；想要更多监听帧先降到 `80` 或 `60`。看 host `effective_fps` 和板端 `steering_media.summary.skip_interval/image_sent`。 |

## 8. BEV Projector 标定

`BEV_PROJECTOR` 定义原图到车辆坐标系的投影。它是白点事实层的根，错误时后续所有参数都会被误导。

| 参数 | 当前 JSON 值 | 调参方法与证据 |
| --- | --- | --- |
| `BEV_PROJECTOR.VALID` | `1` | 投影是否可用。置 `0` 会让 perception health 失败，只用于 fail-safe 验证。 |
| `BEV_PROJECTOR.PROJECTOR_ID` | `bev_projector_true_bev_manual_forward_scale_v5` | 标定版本名。只改标识，不改变几何；更新标定时同步改。 |
| `BEV_PROJECTOR.PROJECTOR_HASH` | `bev-projector-true-bev-manual-forward-scale-20260428` | 标定版本 hash/说明。只用于身份和 LUT 重建判断。 |
| `BEV_PROJECTOR.DEBUG_GRID_WIDTH` | `160` | dense debug BEV 图宽度，只影响调试图，不是 runtime sparse authority。 |
| `BEV_PROJECTOR.DEBUG_GRID_HEIGHT` | `128` | dense debug BEV 图高度，只影响调试图。 |
| `BEV_PROJECTOR.SOURCE_ROW_0` / `SOURCE_COL_0` | `220.0` / `19.0` | 近端左标定点在原图中的像素位置。 |
| `BEV_PROJECTOR.SOURCE_ROW_1` / `SOURCE_COL_1` | `220.0` / `305.0` | 近端右标定点在原图中的像素位置。 |
| `BEV_PROJECTOR.SOURCE_ROW_2` / `SOURCE_COL_2` | `68.0` / `108.0` | 远端左标定点在原图中的像素位置。 |
| `BEV_PROJECTOR.SOURCE_ROW_3` / `SOURCE_COL_3` | `68.0` / `220.0` | 远端右标定点在原图中的像素位置。 |
| `BEV_PROJECTOR.TARGET_FORWARD_0` / `TARGET_LATERAL_0` | `0.061` / `-0.21` | 近端左标定点对应的车辆坐标。 |
| `BEV_PROJECTOR.TARGET_FORWARD_1` / `TARGET_LATERAL_1` | `0.061` / `0.21` | 近端右标定点对应的车辆坐标。 |
| `BEV_PROJECTOR.TARGET_FORWARD_2` / `TARGET_LATERAL_2` | `0.61` / `-0.21` | 远端左标定点对应的车辆坐标。 |
| `BEV_PROJECTOR.TARGET_FORWARD_3` / `TARGET_LATERAL_3` | `0.61` / `0.21` | 远端右标定点对应的车辆坐标。 |

调 `SOURCE_*` 或 `TARGET_*` 时必须重新生成 dense debug BEV、分类图、row intervals 和白点 overlay。不要通过 lateral-error 或 PID 参数掩盖标定错误。

## 9. BEV Geometry 行扫描

| 参数 | 当前 JSON 值 | 作用与调参方法 |
| --- | --- | --- |
| `BEV_GEOMETRY.FORWARD_SAMPLE_0` | `0.061` | reference path 第 0 层，最近端控制事实。index 0 没有 interval 时当前视觉 reference invalid。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_1` | `0.123565` | 第 1 层。用于 leading 连续段和插值。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_2` | `0.18613` | 第 2 层。默认 `MIN_LEADING_REFERENCE_SAMPLES=3` 时，这是最小 usable 远端。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_3` | `0.248696` | 第 3 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_4` | `0.311261` | 第 4 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_5` | `0.373826` | 第 5 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_6` | `0.436391` | 第 6 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_7` | `0.498957` | 第 7 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_8` | `0.561522` | 第 8 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_9` | `0.624087` | 第 9 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_10` | `0.686652` | 第 10 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_11` | `0.749217` | 第 11 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_12` | `0.811783` | 第 12 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_13` | `0.874348` | 第 13 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_14` | `0.936913` | 第 14 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_15` | `0.999478` | 第 15 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_16` | `1.062043` | 第 16 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_17` | `1.124609` | 第 17 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_18` | `1.187174` | 第 18 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_19` | `1.249739` | 第 19 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_20` | `1.312304` | 第 20 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_21` | `1.37487` | 第 21 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_22` | `1.437435` | 第 22 层。 |
| `BEV_GEOMETRY.FORWARD_SAMPLE_23` | `1.5` | 第 23 层，最远端视觉事实；当前算法不会为了远端点跨 gap 补点。 |
| `BEV_GEOMETRY.SEARCH_LATERAL_LIMIT_M` | `0.65` | BEV 后横向扫描半宽。漏掉真实白线时可增大；噪声 interval 变多时减小。它不是原图有效 span 裁剪。 |
| `BEV_GEOMETRY.LATERAL_STEP_M` | `0.02` | BEV 横向采样步长。减小会更精细但更耗时、更易拾取细碎噪声；增大会更稳但白点量化更粗。 |

`FORWARD_SAMPLE_*` 必须单调递增。改采样分布会影响 LUT identity、leading range、lateral-error 权重含义和 steering media snapshot；不要只改某一个点来修局部画面。

## 10. BEV Classification 与 hold

| 参数 | 当前 JSON 值 | 作用层 | 调参方法与证据 |
| --- | ---: | --- | --- |
| `BEV_CLASSIFICATION.WHITE_CONFIDENCE_MIN` | `0.55` | sparse sample classification | 白色置信阈值。漏白线时降低；背景被误判为白时提高。看分类图、row intervals、`reference.source=simple_interval_center` 的白点来源。 |
| `BEV_CLASSIFICATION.UNKNOWN_CONFIDENCE_MIN` | `0.25` | sparse sample classification | 阈值附近 unknown 区间。误把灰噪声当事实时提高 unknown 区；真实白线被 unknown 吃掉时降低。 |
| `BEV_CLASSIFICATION.HOLD_LAST_MAX_CYCLES` | `32` | reference continuity | 当前视觉 facts 不 usable 时最多 hold 上次白点路径的周期数。短暂丢点可增大；不想让旧路径影响控制就减小。hold 点必须显示 `reference.mode=hold_last`、`reference.source=hold`。 |

分类语义固定：

- `white`：可进入 row white interval。
- `black`：背景事实。
- `unknown`：不参与 white interval。
- `invalid`：采样不可用；不能自动成为 edge、元素或路径证据。

## 11. BEV Control Model

| 参数 | 当前 JSON 值 | 作用层 | 调参方法与证据 |
| --- | ---: | --- | --- |
| `BEV_CONTROL_MODEL.LATERAL_ERROR_FAR_WEIGHT` | `0.25` | reference lateral error | 24 点线性权重的远端权重，近端固定为 `1.0`。合法范围 `[0.0, 1.0]`，越界参数按解析失败处理，不在公式里隐藏修正。减小会更重视近端；增大会让远端趋势更影响输出。 |
| `BEV_CONTROL_MODEL.LATERAL_ERROR_TO_WHEEL_DELTA_GAIN` | `180` | turn-output target | weighted lateral error 到左右轮速半差目标的直接增益。合法范围 `[0, 1000]`，越界参数按解析失败处理。`0.20m` 横向误差在 speed scale 为 `1` 时输出约 `36`。 |
| `BEV_CONTROL_MODEL.MIN_LEADING_REFERENCE_SAMPLES` | `3` | reference usability | 从 index 0 开始连续 present 白点的最小数量。降低会更容易进入控制但容错差；提高更保守但可能频繁 unusable。小于数学下限时按 2 处理。 |

## 12. 禁止使用历史参数思路调车

不属于当前 `default_params.json` 的历史参数名、历史场景名、历史拓扑/策略字段，都不得作为 active 调参依据。需要查历史上下文时看 archive 文档；新的调参记录只写本文列出的当前参数和当前分层证据字段。

## 13. 改参记录模板

每次赛道改参至少记录：

```text
时间:
参数文件/commit:
只改的参数:
改参假设:
验证命令:
证据目录:
关键字段:
  perception_health:
  reference:
  eligibility:
  lateral_error:
  reference_control:
  safety_gate:
  yaw_control:
  actuator:
结论:
下一步:
```

运行时分层与 include 边界见 `new/code/port/README.md`。`PerceptionResult is a runtime transport snapshot, not a dependency shortcut.`

后续在 `bev-simple-reference-extension` 上扩展 BEV 元素或路径策略前，先遵守根目录 `README.md` 中的大道至简与互不知晓约束。
