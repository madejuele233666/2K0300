## Context

当前 `new/code/legacy/camera_logic.cpp` 仍是近似普通赛道模型，且文档曾错误把新系统 steering 输入写成 160x120，并继续沿用 `mid_servo` 一类 servo 命名。实际项目中的摄像头分辨率是 320x240，实际执行器链也只有 `turn_output -> wheel_target_mixer -> wheel PID -> left/right PWM`，没有 servo 设备，因此本 change 必须同步修正感知输入、控制命名、场景模块划分、参数/调试字段和 `new/user` 下的脚本。

### Reference Alignment Inventory

目标动作：`adapt`

必须显式对齐的旧系统参考：

- `old_2/project/code/camera.cpp`
- `old_2/project/code/camera.h`
- `old_2/project/code/motor.cpp`
- `old_2/project/code/motor.h`
- `old_2/project/code/FUZZY_PID.cpp`
- `old_2/project/code/FUZZY_PID.h`
- `old_2/project/code/all_init.cpp`

当前承接模块：

- `new/code/legacy/camera_logic.*`
- 模块化 steering 逻辑文件（straight / bend / circle entry / circle interior / circle exit / zebra / cross）
- `new/code/legacy/pid_control.*`
- `new/code/legacy/fuzzy_pid_ucas.*`
- `new/code/runtime/control_loop.*`
- `new/code/runtime/runtime_state.hpp`
- `new/code/runtime/control_debug_snapshot.hpp`
- `new/code/port/control_types.hpp`
- `new/code/platform/param_store.cpp`
- `new/user/main.cpp`
- `new/user/debug.sh`
- `new/user/steering_media_capture.py`
- `new/user/tune_steering.py`
- `new/user/steering_media_selftest.cpp`

从参考系统提取出的关键契约：

- `old_2/project/code/camera.cpp` / `camera.h` 提供 steering 几何语义与场景分支真值源，但这些语义必须适配到新系统 320x240 输入，而不是把 160x120 误写成新系统输入分辨率。
- `old_2/project/code/motor.cpp` / `motor.h` 提供模糊 P、陀螺仪阻尼、跨周期记忆和特殊场景 steering 修正真值源，但新系统只能输出 `turn_output`，不能恢复 servo 终端。
- `old_2/project/code/FUZZY_PID.cpp` / `FUZZY_PID.h` 定义模糊 P 标定语义，而不是强制保留旧命名。
- `old_2/project/code/all_init.cpp` 定义 PID/fuzzy 初始化边界，说明 reset/init 契约仍然重要。
- `new/user` 下的调试脚本和自测程序直接消费 steering 字段，因此字段变更必须同步更新脚本与自测，而不是只改 C++ 结构体。

### Alignment Mapping

| Reference Module | Target Module | Action | Notes |
|---|---|---|---|
| `old_2/project/code/camera.cpp` 感知主链 | `new/code/legacy/camera_logic.*` | Adapt | 以 320x240 输入承接 old_2 steering 几何语义 |
| `old_2` 普通赛道/弯道/特殊场景逻辑 | 模块化 steering 逻辑文件 | Adapt | straight、bend、circle entry/interior/exit、zebra、cross 分模块实现 |
| `old_2` roadblock 绕行 | roadblock 接口与状态占位 | Partial adapt | 保留接口，本次不实现绕行控制 |
| `old_2/project/code/motor.cpp` steering 控制律 | `new/code/legacy/pid_control.*` | Adapt | 保留控制语义，但输出归属 `turn_output` |
| `old_2/project/code/FUZZY_PID.*` 模糊 P 标定 | `new/code/legacy/fuzzy_pid_ucas.*` | Adapt | 对齐标定语义，不要求保留旧字段名 |
| `old_2` 直接舵机终端 | `new/code/runtime/control_loop.*` | Reject direct copy | 明确禁止恢复 servo 终端 |
| steering 字段消费方 | `new/user/*.py` / `new/user/*.sh` / `new/user/*selftest*` | Update | 字段删改后同步脚本和自测 |

