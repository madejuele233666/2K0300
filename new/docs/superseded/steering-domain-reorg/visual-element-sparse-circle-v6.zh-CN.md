# Visual Element Sparse Circle V6 同层 Reference Tracking Geometry / Curvature-Aware Steering

状态：设计记录。V6 不修改 CircleV2 FSM，不修改 circle / cross 元素识别，不调整 visual reference arbitration，不改变 wheel mixer / wheel PID / PWM 输出层。V6 记录一个控制链路层面的修正方向：转向目标不应只由前方路径点的加权 `lateral_error` 生成，而应由同一条 selected reference path 解释出的同层几何事实生成。

本文档继承 V2/V3/V4/V5 的“互不知晓”和“大道至简”原则：

```text
路径生成模块只生成 BEVReferencePath；
reference tracking geometry 只解释 selected/aligned reference 的几何事实；
steering controller 只消费 tracking geometry 并生成 turn_output_target；
wheel mixer / wheel PID / PWM 层不知道 reference 几何来自哪里。
```

## 1. 本次问题

当前控制链路里，`weighted_lateral_error_m` 是这样得到的：

```text
selected BEVReferencePath
-> 取前方一串 usable samples
-> 对 sample.point.lateral_m 做加权平均
-> weighted_lateral_error_m
```

然后转向目标近似为：

```text
turn_output_target =
  lateral_error_to_wheel_delta_gain
  * speed_scale
  * weighted_lateral_error_m
```

这个设计在直道上可用，但在弯道、环岛和大曲率路径里会混淆两个不同语义：

```text
车辆当前相对参考线的横向偏差；
参考线未来一段本身的弯曲趋势。
```

前方路径向左弯时，即使车身当前并没有偏离参考线，远端 samples 的 `lateral_m` 也会变大。此时把加权 `lateral_m` 当作唯一控制误差，会把“路径形状”误当成“车辆偏差”。

因此 V6 的核心不是简单新增一个 curvature 项，也不是让 curvature 替代 lateral error，而是把旧的单一 `ReferenceLateralErrorEstimate` 升级为同层的 reference tracking geometry。

## 2. 设计结论

V6 应建立一个统一几何事实层：

```text
BEVReferencePath
-> ReferenceTrackingGeometry
     lateral_offset_m
     heading_error_rad
     curvature_m_inv
-> SteeringControlLaw
-> turn_output_target
```

这三个量同层级，因为它们都来自同一条 selected/aligned reference path 的局部几何解释：

```text
0 阶：lateral_offset_m
  参考线横向位置项，描述控制律中的横向修正输入。

1 阶：heading_error_rad
  参考线在车辆当前位置附近的切线方向，描述车身朝向相对参考线的方向偏差。

2 阶：curvature_m_inv
  参考线局部曲率，描述这条路本身要求车辆产生的基础转弯趋势。
```

“同层级”不代表控制律里必须并列相加。更准确的控制结构是：

```text
base_turn = CurvatureFeedforward(curvature_m_inv, speed)
correction = TrackingCorrection(lateral_offset_m, heading_error_rad, speed)

turn_output_target = base_turn + correction
```

也就是说：

```text
curvature:
  偏前馈，回答“这条路本身要怎么转”。

lateral_offset / heading_error:
  偏反馈，回答“车当前偏离这条路多少，需要怎么修正”。
```

## 3. 不做的事

V6 不应把曲率逻辑放入以下模块：

```text
CircleV2Scene
CircleV2 event observer / geometry observer / composer
cross detector
ordinary path builder
single-boundary helper
path connectivity helper
VisualReferenceSelector
WheelTargetMixer
WheelPidController
motor logic / PWM layer
```

这些模块不应该知道：

```text
curvature gain；
lateral correction gain；
heading correction gain；
曲率来自环岛还是普通弯道；
当前 reference 是 line / circle / cross。
```

它们只保持自己的职责：

```text
path generator:
  生成路径。

selector:
  选择路径。

tracking geometry:
  解释 selected reference 的几何事实。

controller:
  将 tracking geometry 映射为 turn_output_target。

wheel mixer:
  将 applied_turn_output 映射为左右轮目标。
```

