# Actuator V8 统一执行器边界与无刷电调实现记录

状态：已按当前工作树实现。本文档记录 2026-05-31 对“原有电机 PWM 模块 + 新增无刷电调模块”的边界合并结论和落地点。V8 不调整控制参数，不改变视觉、reference、yaw、safety gate 的语义；V8 聚焦执行器边界的统一，避免原有电机与无刷电调形成两套生命周期、两套命令合同、两套 apply 标准。

本文档继承当前工程的“互不知晓”和“大道至简”原则：

```text
control loop 只产出本周期统一执行器命令；
actuator adapter 只负责统一生命周期和统一 apply；
各硬件输出 backend 只读取自己负责的字段；
motor output 不知道 brushless output；
brushless output 不知道 motor output；
上游 reference / yaw / safety 不知道硬件细节。
```

## 1. 本次语义结论

无刷电调不是一个独立控制链，也不是一个单独生命周期模块。现场需求是：

```text
motor 的生命周期和无刷电调完全一致；
需要输出电机 PWM 的地方，同时也需要输出无刷电调 PWM；
二者应随同一 startup / control tick / stop / shutdown 进入同一执行器语义；
不能维护两套 apply 成败标准。
```

因此 V8 的核心结论是：

```text
不要新增 IBrushlessEscAdapter 作为第二套平台生命周期；
不要让 ControlLoop 分别调用 motor->Apply() 和 esc->Apply()；
不要新增 BrushlessEscCommand 作为第二套命令合同；
不要用 ActuatorCoordinator 在 control loop 末端做双分发。
```

正确边界是把当前事实上的 `motor` 执行器边界提升为统一 `actuator` 边界：

```text
ControlLoop
-> ActuatorCommandBuilder
-> port::ActuatorCommand
-> IActuatorAdapter::Apply(command)
   ├─ DifferentialMotorOutput
   └─ BrushlessEscOutput
```

对上游只有一个执行器系统；对下游有多个硬件输出通道。

## 2. 统一 Actuator 链路

V8 完成后的执行器链路包含以下层次：

```text
hardware_profile.json
-> HardwareProfile::actuator
-> ParamStore::LoadHardwareProfile()
-> PlatformBundle::actuator
-> MakeActuatorAdapter()
-> RunStartup(): actuator Initialize + RequireReady
-> ControlLoop::Start(): actuator Ready
-> ControlLoop::Tick(): wheel target -> wheel PID -> ActuatorCommand
-> actuator->Apply(command)
   ├─ true_ls2k0300::ApplyMotorCommand(left_drive_pwm, right_drive_pwm)
   └─ true_ls2k0300::ApplyBrushlessEscCommand(left_brushless_pwm, right_brushless_pwm)
-> RunShutdown(): actuator Disable + Shutdown
```

关键事实：

```text
port::ActuatorCommand 已经是控制层向执行器层发送的唯一命令类型；
port::IActuatorAdapter 包含 Initialize / Apply / Disable / Shutdown / Ready 生命周期；
RunStartup() 以 actuator 为 required adapter；
ControlLoop 每个可驱动 tick 只在一个位置生成 PWM 并调用 Apply；
RunShutdown() 先 Disable 再 Shutdown；
ActuatorAdapter 处理 emergency_stop、apply 失败回滚、ready 状态和诊断输出；
true_ls2k0300 bridge 只负责设备路径、方向归一化和实际写入。
```

V8 复用这条链路的结构，不复制出另一条 esc 链路。

## 3. 统一边界命名

最终有效命名：

```text
IActuatorAdapter
MakeActuatorAdapter
PlatformBundle::actuator
HardwareProfile::actuator
actuator_adapter.cpp
ActuatorCommandBuilder
```

命名调整的原因：

```text
当前边界已经不再只控制“左右轮电机”；
它将同时控制左右差速电机 PWM 和无刷电调 PWM；
继续命名为 motor 会让后续代码误以为 brushless 是外接例外；
统一改名为 actuator 后，后续所有执行器输出都共享同一标准。
```

实现直接落到 `actuator` 边界，不保留 `motor` 与 `actuator` 两套入口，不引入机械转发层。