## Goals / Non-Goals

**Goals:**

- 将 steering 输入分辨率明确为 320x240，并同步修正文档、参数、调试脚本和验证面。
- 将 straight、bend、circle entry/interior/exit、zebra、cross 拆成独立 steering 逻辑模块，由主编排层调度。
- 保持当前 `turn_output` 差速末端，不恢复 servo 设备语义，也不在新代码中继续保留 servo 命名字段。
- 本次只实现 straight、bend、circle、zebra、cross；roadblock 仅保留控制接口与状态扩展边界。
- 在尽可能复用当前字段的前提下，删除无用字段，只新增实现与验收必需字段，并同步更新 `new/user` 调试程序和脚本。

**Non-Goals:**

- 将 160x120 作为新系统规范输入分辨率。
- 在 `PerceptionResult`、`RuntimeParameters`、`ControlDebugSnapshot::steering` 或脚本中继续引入 `servo` 命名字段。
- 本次实现 roadblock 绕行控制。
- 为了“兼容旧名字”而保留无用字段。
- 恢复 direct servo-device 输出或旧工程设备控制路径。

## Decisions

以下 decision 为先前已经确认的基础决策，继续保留；后续新增的 320x240、去 servo 化、模块化、roadblock 接口保留、字段裁剪与 `new/user` 同步等 decision，是对这些基础决策的补充约束，而不是替换。

### Decision: 由 `camera_logic` 统一承接 old_2 感知与特殊场景输入

问题：
当前 `new/code/legacy/camera_logic.cpp` 只能给出近似的普通赛道误差，无法完整表达 `old_2/project/code/camera.cpp` 中 steering 依赖的边界语义、`highest_line`、`farthest_line`、加权中线与特殊场景输入。

备选方案：

- 方案 A：在现有近似 `lateral_error` 模型上继续叠加 heuristics。
  - 优点：改动少。
  - 缺点：无法机械对齐 old_2，也难以证明 golden-frame 一致性。
  - 验证影响：差。
- 方案 B：把 old_2 `camera.cpp` 原样复制进 `new/`。
  - 优点：表面上更接近旧行为。
  - 缺点：会把旧全局状态、头文件耦合和设备假设直接带回 `new/`。
  - 验证影响：中等。
- 方案 C：在 `new/code/legacy/camera_logic.*` 中重写实现，但以 `old_2/project/code/camera.cpp` / `camera.h` 为契约参考，并把场景与边界上下文写入 runtime-owned state。
  - 优点：兼顾行为对齐与 `new/` 架构边界。
  - 缺点：需要同步扩展 `port/` 和 `runtime/` 输出面。
  - 验证影响：强。

选定方案：
方案 C。

Stack Equivalent：

- old_2 感知主链 -> `new/code/legacy/camera_logic.*`
- old_2 边界/中线/场景输入语义 -> `port::PerceptionResult` + runtime-owned steering state
- 感知编排层 -> `camera_logic` facade

Named Deliverables：

- `new/code/legacy/camera_logic.hpp`
- `new/code/legacy/camera_logic.cpp`
- `new/code/port/control_types.hpp`
- `new/code/runtime/runtime_state.hpp`
- `new/code/runtime/control_debug_snapshot.hpp`

Failure Semantics：

- 若只能输出近似误差而无法承接 old_2 关键感知语义，则 legacy 感知迁移未完成。
- 若特殊场景输入仍依赖裸全局而非 runtime-owned state，则架构边界未对齐。

Boundary Examples：

- 普通赛道：输出 normal-lane 所需感知上下文。
- 场景入口：输出 circle/zebra/cross 等模块需要的感知输入。

Contrast Structure：

- 采用：`camera_logic` 统一承接感知与场景输入。
- 不采用：继续依赖近似误差模型或复制 old_2 裸全局实现。

Verification Hook：

- golden-frame 感知输出比对
- `control.steering_snapshot` 感知字段与模块来源审查

### Decision: 由 runtime-owned legacy steering state 替代旧全局直接迁移