## 4. ReferenceTrackingGeometry

建议新增中性类型：

```cpp
struct ReferenceTrackingGeometry {
  bool computed = false;
  float lateral_offset_m = 0.0F;
  float heading_error_rad = 0.0F;
  float curvature_m_inv = 0.0F;
  std::size_t sample_count = 0;
  std::string reason = "reference_unusable";
};
```

该类型不包含：

```text
candidate kind；
circle direction；
cross evidence；
FSM phase；
图像阈值；
PWM；
wheel speed；
gyro_z。
```

它只回答：

```text
当前 selected/aligned reference path 在指定拟合/窗口策略下的几何事实是什么。
```

## 5. 几何估计

第一版可以用 selected/aligned `BEVReferencePath` 的 leading usable prefix 做二次拟合：

```text
令 x = forward_m
令 y = lateral_m

y = a*x^2 + b*x + c
```

则可解释为：

```text
lateral_offset_m = c
heading_error_rad = atan(b)
curvature_m_inv ~= 2a / (1 + b^2)^(3/2)
```

若选择在某个近端 anchor `x0` 处解释，也可以写成：

```text
y0 = a*x0^2 + b*x0 + c
slope0 = 2a*x0 + b

lateral_offset_m = y0
heading_error_rad = atan(slope0)
curvature_m_inv ~= 2a / (1 + slope0^2)^(3/2)
```

第一版应保持简单：

```text
使用同一段 leading usable samples；
不按 line / circle / cross 分场景；
不读原始图像；
不重新做路径选择；
不引入场景专用曲率阈值。
```

如果 usable samples 不足以稳定拟合：

```text
ReferenceTrackingGeometry.computed = false；
reason 复用 reference unusable / insufficient_samples 类语义；
readiness 层决定是否允许控制。
```

## 6. 控制律

建议把 `SteeringYawController` 的输入从单个 `weighted_lateral_error_m` 升级为：

```cpp
TurnOutputTargetComputation ComputeTurnOutputTarget(
    const port::ReferenceTrackingGeometry& geometry,
    double effective_speed_target,
    port::BEVControllerMemory& memory);
```

第一版控制律可以是：

```text
speed_scale = effective_speed_target / nominal_running_speed

curvature_term =
  curvature_to_wheel_delta_gain
  * speed_scale
  * geometry.curvature_m_inv

lateral_term =
  lateral_offset_to_wheel_delta_gain
  * speed_scale
  * geometry.lateral_offset_m

heading_term =
  heading_error_to_wheel_delta_gain
  * speed_scale
  * geometry.heading_error_rad

turn_output_target =
  curvature_term + lateral_term + heading_term
```

三项统一使用 `speed_scale`。这使三个 gain 的调参语义一致：

```text
lateral / heading / curvature gain
  都表示 nominal_running_speed 下，每单位几何输入产生多少 turn-output。

speed_scale
  只负责随当前 effective_speed_target 对控制强度做等比例缩放。
```

为了大道至简，第一版可以先令：

```text
heading_error_to_wheel_delta_gain = 0
```

但数据结构中保留 `heading_error_rad`。这样后续启用 heading feedback 时不需要重拆架构。

重要约束：

```text
不要把 curvature 和旧 weighted_lateral_error 粗暴相加。
```

旧 `weighted_lateral_error_m` 已经混入未来路径形状；在它之上再加 curvature 时，弯道可能会被双重计算。这里不把“双重计算”定义成架构错误，也不强行引入一个模糊不清的“局部 lateral”语义来消除它。V6 的重点是把 `lateral_offset_m`、`heading_error_rad`、`curvature_m_inv` 三个几何参数分离，让同一层控制律分别消费三项，再通过控制参数调节三项的权重。

这里说的“三个参数”是 reference tracking geometry 的三项：

```text
0 阶：lateral_offset_m
1 阶：heading_error_rad
2 阶：curvature_m_inv
```

不是三段前中后 offset，也不是三个 gain。gain 只属于控制律参数面。

## 7. Gyro 反馈关系