## 4. 统一命令合同

`port::ActuatorCommand` 是唯一命令合同。V8 将其从“左右轮 PWM 命令”提升为“本周期全部执行器输出命令”：

```cpp
struct ActuatorCommand {
    int left_drive_pwm = 0;
    int right_drive_pwm = 0;
    int left_brushless_pwm = 0;
    int right_brushless_pwm = 0;
    bool emergency_stop = true;
};
```

字段语义：

```text
left_drive_pwm:
  左侧差速驱动输出，有符号，正值沿现有逻辑前进方向。

right_drive_pwm:
  右侧差速驱动输出，有符号，正值沿现有逻辑前进方向。

left_brushless_pwm:
  左无刷电调输出 PWM，输出到 P828 对应的 ESC1。

right_brushless_pwm:
  右无刷电调输出 PWM，输出到 P829 对应的 ESC2。

emergency_stop:
  true 表示本周期所有执行器输出必须进入安全态。
```

不新增 `BrushlessEscCommand`。原因：

```text
无刷电调与原 motor 生命周期完全一致；
输出时机完全一致；
成败标准完全一致；
新增第二个 command 会形成第二套标准，并让 telemetry / tests / fail-safe 漂移。
```

## 5. 命令构造层

当前命令构造层提升为统一执行器命令构造层：

```text
ActuatorCommandBuilder::Compose(...)
```

输入：

```cpp
port::ActuatorCommand Compose(int left_drive_pwm,
                              int right_drive_pwm,
                              int left_brushless_pwm,
                              int right_brushless_pwm,
                              bool emergency_stop,
                              int drive_pwm_limit,
                              int brushless_pwm_limit) const;
```

输出：

```cpp
port::ActuatorCommand
```

职责：

```text
构造唯一 ActuatorCommand；
按参数合同限幅各输出字段；
保持 emergency_stop 的 fail-safe 默认；
不调用硬件；
不读取 profile；
不读取 vision / reference / yaw；
不决定 startup / shutdown。
```

`ControlLoop` 的 PWM 生成阶段保持原结构：

```text
wheel_targets = WheelTargetMixer::Compute(...)
left_drive_pwm = left_wheel_pid.Compute(...)
right_drive_pwm = right_wheel_pid.Compute(...)
left_brushless_pwm / right_brushless_pwm = actuator brushless command source
command = ActuatorCommandBuilder::Compose(...)
```

左右无刷输出保持显式输入。`ActuatorCommand` 字段默认仍为 0；当前 runtime 调试默认通过 `brushless_debug_fixed_pwm_enabled=1`、`brushless_debug_fixed_pwm=600` 在 control loop 命令构造点给左右无刷通道写入固定 PWM。若后续让无刷输出与当前速度目标同步，映射必须同时产出 left/right 两个通道，且只消费控制周期中已有的运动状态或生效速度目标，不读取视觉内部事实。固定输出必须通过显式 runtime 参数或 bench/env 参数进入 command，而不是在 adapter 内隐藏生成。

## 6. 统一 Adapter 内部结构

对外只有一个接口：

```cpp
class IActuatorAdapter {
public:
    virtual ~IActuatorAdapter() = default;
    virtual bool Initialize(const HardwareProfile& profile,
                            DiagnosticSink& diagnostics) = 0;
    virtual bool Apply(const ActuatorCommand& command,
                       DiagnosticSink& diagnostics) = 0;
    virtual void Disable(DiagnosticSink& diagnostics) = 0;
    virtual void Shutdown(DiagnosticSink& diagnostics) = 0;
    virtual bool Ready() const = 0;
};
```

`ActuatorAdapter` 内部直接调用两个后端 bridge 函数，不引入第二层 output 抽象：

```text
Differential motor bridge:
  只读取 left_drive_pwm / right_drive_pwm / emergency_stop。
  复用现有 true_ls2k0300 motor bridge 逻辑。

Brushless ESC bridge:
  只读取 left_brushless_pwm / right_brushless_pwm / emergency_stop。
  负责无刷电调设备路径、PWM 写入、安全态输出。
```