问题：
`old_2/project/code/camera.h` 使用大量全局状态携带场景阶段与跨周期上下文；`new/` 需要这些语义，但不能把旧全局模型直接引入当前 runtime。

备选方案：

- 方案 A：将旧全局逐个复制到 `new/code/legacy/*` 静态区。
  - 优点：实现快。
  - 缺点：生命周期与 reset 所有权失控。
  - 验证影响：差。
- 方案 B：把状态分散放进 `PerceptionResult` 临时字段。
  - 优点：接口简单。
  - 缺点：会把跨周期状态误建模成单帧结果。
  - 验证影响：差。
- 方案 C：将 scene flags、carry-over、边界上下文和 prior-cycle controller memory 归属到 `RuntimeState`，由现有 reset 路径统一复位。
  - 优点：符合 `new/` 生命周期所有权。
  - 缺点：需要定义更清晰的 state 结构和 reset 边界。
  - 验证影响：强。

选定方案：
方案 C。

Stack Equivalent：

- old_2 全局场景状态 -> `runtime::RuntimeState` 内 legacy steering state
- 旧 reset/init 边界 -> 现有 runtime controller reset path
- prior-cycle memory -> `W_Target_last` 与其他 steering memory fields

Named Deliverables：

- `new/code/runtime/runtime_state.hpp`
- `new/code/runtime/control_loop.cpp`
- 与 steering reset 相关的测试夹具

Failure Semantics：

- 若 fail-safe 或 stop 后状态未清理，则下一轮 steering 行为会被污染。
- 若 scene-state 只能在局部线程存在，则多周期控制与 runtime apply 无法可靠对齐。

Boundary Examples：

- `STOPPING -> DISARMED`：scene-state 与 controller memory 同步清零。
- 显式 reset：允许在不结束进程的前提下清理 legacy steering carry-over。

Contrast Structure：

- 采用：runtime-owned state。
- 不采用：legacy 静态全局或把跨周期状态塞进单帧结果。

Verification Hook：

- reset-path tests
- multi-cycle controller tests
- `control.steering_snapshot` 中的 state/source 字段

#### Post-Reset Clean Baseline

为避免 reset 契约只停留在“清零”表述，本 change 将 post-reset clean baseline 明确固定为：

- `highest_line=0`
- `farthest_line=0`
- `steering_reference_col=160`
- `active_module=straight`
- `scene_phase=idle`
- `scene_override_source=none`
- `roadblock_interface_state=supported_not_implemented`
- `last_special_scene_correction=none`
- roadblock `active=false`
- prior-cycle controller memory：
  - `W_Target_last=0`
  - `camera_error_last=0`
  - `gyro_error_last=0`
  - 与 turn 计算有关的积分/阻尼/滤波累积项全部为 `0`

首个 post-reset 控制周期期望：

- 若还没有新的有效感知输入，则 `raw_turn_output=0` 且 `applied_turn_output=0`
- 若已经有新的有效感知输入，则从上述零记忆状态重新计算，不得继承 reset 前任何一拍的 `scene_phase`、`scene_override_source`、`last_special_scene_correction` 或控制器记忆

### Decision: 将 old_2 steering 控制律迁入 `pid_control`，但保持当前差速驱动末端

问题：
`old_2/project/code/motor.cpp` 以旧 steering 终端为目标，而当前 `new/` 已接受的最终执行器链路是 `turn_output -> wheel_target_mixer -> wheel PID -> left/right PWM`。本 change 必须迁入 old_2 steering 语义，同时不能回退 actuator terminal。

备选方案：

- 方案 A：直接恢复旧 steering 终端路径，再在末端重新映射。
  - 优点：控制律更接近旧代码。
  - 缺点：违反现有运行时边界。
  - 验证影响：不可接受。
- 方案 B：保留当前简化 `pid_control`，只微调参数。
  - 优点：改动少。
  - 缺点：不能实现 old_2 控制语义对齐。
  - 验证影响：差。