当前 gyro feedback 更像零角速度阻尼：

```text
gyro_error = -gyro_z
output = turn_output_target + gyro_pid(gyro_error)
```

当 curvature feedforward 开始承担基础转弯时，gyro feedback 继续强力追求零角速度，可能抵消曲率前馈。

更完整的方向是：

```text
desired_yaw_rate = f(curvature_m_inv, speed)
gyro_error = desired_yaw_rate - measured_yaw_rate
output = turn_output_target + gyro_pid(gyro_error)
```

但这可以作为第二步。V6 第一版可以先只改 reference geometry 和 turn target，同时把 gyro 增益保持保守，避免把两个问题混在一起。

## 8. 参数面

第一版参数应尽量少：

```text
BEV_CONTROL_MODEL.LATERAL_OFFSET_TO_WHEEL_DELTA_GAIN
BEV_CONTROL_MODEL.CURVATURE_TO_WHEEL_DELTA_GAIN
BEV_CONTROL_MODEL.HEADING_ERROR_TO_WHEEL_DELTA_GAIN
BEV_CONTROL_MODEL.TRACKING_FIT_MIN_SAMPLES
```

可选参数：

```text
BEV_CONTROL_MODEL.TRACKING_FIT_FORWARD_MIN_M
BEV_CONTROL_MODEL.TRACKING_FIT_FORWARD_MAX_M
BEV_CONTROL_MODEL.TRACKING_ANCHOR_FORWARD_M
```

如果不加 forward window 参数，第一版默认使用当前 `ReferenceUsability` 认可的 leading usable prefix。

保留旧参数兼容时应明确语义迁移：

```text
LATERAL_ERROR_TO_WHEEL_DELTA_GAIN
  旧语义：weighted future lateral average -> turn output
  V6 语义：lateral_offset_m -> lateral term
```

如果为了避免混淆，建议改名为：

```text
LATERAL_OFFSET_TO_WHEEL_DELTA_GAIN
```

## 9. Debug / Telemetry

必须暴露控制分解，否则无法调参：

```text
tracking_geometry.computed
tracking_geometry.lateral_offset_m
tracking_geometry.heading_error_rad
tracking_geometry.curvature_m_inv
tracking_geometry.sample_count
tracking_geometry.reason

yaw_control.curvature_term
yaw_control.lateral_term
yaw_control.heading_term
yaw_control.turn_output_target
```

保留旧字段时应避免误导：

```text
lateral_error.weighted_lateral_error_m
```

可以短期保留用于对照，但 V6 主控制事实应迁移到：

```text
tracking_geometry.lateral_offset_m
```

## 10. 数据流

V6 后建议数据流：

```text
Camera frame
-> BEV sparse row scan
-> path generators
   -> ordinary path
   -> cross path
   -> circle path
-> V5 connectivity clipper
-> VisualReferenceCandidate set
-> SelectVisualReference
-> Reference continuity / hold
-> optional reference time alignment
-> ReferenceUsability
-> ReferenceTrackingGeometry
-> ReferenceControlReadiness
-> SteeringYawController
   -> curvature feedforward
   -> lateral / heading correction
   -> gyro correction
-> raw_turn_output / applied_turn_output
-> WheelTargetMixer
-> WheelPidController
-> PWM
```

注意：

```text
ReferenceTrackingGeometry 应在 selected/aligned reference path 之后计算。
```

这样它天然适配：

```text
ordinary line；
single-boundary inferred path；
circle inner / exit path；
cross exit path；
hold-last / time-aligned path。
```

因为它只关心最终用于控制的 reference path，不关心路径来源。

## 11. Readiness 边界

`ReferenceControlReadiness` 不应只检查 lateral error 是否 computed。V6 后应检查：

```text
reference_usability.usable；
reference_tracking_geometry.computed；
必要控制输入 finite；
hold / stale / alignment 仍按现有规则处理。
```

它不应该知道：

```text
curvature gain；
控制律公式；
PWM 限幅；
gyro PID。
```

## 12. 落地步骤

推荐小步落地：