互不知晓约束：

```text
DifferentialMotorOutput 不知道 BrushlessEscOutput；
BrushlessEscOutput 不知道 DifferentialMotorOutput；
二者都不知道 ControlLoop、yaw、reference、safety gate；
ActuatorAdapter 不重新计算 PWM，只分发 ActuatorCommand。
```

## 7. Apply 成败与安全语义

V8 只保留一个 apply 标准：

```text
ActuatorAdapter::Apply(command) 返回 true:
  本周期所有启用的 actuator output 均接受命令。

ActuatorAdapter::Apply(command) 返回 false:
  至少一个启用 output 拒绝命令，整体执行器施加失败。
```

失败处理：

```text
任一路 Apply 失败：
  ActuatorAdapter 立即 best-effort Disable 所有 output；
  ready_ 置 false；
  发出统一 actuator.apply.failed 诊断；
  返回 false。

command.emergency_stop == true：
  ActuatorAdapter 对所有 output 执行安全态输出；
  所有 output 安全态成功才返回 true。

Disable():
  对所有启用 output 执行安全态输出；
  任一路失败只影响 ready_ 和诊断，不阻止继续 disable 其他 output。

Shutdown():
  先 Disable；
  再释放所有 output 资源；
  ready_ 置 false。
```

这保证 control loop 只判断一个 `apply_ok`，不会出现 motor 成功、brushless 失败但上游仍认为 actuator armed 的漂移。

## 8. Profile 合同

当前 profile：

```json
"actuator": {
  "mode": "direct-match",
  "hook": "differential-motor-plus-brushless-esc"
}
```

当前不保留：

```json
"motor": { ... },
"brushless_esc": { ... }
```

原因：

```text
motor 与 brushless 生命周期一致；
输出时机一致；
启停失败标准一致；
拆成两个 profile block 会诱导两个 RequireReady、两个 diagnostics-only 分支和两个 fail-safe 解释。
```

`actuator.hook` 可选择具体硬件组合，但不能改变上层生命周期：

```text
differential-motor-only
differential-motor-plus-brushless-esc
diagnostics-disabled
```

当前实现只支持 `differential-motor-plus-brushless-esc`，未知 hook 必须 fail-closed。

## 9. 参数合同

V8 不通过修改现有控制参数掩盖硬件接入。新增无刷输出所需参数应只表达 actuator 输出物理约束：

```text
brushless_pwm_limit
left_brushless_pwm_floor / right_brushless_pwm_floor
left_brushless_pwm_neutral / right_brushless_pwm_neutral
brushless_pwm_step_limit
```

命名必须明确属于 actuator 输出层，不属于 yaw、reference 或 wheel PID。

第一版约束：

```text
不修改 RUNNING_SPEED_TARGET；
不修改 yaw gains；
不修改 wheel PID；
不修改 raw_turn_output_limit；
不让 brushless adapter 内部从速度目标私自推导 PWM；
所有 brushless PWM 生成逻辑必须在命令构造层可见。
```

若左右 brushless PWM 与 effective_speed_target 的关系需要参数化，应作为单独映射参数进入 builder，例如：

```text
ACTUATOR.LEFT_BRUSHLESS_PWM_PER_SPEED_TARGET
ACTUATOR.RIGHT_BRUSHLESS_PWM_PER_SPEED_TARGET
ACTUATOR.LEFT_BRUSHLESS_PWM_BASE
ACTUATOR.RIGHT_BRUSHLESS_PWM_BASE
```

命名以最终参数体系为准，但 owner 必须是 actuator command builder，不是硬件 bridge。

## 10. Telemetry 与 Debug

telemetry 应围绕统一 `ActuatorCommand` 展开：

```text
raw_turn_output
applied_turn_output
left_drive_pwm_command
right_drive_pwm_command
left_brushless_pwm_command
right_brushless_pwm_command
actuator_apply_outcome
```

debug 不参与决策：

```text
debug 只序列化最终命令和 apply 结果；
debug 不重新计算左右 brushless PWM；
debug 不根据左右 brushless 状态改变 safety gate；
debug 不反向影响 motion supervisor。
```