- 方案 C：把 old_2 的模糊 P、camera PD、gyro damping、场景覆盖、输出限幅和 reset 语义迁入 `new/code/legacy/pid_control.*` 与 `new/code/legacy/fuzzy_pid_ucas.*`，但输出所有权仍归属当前 `turn_output`。
  - 优点：兼顾行为对齐与现有执行器边界。
  - 缺点：需要同步扩展参数与 debug 面。
  - 验证影响：强。

选定方案：
方案 C。

Stack Equivalent：

- old_2 steering 控制律 -> `new/code/legacy/pid_control.*`
- old_2 fuzzy 标定语义 -> `new/code/legacy/fuzzy_pid_ucas.*`
- actuator terminal -> runtime-owned `turn_output`

Named Deliverables：

- `new/code/legacy/pid_control.hpp`
- `new/code/legacy/pid_control.cpp`
- `new/code/legacy/fuzzy_pid_ucas.hpp`
- `new/code/legacy/fuzzy_pid_ucas.cpp`
- `new/code/runtime/control_loop.cpp`

Failure Semantics：

- 若控制律对齐完成但输出绕过 `turn_output` 直接驱动设备，则变更失败。
- 若 `raw_turn_output` / `applied_turn_output` 缺失，无法证明 clamp 行为，则验收必须阻塞。

Boundary Examples：

- 直道：camera + gyro 控制链生成 `turn_output`。
- 特殊场景：scene-state 覆盖普通赛道 steering，但末端仍走差速链。

Contrast Structure：

- 采用：迁移控制语义，保持差速末端。
- 不采用：恢复旧终端或只调当前简化参数。

Verification Hook：

- 多周期 controller fixture
- `control.steering_snapshot` 中的 raw/applied turn 与中间项
- differential-drive 非回退证明

### Decision: 扩展参数与调试面作为 steering 契约的一部分

问题：
`RuntimeParameters`、`PerceptionResult` 和 `ControlDebugSnapshot::steering` 既是实现接口，也是 steering 验收面。即使本次字段策略改成“删旧增必需”，参数与调试面仍必须被明确当作设计决策保留，而不能退回“内部细节”。

备选方案：

- 方案 A：只在实现里处理参数和调试字段，不在设计中单独约束。
  - 优点：文档更短。
  - 缺点：验收面会漂移。
  - 验证影响：差。
- 方案 B：保留“参数与调试面是 steering 契约的一部分”这一基础决策，再由后续 decision 细化字段裁剪原则。
  - 优点：基础边界稳定，后续字段策略可在其上收紧。
  - 缺点：文档层次更多。
  - 验证影响：强。

选定方案：
方案 B。

Stack Equivalent：

- steering calibration surface -> `port::RuntimeParameters`
- steering output/debug surface -> `port::PerceptionResult` + `runtime::ControlDebugSnapshot::steering`
- host-side consumers -> `new/user` scripts/tests

Named Deliverables：

- `new/code/port/control_types.hpp`
- `new/code/platform/param_store.cpp`
- `new/code/runtime/control_debug_snapshot.hpp`
- diagnostics/tests/host scripts

Failure Semantics：

- 若参数或调试面未被视作 change 的正式契约，字段漂移会直接破坏验收与调试流程。

Boundary Examples：

- 参数默认值、可选字段、调试快照字段都属于 review surface。
- host 侧 `new/user` 脚本必须与其同步。

Contrast Structure：

- 采用：把参数与调试面作为显式 steering 契约。
- 不采用：把它们视作可随意漂移的内部细节。

Verification Hook：

- diagnostics tests
- host script compatibility checks
- docs-first 对字段策略的审查

#### Steering Field Contract Table

以下字段合同表是本 change 的显式文档契约，而不是实现提示。`control.steering_snapshot` 必须镜像 `ControlDebugSnapshot::steering` 的公开字段集合；`new/user` 消费方必须按该表同步。

`RuntimeParameters`