```text
1. 新增 ReferenceTrackingGeometry 类型。
2. 新增 neutral helper：ComputeReferenceTrackingGeometry(reference_path, usability, params)。
3. 在 perception pipeline 中同时计算旧 lateral_error 和新 tracking_geometry。
4. telemetry 先暴露 tracking_geometry，但 controller 仍可先使用旧 lateral_error。
5. 修改 SteeringYawController 输入为 tracking_geometry。
6. 控制律先启用 curvature_term + lateral_term，heading_term 默认 0。
7. 逐步废弃旧 weighted_lateral_error 主控语义。
```

若想最小改动，也可以先保留旧 `ReferenceLateralErrorEstimate` 字段，但控制器入口不应继续只接收单个 `weighted_lateral_error_m`。

## 13. 当前代码修改面

V6 的实现边界只在：

```text
selected / held / time-aligned reference path 之后；
WheelTargetMixer 之前。
```

明确不改：

```text
CircleV2Scene；
CircleV2 event observer / geometry observer / composer；
cross detector；
ordinary path builder；
single-boundary helper；
path connectivity helper；
VisualReferenceSelector；
WheelTargetMixer；
WheelPidController；
PWM 输出层。
```

这些模块仍然只负责生成、过滤、选择或执行 reference；它们不应知道 `lateral_offset_m`、`heading_error_rad`、`curvature_m_inv` 如何进入控制律。

需要新增：

```text
new/code/port/reference_tracking_geometry_types.hpp
new/code/legacy/steering_reference_tracking_geometry.hpp
new/code/legacy/steering_reference_tracking_geometry.cpp
```

`reference_tracking_geometry_types.hpp` 定义中性事实：

```cpp
struct ReferenceTrackingGeometry {
  bool computed = false;
  float lateral_offset_m = 0.0F;
  float heading_error_rad = 0.0F;
  float curvature_m_inv = 0.0F;
  std::size_t sample_count = 0;
  std::string reason = "reference_unusable";
};
```

`steering_reference_tracking_geometry` 只允许依赖：

```text
BEVReferencePath；
ReferenceUsability；
BEVControlModelParameters / RuntimeParameters。
```

它不允许依赖：

```text
PerceptionResult；
VisualReferenceSelection；
VisualReferenceCandidate；
CircleV2Memory / CircleV2Telemetry；
ControlGateDecision；
WheelTargetMixer；
PWM / motor adapter。
```

需要更新的权威数据流文件：

```text
new/code/port/perception_result.hpp
new/code/runtime/steering_frame_perception_pipeline.cpp
new/code/runtime/control_loop.cpp
new/code/legacy/steering_reference_control_readiness.hpp
new/code/legacy/steering_reference_control_readiness.cpp
new/code/legacy/steering_yaw_controller.hpp
new/code/legacy/steering_yaw_controller.cpp
```

其中：

```text
PerceptionResult:
  新增 reference_tracking_geometry。
  reference_lateral_error 可短期保留为 debug 对照，但不得继续作为 yaw controller 的唯一权威输入。

SteeringFramePerceptionPipeline:
  在 selected / held reference_path 和 ReferenceUsability 之后计算 ReferenceTrackingGeometry。
  不改变 visual reference candidate 构造、连通性过滤、arbitration 或 hold 选择。

ControlLoop:
  reference time alignment 后必须重算 ReferenceTrackingGeometry。
  readiness 和 yaw controller 消费 control-time reference 对应的 geometry。

ReferenceControlReadiness:
  从检查 lateral_error.computed 改为检查 reference_tracking_geometry.computed。
  不读取 gain、控制律公式、PWM 限幅或 gyro PID。

SteeringYawController:
  输入从 weighted_lateral_error_m 改为 ReferenceTrackingGeometry。
  输出 turn_output_target，同时输出 lateral_term / heading_term / curvature_term 分解。
```

需要更新的参数面：

```text
new/code/port/bev_geometry_types.hpp
new/code/platform/param_store.cpp
new/code/platform/steering_media_protocol.cpp
new/config/default_params.json
new/config/default_params.md
```

参数命名应表达控制律 gain，而不是几何事实：