## 11. Bench 与现场测试入口

现有 bench PWM pulse 测试也应对齐统一命令：

```text
LS2K_BENCH_DRIVE_LEFT_PWM  -> left_drive_pwm
LS2K_BENCH_DRIVE_RIGHT_PWM -> right_drive_pwm
LS2K_BENCH_LEFT_BRUSHLESS_PWM  -> left_brushless_pwm
LS2K_BENCH_RIGHT_BRUSHLESS_PWM -> right_brushless_pwm
```

bench 仍通过 `platform.actuator->Apply(command)` 输出，不直接访问 motor bridge 或 brushless bridge。

bench 的低电压保护保持不变：

```text
low voltage emergency active:
  不允许输出任一执行器通道；
  返回 blocked diagnostics；
  不单独绕过 brushless。
```

## 12. 实施步骤

### Step 1：统一命令类型

```text
修改 port::ActuatorCommand 字段；
更新所有左右驱动 PWM 读写点；
补充 left_brushless_pwm / right_brushless_pwm 字段默认值为 0；
保证 emergency_stop 默认仍为 true。
```

验证点：

```text
现有 wheel_target_mixer_test 不受影响；
actuator command builder 测试更新到新字段；
telemetry 输出 raw_turn_output / applied_turn_output / 四路执行器命令 / actuator_apply_outcome。
```

### Step 2：命令构造层改名与扩展

```text
ActuatorCommandBuilder 是唯一命令构造层；
Compose() 一次性产出 left_drive_pwm / right_drive_pwm / left_brushless_pwm / right_brushless_pwm；
drive PWM 限幅和 brushless PWM 限幅都在 builder 明确发生；
不在 adapter 内隐藏限幅。
```

验证点：

```text
emergency_stop 返回全零安全命令；
left/right drive PWM 限幅保持原行为；
left/right brushless PWM 限幅独立覆盖；
ActuatorCommand 字段默认 0；runtime 调试默认用显式参数给 left/right brushless PWM 固定 600。
```

### Step 3：Adapter 边界改名

```text
IActuatorAdapter 是唯一平台执行器接口；
PlatformBundle 只暴露 actuator；
平台构造只调用 MakeActuatorAdapter()；
执行器实现落在 actuator_adapter.cpp。
```

验证点：

```text
RunStartup 只检查 platform.actuator；
ControlLoop::Start 只检查 actuator Ready；
ControlLoop::Tick 只调用 actuator Apply；
RunShutdown 只调用 actuator Disable / Shutdown。
```

### Step 4：内部输出 backend

```text
把现有 true_ls2k0300 motor 调用封装成 DifferentialMotorOutput；
新增 BrushlessEscOutput；
ActuatorAdapter 初始化所有启用 output；
ActuatorAdapter Apply 同周期分发统一 command。
```

验证点：

```text
motor output apply 失败时，brushless output 被 best-effort disable；
brushless output apply 失败时，motor output 被 best-effort disable；
emergency_stop 对两个 output 都生效；
disable 对两个 output 都生效。
```

### Step 5：Profile 与启动链

```text
hardware_profile.json 使用 actuator block；
ParamStore 解析 actuator block；
ValidateProfileContracts 校验 actuator direct-match；
main 输出 profile.actuator diagnostics。
```

验证点：

```text
缺 actuator block fail-closed；
未知 actuator mode fail-closed；
未知 actuator hook fail-closed；
disabled actuator 只在 degraded startup 下允许 diagnostics-only。
```

### Step 6：ControlLoop 输出左右 brushless PWM

```text
在现有 PWM 输出位置构造 left_brushless_pwm / right_brushless_pwm；
左右 brushless PWM 与 left/right drive PWM 一起进入 ActuatorCommandBuilder；
不新增第二个 apply；
不新增第二个 apply_ok。
```

验证点：

```text
gate veto 时 command.emergency_stop 或安全零输出覆盖所有通道；
hold disarmed 时左右 brushless PWM 为安全值；
stopping 阶段左右 brushless PWM 按 actuator 输出规则收敛到安全值；
no-motion 时左右 brushless PWM 不输出驱动值。
```