| 字段 | 决策 | 说明 | 主要消费者 |
|---|---|---|---|
| `Speed_base` | Retain | 继续作为运行速度基准 | runtime/control |
| `see_max` | Retain | 保留为感知视野/搜索上界参数 | perception |
| `pid_turn_camera_p` | Retain | 保留 camera P 标定 | pid_control |
| `pid_turn_camera_d` | Retain | 保留 camera D 标定 | pid_control |
| `pid_turn_gyro_camera_p` | Retain | 保留 gyro P 标定 | pid_control |
| `pid_turn_gyro_camera_i` | Retain | 保留 gyro I 标定 | pid_control |
| `pid_turn_gyro_camera_d` | Retain | 保留 gyro D 标定 | pid_control |
| `P_Mode` | Retain | 保留 fuzzy calibration mode | fuzzy_pid_ucas |
| `camera_frame_width` | Retain | 继续存在，但本 change 固定规范输入为 `320` | platform/perception |
| `camera_frame_height` | Retain | 继续存在，但本 change 固定规范输入为 `240` | platform/perception |
| scene module enable/config fields | Add | 为 straight/bend/circle/zebra/cross 模块提供必需开关/阈值/子阶段常量 | perception/runtime |
| roadblock steering constants | Remove | 本次不实现绕行，不保留无用 roadblock 绕行常量 | none |
| servo-related public params | Remove | 新系统没有 servo，不保留 servo 命名参数 | none |

`PerceptionResult`

| 字段 | 决策 | 说明 | 主要消费者 |
|---|---|---|---|
| `published` | Retain | 保留发布态 | runtime |
| `fresh` | Retain | 保留新鲜度 | runtime |
| `emergency_veto` | Retain | 保留 veto 入口 | runtime |
| `low_voltage_veto` | Retain | 保留低压 veto | runtime |
| `threshold_veto` | Retain | 保留阈值 veto | runtime |
| `geometry_veto` | Retain | 保留几何 veto | runtime |
| `threshold` | Retain | 保留二值化阈值 | runtime/debug |
| `highest_line` | Retain | 保留 old_2 对齐几何语义 | runtime/debug |
| `lateral_error` | Retain | 保留为当前通用横向误差接口 | runtime/control |
| `farthest_line` | Add | old_2 对齐所必需 | runtime/control/debug |
| `steering_reference_col` | Add | 取代 `mid_servo` 的通用命名 | control/debug/scripts |
| `active_module` | Add | 标识 straight/bend/circle/zebra/cross 当前拥有权 | debug/scripts |
| `scene_phase` | Add | 标识 circle entry/interior/exit 等子阶段 | debug/scripts |
| `roadblock_interface_state` | Add | 标识 `supported_not_implemented` | debug/scripts |
| servo-named fields | Remove | 不引入 `mid_servo` 等字段名 | none |
| decorative compatibility fields | Remove | 不为兼容保留无意义字段 | none |

`ControlDebugSnapshot::steering` / `control.steering_snapshot`

| 字段 | 决策 | 说明 | 主要消费者 |
|---|---|---|---|
| `valid` | Retain | 保留有效标志 | debug |
| `frame_id` | Retain | 保留帧标识 | scripts |
| `capture_time_ms` | Retain | 保留时间戳 | scripts |
| `lateral_error` | Retain | 保留横向误差 | debug |
| `highest_line` | Retain | 保留几何语义 | debug |
| `threshold` | Retain | 保留阈值 | debug |
| `threshold_veto` | Retain | 保留阈值 veto | debug |
| `resolved_fuzzy_p` | Retain | 保留模糊 P 解析值 | debug/tests |
| `camera_p_term` | Retain | 保留 camera P 项 | debug/tests |
| `camera_d_term` | Retain | 保留 camera D 项 | debug/tests |
| `w_target` | Retain | 保留目标角速度/turn target 中间量 | debug/tests |
| `gyro_z` | Retain | 保留 gyro 测量 | debug/tests |
| `gyro_error` | Retain | 保留 gyro error | debug/tests |
| `gyro_p_term` | Retain | 保留 gyro P 项 | debug/tests |
| `gyro_d_term` | Retain | 保留 gyro D 项 | debug/tests |
| `raw_turn_output` | Retain | 保留 raw turn | runtime/debug |
| `applied_turn_output` | Retain | 保留 applied turn | runtime/debug |
| `farthest_line` | Add | 补齐 old_2 对齐所需 | debug/scripts |
| `steering_reference_col` | Add | 非 servo 命名的中线/转向参考列 | debug/scripts |
| `active_module` | Add | 当前模块拥有权 | debug/scripts |
| `scene_phase` | Add | 当前子阶段 | debug/scripts |
| `scene_override_source` | Add | 当前修正来源 | debug/scripts |
| `roadblock_interface_state` | Add | `supported_not_implemented` / future-ready | debug/scripts |
| `last_special_scene_correction` | Add | 说明当前是否有场景覆盖修正 | debug/tests |
| servo-named fields | Remove | 不保留 `mid_servo`、`servo_target` 等 | none |

