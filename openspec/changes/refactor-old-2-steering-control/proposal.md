## Why

当前 `new/` 的 steering 仍是近似普通赛道模型，既没有以 320x240 摄像头分辨率承接 `old_2` 的转向语义，也没有把环岛、斑马线、十字等特殊场景拆成清晰的独立逻辑模块。继续在现状上叠加修补会让感知、控制、参数面和调试脚本持续漂移，因此需要重写 change，把 old_2 语义迁入 `new/`，同时遵守当前 `turn_output` 差速末端和无 servo 的实际硬件条件。

## What Changes

- 将 steering 感知输入分辨率明确为 320x240，并同步更新受影响的 `legacy`、`runtime`、`platform`、`new/user` 调试脚本与验证工件。
- 将 `old_2` 的普通赛道、弯道、环岛入口/环内/出环、斑马线、十字等 steering 逻辑模块化迁入 `new/`，避免与直道逻辑混写在一个实现里。
- 保留当前最终执行器链路：新的 steering 输出仍然进入 `turn_output -> wheel_target_mixer -> wheel PID -> left/right PWM`，不得恢复直接舵机设备输出，也不得在新代码中继续引入 servo 命名字段。
- 在 `new/code/runtime/*` 中继续使用 runtime-owned legacy steering state 承接旧系统的场景标志、跨周期记忆、`highest_line` / `farthest_line` 上下文和 prior-cycle controller memory，而不是把 `old_2` 全局变量原样搬进 `new/`。
- scene-state 本次只实现直道、bend、circle、zebra、cross；roadblock 暂不移植绕行逻辑，但必须保留控制接口、状态占位、调试边界和后续可扩展边界。
- 更新 `RuntimeParameters`、`PerceptionResult`、`ControlDebugSnapshot::steering` 与 `control.steering_snapshot`：尽可能复用当前字段，删除无用字段，只新增实现与验收必需字段，不为了“对齐 old_2 名称”保留无意义字段，并在 change 文档中给出明确的 retained/removed/added 字段合同表。
- 同步更新 `new/user` 下调试程序、脚本和自测，使字段变更、调试输出和 steering 采集链路保持一致。
- 将 golden-frame、跨周期控制器测试、reset-path 测试、诊断面测试和 differential-drive 非回退证明纳入 source-first 验收面，使 assistant 关闭时仍可通过 `control.steering_snapshot` 与 host 侧脚本完成判定；reset 契约还必须明确给出 post-reset clean baseline。

## Capabilities

### New Capabilities
- `legacy-steering-perception-alignment`: 定义 320x240 输入下的 old_2 风格 steering 感知、阈值、边界、`highest_line`、`farthest_line`、中线/转向参考输出、模块化的普通赛道/场景入口，以及 assistant-disabled 可审查的感知输出契约。
- `legacy-steering-scene-state`: 定义 runtime-owned legacy steering state、环岛/斑马线/十字的场景状态、模块化切换与 steering 修正规则，并为 roadblock 保留接口但暂不实现绕行逻辑。
- `legacy-steering-control-alignment`: 定义 old_2 对齐的模糊 P、陀螺仪阻尼、控制器记忆与 reset、输出限幅、raw/applied turn 诊断面，以及向当前 `turn_output` 所有权模型的映射，不再使用 servo 命名字段。

### Modified Capabilities

本变更不修改现有主线 spec 名称，而是继续通过新的 capability spec 集合描述旧 steering 语义迁移。

## Risk Tier

- `STRICT`: 本变更同时重接 steering 感知分辨率、模块拆分、场景状态、跨周期控制语义、运行时 reset、调参与调试输出，并直接影响真实车辆的差速驱动输出和主机侧调试脚本。任何文档或实现偏差都可能导致行为失真或验收证据失效，因此必须以 STRICT 方式执行 docs-first/source-first 审核。

## Impact

- 参考真值源：`old_2/project/code/camera.cpp`、`old_2/project/code/camera.h`、`old_2/project/code/motor.cpp`、`old_2/project/code/motor.h`、`old_2/project/code/FUZZY_PID.cpp`、`old_2/project/code/FUZZY_PID.h`、`old_2/project/code/all_init.cpp`。
- 主要影响代码：`new/code/legacy/camera_logic.*`、模块化 scene/track steering 逻辑文件、`new/code/legacy/pid_control.*`、`new/code/legacy/fuzzy_pid_ucas.*`、`new/code/runtime/control_loop.*`、`new/code/runtime/runtime_state.hpp`、`new/code/runtime/control_debug_snapshot.hpp`、`new/code/port/control_types.hpp`、`new/code/platform/param_store.cpp`、相关测试与 golden fixtures。
- 主要影响调试链：`new/user/main.cpp`、`new/user/debug.sh`、`new/user/steering_media_capture.py`、`new/user/tune_steering.py`、`new/user/steering_media_selftest.cpp`、相关 README 与验证脚本。
- 主要影响运行时契约：320x240 摄像头输入、runtime-owned legacy scene-state 所有权、turn 输出计算、reset 语义、参数/调试字段裁剪与新增、assistant-disabled 验收路径。
- 相关验证与流程：`openspec-continue-change`、`openspec-align`、`openspec-artifact-verify`、`openspec-repair-change`、`openspec-apply-change`、`openspec-verify-change`。