### Step 7：Telemetry 与媒体协议

```text
ControlDebugSnapshot 增加 left_brushless_pwm_command / right_brushless_pwm_command；
AssistantTelemetryView 增加 left_brushless_pwm_command / right_brushless_pwm_command；
steering media header / snapshot 增加统一 actuator 输出字段；
assistant telemetry 以顶层 raw_turn_output / applied_turn_output / 四路执行器命令 / actuator_apply_outcome 表达统一事实；
steering media 在 actuator 分组内表达 raw_turn_output / applied_turn_output / 四路执行器命令 / apply_outcome。
```

验证点：

```text
debug 中看到的左右 brushless PWM 与实际 ActuatorCommand 一致；
debug 不重新推导左右 brushless PWM；
旧调试脚本仍能读到左/右 drive PWM。
```

### Step 8：Bench 与硬件验证

```text
bench pulse 支持 left/right drive 与 left/right brushless 四路输入；
bench 仍走统一 actuator Apply；
低电压阻断仍覆盖所有通道；
bench summary 输出四路命令值和整体 apply_ok。
```

验证点：

```text
仅 left_drive_pwm 输出；
仅 right_drive_pwm 输出；
仅 left_brushless_pwm 输出；
仅 right_brushless_pwm 输出；
四路同时输出；
任一路失败整体 apply_ok=false。
```

## 13. 测试计划

新增或更新单元测试：

```text
actuator_command_builder_test:
  emergency_stop 默认安全；
  left/right drive PWM 限幅；
  left/right brushless PWM 限幅；
  command 字段默认 0，runtime 参数默认把左右 brushless 调试输出固定到 600。

actuator_adapter_test:
  initialize all outputs；
  apply success only when all outputs success；
  motor output failure disables brushless；
  brushless output failure disables motor；
  emergency_stop disables all outputs；
  shutdown disables all outputs。

hardware_profile_test / runtime_parameter_defaults_test:
  actuator profile 必填；
  actuator 参数默认值与 JSON 一致；
  未知 hook fail-closed。

control_loop actuator test:
  可驱动 tick 只调用一次 actuator Apply；
  gate veto 不调用 yaw，并输出统一安全命令；
  hold disarmed / stopping 覆盖左右 brushless PWM 安全态。
```

回归测试：

```text
run_wheel_target_mixer_test.sh
run_reference_tracking_geometry_test.sh
run_reference_usability_lateral_error_test.sh
run_runtime_parameter_defaults_test.sh
run_startup_low_voltage_order_test.sh
```

若新增 telemetry 字段触及 media / assistant：

```text
steering_media_selftest
assistant_protocol 相关测试
```

## 14. 不做的事

V8 不做以下改动：

```text
不新增独立 brushless lifecycle；
不新增 BrushlessEscCommand；
不新增第二套 Apply 结果；
不在 ControlLoop 里分别 apply motor 和 brushless；
不让 brushless adapter 内部生成控制 PWM；
不修改视觉链路；
不修改 reference/path；
不修改 yaw sign；
不修改 wheel PID 以适配无刷；
不通过调大/调小控制参数掩盖硬件接入问题；
不让 debug 或 telemetry 成为决策来源。
```

## 15. 验收标准

实现完成后，代码结构应满足：

```text
全工程只有一个执行器生命周期入口；
全工程只有一个 ActuatorCommand；
ControlLoop 每周期只有一次 actuator Apply；
RunStartup / RunShutdown 只面对 actuator；
左右 drive PWM 和左右 brushless PWM 在同一个命令中可见；
任一路硬件输出失败都会导致整体 apply 失败；
任一路失败都会触发所有输出 best-effort disable；
测试中不存在 motor 与 brushless 两套分离标准；
telemetry 显示的是统一命令事实，不是重新计算结果。
```

现场验证时重点看：

```text
no-motion 状态下左右 brushless PWM 是否保持安全值；
drive 状态下左右 brushless PWM 是否与本周期 actuator command 一致；
stop / emergency / low voltage 时 motor 与 brushless 是否同时进入安全态；
单路故障时另一输出是否被同步 disable；
assistant / steering media 中 apply outcome 是否只表达整体 actuator 结果。
```