`new/user` consumers

| 消费方 | 决策 | 说明 |
|---|---|---|
| `new/user/debug.sh` | Update | 读取/写入保留字段并适配新增字段，删除已移除字段引用 |
| `new/user/steering_media_capture.py` | Update | 解析新的 `control.steering_snapshot` 字段集合 |
| `new/user/tune_steering.py` | Update | 对齐新的快照字段、模块名和 scene phase |
| `new/user/steering_media_selftest.cpp` | Update | 按 retained/added 字段更新断言 |
| `new/user/main.cpp` | Update | 确保调试链启动和输出使用新字段契约 |

### Decision: 以 320x240 为 steering 规范输入，并由 `camera_logic` 统一编排

问题：
当前文档把 steering 输入写成 160x120，但实际项目摄像头与 `camera_frame_width/camera_frame_height` 约束都在 320x240。若 change 继续以 160x120 作为新系统规范输入，会让实现、参数和脚本都围绕错误假设展开。

备选方案：

- 方案 A：继续把 160x120 写成规范输入。
  - 优点：看起来更接近 old_2。
  - 缺点：与当前项目实际硬件和代码不符。
  - 验证影响：不可接受。
- 方案 B：以 320x240 为规范输入，在实现中承接 old_2 steering 语义。
  - 优点：符合现有项目事实。
  - 缺点：需要同步修正脚本、字段和 fixtures。
  - 验证影响：强。

选定方案：
方案 B。

Stack Equivalent：

- 320x240 capture -> `camera_adapter` / `camera_logic`
- old_2 几何语义 -> `PerceptionResult` 的非 servo 字段
- 感知模块编排 -> `camera_logic` facade + scene modules

Named Deliverables：

- `new/code/legacy/camera_logic.*`
- `new/code/port/control_types.hpp`
- `new/code/platform/param_store.cpp`
- `new/user` 调试脚本与自测

Failure Semantics：

- 文档或实现仍把 160x120 当成规范输入时，本 change 视为未对齐。
- 320x240 字段变更未同步到脚本或自测时，source-first 验收阻塞。

Boundary Examples：

- 320x240 直道帧进入感知链。
- 320x240 环岛入口帧触发 circle entry 模块。

Contrast Structure：

- 采用：320x240 规范输入 + 语义适配。
- 不采用：160x120 规范输入假设。

Verification Hook：

- 参数/默认值测试
- `new/user` 脚本与自测同步验证
- 320x240 golden-frame 夹具

### Decision: 去 servo 化字段与控制命名

问题：
当前 change 文档仍引用 `mid_servo` 等 servo 命名，但项目硬件实际上没有 servo，只有 `turn_output` 差速链。

备选方案：

- 方案 A：为了贴近 old_2 保留 servo 命名字段。
  - 优点：名称更像旧代码。
  - 缺点：误导新系统架构，污染参数与调试脚本。
  - 验证影响：差。
- 方案 B：把 old_2 对应语义映射为非 servo 命名字段，只保留对 `turn_output` 有意义的量。
  - 优点：符合当前系统真实结构。
  - 缺点：需要脚本和调试面同步改名。
  - 验证影响：强。