```text
BEV_CONTROL_MODEL.LATERAL_OFFSET_TO_WHEEL_DELTA_GAIN
BEV_CONTROL_MODEL.HEADING_ERROR_TO_WHEEL_DELTA_GAIN
BEV_CONTROL_MODEL.CURVATURE_TO_WHEEL_DELTA_GAIN
```

可保留旧参数名做兼容迁移，但必须写清：

```text
旧 LATERAL_ERROR_TO_WHEEL_DELTA_GAIN:
  只能迁移为 LATERAL_OFFSET_TO_WHEEL_DELTA_GAIN 的兼容别名；
  不得继续表达 weighted future lateral average 的主控语义。
```

第一版不新增 forward window / anchor 参数。若必须保留扩展点，应只作为后续可选参数，避免为了规避弯道双重计算而引入模糊“局部”语义。

需要更新的 telemetry / media / assistant 面：

```text
new/code/runtime/control_debug_snapshot.hpp
new/code/runtime/control_debug_reporter.cpp
new/code/platform/steering_media_protocol.hpp
new/code/platform/steering_media_protocol.cpp
new/code/runtime/steering_media_service.cpp
new/code/platform/assistant_protocol.hpp
new/code/platform/assistant_protocol.cpp
new/user/scene_overlay_probe.cpp
```

公开事实应新增：

```text
tracking_geometry.computed
tracking_geometry.lateral_offset_m
tracking_geometry.heading_error_rad
tracking_geometry.curvature_m_inv
tracking_geometry.sample_count
tracking_geometry.reason

yaw_control.lateral_term
yaw_control.heading_term
yaw_control.curvature_term
yaw_control.turn_output_target
```

旧 telemetry：

```text
lateral_error.weighted_lateral_error_m
```

可以短期保留用于对照，但显示层必须能区分它不是 V6 主控事实。

## 14. 测试要求

单测应覆盖：

```text
直线路径：
  lateral_offset 接近路径横向截距；
  curvature 接近 0。

纯偏移直线：
  curvature 接近 0；
  lateral_offset 非 0；
  控制输出来自 lateral correction。

弯曲路径：
  lateral_offset 由拟合/窗口策略确定，允许携带部分前向路径形状；
  curvature 非 0；
  控制输出按 lateral / curvature 两个 gain 分别生效。

样本不足：
  tracking_geometry.computed=false；
  readiness 不允许 reference control。

同一条路径来源切换：
  line / circle / cross candidate 只改变 reference path，不改变 tracking geometry helper 行为。
```

集成测试应覆盖：

```text
Control debug snapshot 中能同时看到 tracking geometry 三项和 turn term 分解；
旧 visual reference arbitration 不因 V6 改变优先级；
wheel target mixer 仍只消费 applied_turn_output；
PWM 层不出现 curvature / lateral / heading 相关依赖。
```

建议测试文件调整：

```text
new/verification/tests/reference_usability_lateral_error_test.cpp
  可拆分或改名，避免继续把 lateral_error 当成主控事实。

新增或改造：
  reference_tracking_geometry_test
  steering_yaw_controller_tracking_geometry_test
  reference_control_readiness_tracking_geometry_test
```

测试脚本和 CMake 目标需要同步加入新的 helper 源文件，避免只在主程序中编译通过而测试漏编。

## 15. 不变量

V6 落地后必须满足：

```text
lateral_offset / heading_error / curvature 是同层 reference tracking geometry 事实；
同层级不等于并列模块，也不等于必须同权相加；
控制律只有一个，不拆成 curvature controller 和 lateral controller 两套互相竞争的模块；
path generator 不知道控制律；
selector 不知道控制律；
wheel mixer 不知道 reference geometry；
PWM 层不知道曲率；
旧 weighted_lateral_error 不再作为弯道主控事实。
```

最终目标：

```text
直道偏移时，lateral correction 能把车拉回参考线；
弯道 / 环岛中，curvature feedforward 能提前产生合理转向；
heading feedback 可作为后续稳定项自然接入；
路径来源变化不影响控制层架构。
```