## 16. 参考例程后的具体实现代码

本节只参考 `true_LS2K0300_Library/` 例程的底层实现事实，不参考其工程结构。

例程给出的事实：

```text
ESC 设备路径：
  /dev/zf_device_pwm_esc_1，对应左无刷电调 P828
  /dev/zf_device_pwm_esc_2，对应右无刷电调 P829

PWM 写入方式：
  向设备节点写入 uint16 duty。

安全关闭：
  对两个 ESC PWM 节点写 0。

50Hz ESC duty 约定：
  1ms / 20ms * 10000 = 500，对应 0%；
  2ms / 20ms * 10000 = 1000，对应 100%。
```

### 16.1 vendor path

落点：`new/code/platform/true_ls2k0300/vendor_paths.hpp`

```cpp
// 左无刷电调 PWM 控制字符设备路径（P828）
inline constexpr char kBrushlessEsc1PwmPath[] = "/dev/zf_device_pwm_esc_1";
// 右无刷电调 PWM 控制字符设备路径（P829）
inline constexpr char kBrushlessEsc2PwmPath[] = "/dev/zf_device_pwm_esc_2";
```

### 16.2 bridge 接口

落点：`new/code/platform/true_ls2k0300/bridge.hpp`

```cpp
// 初始化无刷电调 PWM 设备。探测两个 ESC PWM 节点，并写入安全态。
BridgeStatus InitializeBrushlessEsc();

// 施加左右无刷电调 PWM。
// left_brushless_pwm 输出到 P828，right_brushless_pwm 输出到 P829。
// duty 语义：0 表示关闭；500-1000 表示 0%-100% 油门区间。
BridgeStatus ApplyBrushlessEscCommand(int left_brushless_pwm, int right_brushless_pwm);

// 禁用无刷电调输出。向两个 ESC PWM 节点写 0。
BridgeStatus DisableBrushlessEscOutput();
```

### 16.3 bridge 实现

落点：`new/code/platform/true_ls2k0300/motor_bridge.cpp`

这段代码复用当前文件里已有的 `OpenWritable()`、`WriteBinary()` 和 `BridgeStatus`，不引入逐飞例程的 `zf_driver_pwm` 包装层：

```cpp
namespace {

constexpr int kBrushlessEscDutyMin = 0;
constexpr int kBrushlessEscDutyMax = 1000;

BridgeStatus ProbeBrushlessEscPath(const char* path) {
    BridgeStatus status{};
    if (!OpenWritable(path)) {
        status.detail = std::string("brushless ESC PWM resource unavailable: ") + path;
        return status;
    }
    status.ok = true;
    status.detail = path;
    return status;
}

BridgeStatus WriteBrushlessEscDuty(const char* path, int requested_duty) {
    const int clamped =
        std::clamp(requested_duty, kBrushlessEscDutyMin, kBrushlessEscDutyMax);
    const uint16_t duty = static_cast<uint16_t>(clamped);
    if (!WriteBinary(path, duty)) {
        return {false, std::string("brushless ESC PWM write failed: ") + path};
    }
    return {true, std::string("brushless ESC PWM duty applied: ") + path};
}

}  // namespace

BridgeStatus InitializeBrushlessEsc() {
    for (const char* path : {kBrushlessEsc1PwmPath, kBrushlessEsc2PwmPath}) {
        const BridgeStatus probe = ProbeBrushlessEscPath(path);
        if (!probe.ok) {
            return probe;
        }
    }
    return DisableBrushlessEscOutput();
}

BridgeStatus ApplyBrushlessEscCommand(int left_brushless_pwm, int right_brushless_pwm) {
    const BridgeStatus left = WriteBrushlessEscDuty(kBrushlessEsc1PwmPath, left_brushless_pwm);
    if (!left.ok) {
        const BridgeStatus rollback = DisableBrushlessEscOutput();
        if (!rollback.ok) {
            return {false, left.detail + "; rollback failed: " + rollback.detail};
        }
        return left;
    }

    const BridgeStatus right = WriteBrushlessEscDuty(kBrushlessEsc2PwmPath, right_brushless_pwm);
    if (!right.ok) {
        const BridgeStatus rollback = DisableBrushlessEscOutput();
        if (!rollback.ok) {
            return {false, right.detail + "; rollback failed: " + rollback.detail};
        }
        return right;
    }

    return {true, "brushless ESC command applied"};
}

BridgeStatus DisableBrushlessEscOutput() {
    const uint16_t zero = 0;
    if (!WriteBinary(kBrushlessEsc1PwmPath, zero)) {
        return {false, std::string("brushless ESC PWM write failed: ") + kBrushlessEsc1PwmPath};
    }
    if (!WriteBinary(kBrushlessEsc2PwmPath, zero)) {
        return {false, std::string("brushless ESC PWM write failed: ") + kBrushlessEsc2PwmPath};
    }
    return {true, "brushless ESC outputs disabled"};
}
```