选定方案：
方案 B。

Stack Equivalent：

- old_2 `mid_servo` 语义 -> 通用中线/转向参考字段
- servo target -> `turn_output` reference
- servo diagnostics -> turn diagnostics

Named Deliverables：

- `new/code/port/control_types.hpp`
- `new/code/runtime/control_debug_snapshot.hpp`
- `new/code/legacy/pid_control.*`
- `new/user/steering_media_capture.py`
- `new/user/tune_steering.py`

Failure Semantics：

- 任何新字段仍以 `servo` 命名公开到 `new/` runtime surface 或脚本中，视为设计违规。
- 字段改名后脚本不同步，视为验收阻塞。

Boundary Examples：

- 快照中输出 `raw_turn_output` / `applied_turn_output` 与通用中线参考。
- 不再输出 `mid_servo`、`servo_target` 之类字段。

Contrast Structure：

- 采用：语义映射到 turn-centric 命名。
- 不采用：保留 servo 命名兼容层。

Verification Hook：

- `control.steering_snapshot` 字段检查
- `new/user` 解析脚本与 `steering_media_selftest`

### Decision: 场景逻辑必须拆模块，roadblock 只保留接口

问题：
straight、bend、circle、zebra、cross 若继续和直道逻辑混写，会让 steering 语义和 source-first 验收继续失真；同时 roadblock 本次并不打算实现完整绕行。

备选方案：

- 方案 A：继续把场景逻辑混在 `camera_logic` 或单个控制函数里。
  - 优点：修改快。
  - 缺点：不可维护，难验证。
  - 验证影响：差。
- 方案 B：分离 straight、bend、circle entry/interior/exit、zebra、cross 模块，roadblock 只保留接口。
  - 优点：边界清晰，可逐模块验证。
  - 缺点：文件和编排层会变多。
  - 验证影响：强。

选定方案：
方案 B。

Stack Equivalent：

- straight/bend/circle/zebra/cross -> dedicated scene modules
- roadblock -> reserved interface / placeholder state
- main orchestration -> `camera_logic` + runtime-owned state

Named Deliverables：

- straight/bend/circle/zebra/cross 模块文件
- roadblock interface/state placeholder
- 相关 fixtures/tests

Failure Semantics：

- 场景逻辑继续与直道代码混写，视为 change 未满足模块化目标。
- roadblock 若被文档描述为已实现绕行，则视为错误承诺。

Boundary Examples：

- straight 与 bend 各自独立模块。
- circle entry/interior/exit 分别建模块。
- roadblock 仅保留接口，不生成绕行 steering。

Contrast Structure：

- 采用：模块化场景逻辑。
- 不采用：单文件混写全部场景。

Verification Hook：

- 模块级测试与 golden-frame 分组
- debug 快照中的 module/source 标识

### Decision: 参数与调试面按“删旧增必需”原则更新，并同步 `new/user`

问题：
此前 change 文档偏向“尽量兼容 current surface”，但你明确要求不要为了兼容保留无用字段。

备选方案：

- 方案 A：尽量兼容，保留大多数字段。
  - 优点：短期改动少。
  - 缺点：无用字段继续扩散。
  - 验证影响：中等但噪声大。
- 方案 B：复用仍有价值字段，删除无用字段，只新增实现和验收必需字段，并同步所有消费方。
  - 优点：接口更干净。
  - 缺点：需要同步脚本、自测和文档。
  - 验证影响：强。

选定方案：
方案 B。

Stack Equivalent：

- useful current fields -> retained
- dead fields -> removed
- required legacy semantics -> new concise fields
- host consumers -> `new/user` scripts/tests

Named Deliverables：

- `new/code/port/control_types.hpp`
- `new/code/runtime/control_debug_snapshot.hpp`
- `new/code/platform/param_store.cpp`
- `new/user/debug.sh`
- `new/user/steering_media_capture.py`
- `new/user/tune_steering.py`
- `new/user/steering_media_selftest.cpp`

Failure Semantics：

- 为兼容保留无用字段，视为设计未落实。
- 字段变更未同步 `new/user` 消费方，source-first 阻塞。

Boundary Examples：

- 删除无意义兼容字段。
- 新增最小必需 scene/module/debug 字段。

Contrast Structure：

- 采用：删旧增必需。
- 不采用：刻意兼容保留无用字段。

Verification Hook：

- diagnostics tests
- host script parsing tests
- selftest / smoke script 更新验证

## Independent Verification Plan (STANDARD/STRICT)

本 change 的独立验证继续使用共享序列 `verify-sequence/default`：
`openspec/schemas/ai-enforced-workflow/verification-sequence.md`

共享契约：

- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
- `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`

统一编排权威：

- `openspec/schemas/modules/verification-cycle/orchestrator/CALLER-INTEGRATION.md`

Stage A 规则：

- docs-first checkpoint 主审面是本 change 的 `proposal/specs/design/tasks`
- docs-first review 同时必须纳入 `.codex/agents/verify-reviewer.toml`、共享 verification contracts 和 `CALLER-INTEGRATION.md`
- source-first checkpoint 主审面是改动代码、测试、直接受影响代码、`new/user` 调试脚本与自测
- `.index/` 仅是可选背景，不具备权威结论
- verifier 使用 `.codex/agents/verify-reviewer.toml`
- verifier 调用使用内建 subagent API，且 `fork_context=false`
- caller 必须按 `CALLER-INTEGRATION.md` 机械执行 `send_input` / `resume` / `spawn` / `repair` / `terminate`
- 只有同一 `active` agent 的 `block -> pass` 才能标记 `non_active`
- 终止条件只能是一个有效 `active` pass`

### Review Checkpoints

- artifact-completion docs-first review:
  - 主审面：`proposal/specs/design/tasks` + `.codex/agents/verify-reviewer.toml` + shared contracts + `CALLER-INTEGRATION.md`
  - findings: `openspec/changes/refactor-old-2-steering-control/verification/artifact-completion/attempt-<n>/findings.json`
  - evidence: `openspec/changes/refactor-old-2-steering-control/verification/artifact-completion/attempt-<n>/verifier-evidence.json`
  - agent table: `openspec/changes/refactor-old-2-steering-control/verification/artifact-completion/agent-table.json`
- checkpoint-1:
  - 主审面：320x240 perception port + straight/bend/circle/zebra/cross 模块结构 + roadblock 接口占位
- checkpoint-2:
  - 主审面：turn_output-oriented 控制迁移、reset 路径、参数与调试字段裁剪/新增
- checkpoint-3:
  - 主审面：完整 source-first change surface，包括 `new/user` 脚本、自测与直接受影响代码
- checkpoint-4:
  - 主审面：final fixture bundle、diagnostics、host script compatibility、differential-drive 非回退证明

## Migration Plan

- 2026-04-25 的 artifact-completion valid active pass 已满足 docs-first gate；在不再修改 `proposal/specs/design/tasks` 契约的前提下，实施直接从 source-first 阶段继续推进。
- 实现阶段先建立 320x240 perception 对齐与模块骨架，再迁移 control/runtime，再同步 `new/user` 调试脚本与自测。
- roadblock 本次仅保留接口；后续若要实现绕行，必须以新 change 扩展。

## Resolved Implementation Boundary

- straight 与 bend 必须保持独立模块文件、独立入口和独立编排边界。
- 允许共享无状态的底层边界/几何工具库，但不得通过共享实现重新退化为单文件混写。

## Risks / Trade-offs

- 最大风险是“文档改成 320x240，但实现和脚本仍按旧假设工作”，因此 `new/user` 同步必须进入主验收面。
- 去 servo 化会带来命名迁移成本，但这是避免新代码继续传播错误硬件假设的必要代价。
- 模块化会增加文件数量，但比继续把场景逻辑混在直道代码里更可维护、更可验证。