### 16.4 统一命令字段

落点：`new/code/port/actuator_command_types.hpp`

直接使用最终字段顺序：

```cpp
struct ActuatorCommand {
    int left_drive_pwm = 0;
    int right_drive_pwm = 0;
    int left_brushless_pwm = 0;
    int right_brushless_pwm = 0;
    bool emergency_stop = true;
};
```

### 16.5 命令构造层

落点：`new/code/control/actuator_command_builder.hpp`

```cpp
port::ActuatorCommand Compose(int left_drive_pwm,
                              int right_drive_pwm,
                              int left_brushless_pwm,
                              int right_brushless_pwm,
                              bool emergency_stop,
                              int drive_pwm_limit,
                              int brushless_pwm_limit) const;
```

落点：`new/code/control/actuator_command_builder.cpp`

```cpp
port::ActuatorCommand ActuatorCommandBuilder::Compose(int left_drive_pwm,
                                                      int right_drive_pwm,
                                                      int left_brushless_pwm,
                                                      int right_brushless_pwm,
                                                      bool emergency_stop,
                                                      int drive_pwm_limit,
                                                      int brushless_pwm_limit) const {
    if (emergency_stop) {
        return {};
    }

    port::ActuatorCommand command{};
    command.left_drive_pwm = std::clamp(left_drive_pwm, -drive_pwm_limit, drive_pwm_limit);
    command.right_drive_pwm = std::clamp(right_drive_pwm, -drive_pwm_limit, drive_pwm_limit);
    command.left_brushless_pwm = std::clamp(left_brushless_pwm, 0, brushless_pwm_limit);
    command.right_brushless_pwm = std::clamp(right_brushless_pwm, 0, brushless_pwm_limit);
    command.emergency_stop = false;
    return command;
}
```

### 16.6 adapter 同周期双输出

落点：`new/code/platform/actuator_adapter.cpp`

通过 `actuator.hook` 选择硬件组合；当前直接支持 `differential-motor-plus-brushless-esc`：

```cpp
if (hook_name_ != "differential-motor-plus-brushless-esc") {
    ready_ = false;
    diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                      "actuator.init.unsupported_hook",
                      "unsupported actuator direct-match hook: " + hook_name_,
                      port::NowMs()});
    return false;
}
```

初始化：

```cpp
const true_ls2k0300::BridgeStatus motor_init = true_ls2k0300::InitializeMotor();
if (!motor_init.ok) {
    ready_ = false;
    diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                      "actuator.init.motor",
                      "motor backend unavailable: " + motor_init.detail,
                      port::NowMs()});
    return false;
}

const true_ls2k0300::BridgeStatus esc_init =
    true_ls2k0300::InitializeBrushlessEsc();
if (!esc_init.ok) {
    (void)true_ls2k0300::DisableMotorOutput();
    ready_ = false;
    diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                      "actuator.init.brushless_esc",
                      "brushless ESC backend unavailable: " + esc_init.detail,
                      port::NowMs()});
    return false;
}

ready_ = true;
```

施加命令：

```cpp
if (command.emergency_stop) {
    return DisableAllForApply(diagnostics, "actuator.emergency_stop.failed");
}

const true_ls2k0300::BridgeStatus motor_result =
    true_ls2k0300::ApplyMotorCommand(command.left_drive_pwm,
                                     command.right_drive_pwm);
if (!motor_result.ok) {
    DisableAfterFailure();
    ready_ = false;
    diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                      "actuator.apply.motor_failed",
                      motor_result.detail,
                      port::NowMs()});
    return false;
}

const true_ls2k0300::BridgeStatus esc_result =
    true_ls2k0300::ApplyBrushlessEscCommand(command.left_brushless_pwm,
                                            command.right_brushless_pwm);
if (!esc_result.ok) {
    DisableAfterFailure();
    ready_ = false;
    diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                      "actuator.apply.brushless_esc_failed",
                      esc_result.detail,
                      port::NowMs()});
    return false;
}
```

禁用：

```cpp
const true_ls2k0300::BridgeStatus motor_result =
    true_ls2k0300::DisableMotorOutput();
const true_ls2k0300::BridgeStatus esc_result =
    true_ls2k0300::DisableBrushlessEscOutput();
```

### 16.7 control loop 调用点

落点：`new/code/runtime/control_loop.cpp`

控制链不直接写 ESC 设备，只构造统一 command：

```cpp
const int left_drive_pwm =
    left_wheel_pid_.Compute(wheel_targets.left, encoder.left, params_.pwm_limit);
const int right_drive_pwm =
    right_wheel_pid_.Compute(wheel_targets.right, encoder.right, params_.pwm_limit);
const int brushless_pwm =
    params_.brushless_debug_fixed_pwm_enabled ? params_.brushless_debug_fixed_pwm : 0;

command = actuator_command_builder_.Compose(left_drive_pwm,
                                            right_drive_pwm,
                                            brushless_pwm,
                                            brushless_pwm,
                                            false,
                                            params_.pwm_limit,
                                            1000);
```

当前 runtime 控制链不从速度目标推导无刷 PWM，左右无刷输出均为 0；bench 可以单独验证 P828/P829 两个通道。现场确认映射后，再通过显式参数打开左右输出，不在 adapter 内隐藏推导。

### 16.8 bench 入口

落点：`new/user/main.cpp`

```cpp
const int left_drive_pwm = ReadIntEnv("LS2K_BENCH_DRIVE_LEFT_PWM", 0);
const int right_drive_pwm = ReadIntEnv("LS2K_BENCH_DRIVE_RIGHT_PWM", left_drive_pwm);
const int left_brushless_pwm = ReadIntEnv("LS2K_BENCH_LEFT_BRUSHLESS_PWM", 0);
const int right_brushless_pwm = ReadIntEnv("LS2K_BENCH_RIGHT_BRUSHLESS_PWM", left_brushless_pwm);

diagnostics.Emit({ls2k::port::DiagnosticLevel::kWarning,
                  "bench.pwm.start",
                  "running bench PWM pulse test with left_drive_pwm=" +
                      std::to_string(left_drive_pwm) +
                      " right_drive_pwm=" + std::to_string(right_drive_pwm) +
                      " left_brushless_pwm=" + std::to_string(left_brushless_pwm) +
                      " right_brushless_pwm=" + std::to_string(right_brushless_pwm) +
                      " pulse_ms=" + std::to_string(pulse_ms),
                  ls2k::port::NowMs()});

const ls2k::port::ActuatorCommand pulse = {
    left_drive_pwm,
    right_drive_pwm,
    left_brushless_pwm,
    right_brushless_pwm,
    false,
};

const bool apply_ok = platform.actuator->Apply(pulse, diagnostics);
```

bench 仍走同一个 `Apply()`，不直接访问 `ApplyBrushlessEscCommand()`。

### 16.9 CMake

如果只把 ESC bridge 实现放进现有 `motor_bridge.cpp`，`new/user/CMakeLists.txt` 不需要新增源文件。

若拆成独立文件：

```cmake
../code/platform/true_ls2k0300/brushless_esc_bridge.cpp
```

必须加入 `NEW_SRCS`，并保持 `bridge.hpp` 为统一声明入口。
