# Phase C：感知鲁棒性与语义接口

## 1. 阶段目标

让系统从“低速可动”推进到“具备赛道视觉鲁棒性，并能承接赛道图片识别算法”。

这一阶段要解决两件事：

1. 现有循迹视觉不能过于挑环境
2. 未来图片识别算法必须有正式接入点

## 1A. 本阶段继承的感知与接口原则

本阶段承接以下原则：

1. 当前循迹主链路仍源自保留下来的 legacy camera logic，但不能把它当成最终比赛算法。
2. 视觉层必须同时承担“鲁棒循迹”和“语义扩展接口”两项职责。
3. 图片识别属于高层语义能力，必须独立于基础循迹误差计算存在。
4. 控制层不应直接理解识别模型细节，只应消费规范化后的策略输入。
5. 如果未来增加距离传感器，并且其作用是“识别特定情况后改变方向、速度或状态机”，那么它在架构上也属于高层语义/策略输入，而不是新的硬编码控制特例。

## 2. 当前代码中的感知结构

当前主感知链路是：

1. `new/code/runtime/perception_frontend.cpp`
2. `new/code/legacy/camera_logic.cpp`
3. `new/code/runtime/control_loop.cpp`

当前输出主要是：

- 阈值
- 横向误差
- 最高线
- `threshold_veto`
- `low_voltage_veto`
- `emergency_veto`

这套输出足以做基础循迹，但不足以承载后续“赛道图片识别 -> 策略决策”的需求。

此外，当前相机链路仍受以下运行时边界约束：

1. 期望分辨率由参数 `camera_frame_width/camera_frame_height` 给出，当前默认是 `320x240`
2. direct-match 路径只接受与该参数完全一致的灰度 frame，不做静默裁剪、缩放或居中拷贝
3. 非默认 `exp_light` 不能直接通过当前 direct-match 路径支持
4. 当前实现中，`LS2K_FORCE_UVC_GEOMETRY` 强制走非期望几何时，会发出 grep-facing 的 `camera.geometry.override`
5. 真实运行时捕获到与参数不一致的 frame 时，当前代码只会把 `CameraGeometryMarker` 置为 `kNonPhase1Geometry` 并拒绝发布帧；它不是独立 stable code
6. 因此，几何异常的现阶段取证口径必须区分：
   - 人工验证路径：看 `camera.geometry.override`
   - 真实运行拒绝路径：看 frame 未发布、后续 veto 以及内部几何 marker 语义，而不是假定存在独立 grep marker

## 2A. 当前代码的解耦缺口

当前代码并没有真正完成“可插拔语义输入”这一层，主要缺口是：

1. `port::PerceptionResult` 仍偏向基础循迹结果，不是通用策略输入。
2. `PerceptionFrontend` 目前主要处理 `camera + low voltage`，不是多源感知聚合器。
3. `ControlLoop` 仍直接消费 `perception + imu + encoder`，而不是 project-owned 的策略结果。

因此，Phase C 不能只加图片识别接口定义，还必须先把策略层边界补出来。

## 2B. 当前到未来的策略边界映射

当前 `ControlLoop` 直接消费的输入包括：

1. `PerceptionResult`
2. `ImuSample`
3. `EncoderDelta`
4. `LowVoltageSample`

其中直接参与 fail-safe veto 的信号至少包括：

1. `perception.published`
2. `perception.fresh`
3. `perception.emergency_veto`
4. `low voltage emergency`
5. `imu.valid`
6. `encoder.valid`

因此，后续如果引入 `StrategyInput` / `StrategyDecision`，必须先把 ownership 写清：

1. `StrategyInput`
   - 接收基础循迹结果
   - 接收图片识别结果
   - 接收未来距离判定结果
   - 接收与策略有关的 sensor health summary
2. `StrategyDecision`
   - 输出目标速度修正
   - 输出状态机迁移或临时动作建议
   - 输出是否允许控制继续解锁
   - 输出需要保留的 fail-safe veto 结论
3. 以下内容在当前阶段仍可留在 strategy 之外，但必须显式说明：
   - 电机 apply 失败后的执行器状态维护
   - 低层 actuator emergency-stop 落地

也就是说，未来控制层“只消费策略结果”并不意味着 fail-safe 消失，而是意味着 fail-safe 的上层判定要先被 project-owned 结构化。

## 2C. `old_2` 提供的高价值感知链

`old_2/` 对 Phase C 的价值非常高，因为它补上了“LS2K/Linux 平台下，赛道视觉到底是怎样串起来的”这一层证据。

从代码结构看，`old_2` 的相机与赛道链路至少包括：

1. `old_2/libraries/zf_device/zf_device_uvc.cpp`
   - 已经把图像输入切到 UVC/OpenCV 前端。
   - 说明 Linux 下的真实相机路径不是抽象讨论，而是历史上跑通过的一条链。
2. `old_2/project/code/camera.cpp`
   - 以轮廓、边界、断线统计、补线和中线生成为核心。
   - 包含角点、十字、斑马线、环岛、障碍、丢线等赛道元素检测与修复规则。
   - 不只是输出一个简单误差，而是维护一组可驱动状态机的中间状态。
3. `old_2/project/code/key.cpp` 与 `old_2/project/code/save.cpp`
   - 说明视觉阈值、曝光、模式和若干赛道相关参数在现场是频繁调整的，而不是编译期常量。

其中最值得当前迁移显式吸收的，不是单个 if/else，而是以下结构事实：

1. `old/` 更偏向“算出 `Err`，再送入控制”。
2. `old_2/` 更偏向“先根据左右边界、补线和场景规则求出 `mid_servo`，再使用 `Image_middle - mid_servo` 形成控制量”。
3. 这意味着真正高价值的迁移对象不是某个孤立误差公式，而是“中线生成链 + 场景状态链”。

当前 `new/code/legacy/camera_logic.cpp` 只保留了更简化的阈值/误差子集，因此它还不能代表 `old_2` 这一层的完整能力。

对当前工程来说，`old_2` 的吸收重点应明确分成三类：

1. 可直接转译为 project-owned 逻辑的算法资产
   - 边界提取
   - 丢线统计
   - 角点和补线修复
   - 中线生成
   - 十字/斑马线/环岛/障碍等状态判定
2. 只能作为平台参考的实现资产
   - UVC 取流细节
   - Linux 线程和显示调用
   - 厂库式 API 直接调用
3. 不应迁移的产物或噪声
   - `project/out/*`
   - `.vscode/*`
   - 构建产物和一次性调试残留

因此，Phase C 的正确目标不是“把 `old_2/camera.cpp` 整体抄进 `new/`”，而是：

1. 提炼 `mid_servo` 生成所依赖的 project-owned 中间量。
2. 把场景检测从 legacy 隐式全局改成可审计状态。
3. 把这些输出放进未来的 `StrategyInput`，而不是继续只传一个基础误差。

## 3. Phase C 任务清单

说明：

1. 本阶段出现的 `strategy.*` / `semantic.*` marker，默认指任务真正接入运行时之后的 grep-facing stable code。
2. 如果某个任务当前只要求完成接口、类型和 wiring，而运行命令只有 `build`，那么这些 marker 仍视为保留命名目标，不代表当前基线已经会输出它们。

### C-1 视觉鲁棒性增强

目标：
把基础循迹从“受控环境下可工作”推进到“更接近真实赛道可用”。

任务：

1. 替换或增强当前全局阈值策略：
   - 分区阈值
   - 局部阈值
   - 亮度归一化
2. 参考 `old_2/project/code/camera.cpp` 的边界扫描、断线统计、补线和角点修复方式，增加更稳定的边界/中线提取链。
3. 增加感知置信度。
4. 增加短时丢线恢复机制。
5. 明确哪些中间量需要长期保留，例如左右边界有效长度、line-loss 统计、corner/cross/zebra 候选状态，而不是只保留最终误差。

涉及代码：

- `new/code/legacy/camera_logic.cpp`
- `new/code/runtime/perception_frontend.cpp`
- `new/code/port/control_types.hpp`

验收标准：

1. 在多类光照条件下，误差曲线连续且可解释。
2. 单帧扰动不会立刻导致控制大幅跳变。

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-c-vision-smoke.log ./run_remote_smoke.sh)
```

预期 marker：

- `camera.init`
- `control.veto`

证据文件：

- `phase-c-vision-smoke.log`
- `phase-c-vision-before-after.md`
- `phase-c-dataset-index.md`

失败回退方向：

- 感知仍明显挑环境：留在 Phase C

### C-2 赛道元素状态回收

目标：
把旧工程里仍然有价值的赛道元素状态从占位字段恢复成真正可消费的策略输入。

任务：

1. 明确是否恢复：
   - 环岛
   - 十字
   - 斑马线
   - 其他旧工程中的赛道状态
2. 如果恢复，定义状态机进入、退出和对控制的影响。
3. 将 `circle_find` / `zebra_flag` / `cross_flag` 从占位状态变成真实运行状态。
4. 优先参考 `old_2/project/code/camera.cpp` 中已经存在的规则链，而不是凭空重新发明状态名称和进入条件。

涉及代码：

- `new/code/runtime/runtime_state.hpp`
- `new/code/legacy/camera_logic.cpp`
- `new/code/runtime/control_loop.cpp`

验收标准：

1. 赛道元素逻辑是代码里真实存在的，而不是只在状态结构里占位。
2. 元素状态变化能驱动控制策略变化。

运行命令：

```bash
(cd new/user && ./build.sh)
(cd new/user && mkdir -p ../verification && VERIFY_LOG_PATH=../verification/phase-c-track-elements.log ./run_remote_smoke.sh)
```

预期 marker：

- `strategy.track_element.transition`
- `control.start`

证据文件：

- `phase-c-track-elements.log`
- `phase-c-track-elements.md`
- `phase-c-strategy-mapping.md`

失败回退方向：

- 元素状态机与控制耦合不清：留在 Phase C

### C-3 为图片识别算法建立独立接口

目标：
让未来的图片识别算法成为“可插拔的语义模块”，而不是破坏现有循迹主干。

任务：

1. 设计独立的语义识别结果结构，例如：
   - `SceneRecognitionResult`
   - `SemanticObservation`
2. 明确语义结果的最小字段：
   - 是否有效
   - 识别类别
   - 置信度
   - 生效时间
   - 对控制的建议动作
3. 设计 project-owned 的策略输入/输出结构，例如：
   - `StrategyInput`
   - `StrategyDecision`
4. 在控制层增加“只消费策略结果”的接口，但初期可不启用全部功能。
5. 明确语义结果影响控制的方式：
   - 改速度目标
   - 改状态机
   - 改路线决策
   - 临时减速或等待
6. 为 `old_2` 风格的 `mid_servo` / scene-state 输出预留 project-owned 接口，避免后续只能把它们塞回 `camera_logic.cpp` 的内部局部变量。

建议结构：

1. 低层感知层：
   - 输出循迹误差、边界、置信度
2. 语义识别层：
   - 输出赛道图片识别结果
3. 距离判定层：
   - 输出距离传感器触发的特定场景判定
4. 策略层：
   - 合并循迹与语义，决定速度/方向/状态切换
   - 合并 freshness、sensor-validity、semantic veto 等上层是否允许运动的判定
5. 控制层：
   - 消费最终策略，不直接理解图片语义细节
   - 仅保留执行器 apply 与 emergency-stop 落地职责

验收标准：

1. 图片识别算法可以独立接入，不需要重写 `camera_logic.cpp` 的主干。
2. 控制层只消费规范化结果，不直接耦合识别模型细节。
3. `StrategyDecision` 至少明确保留：
   - 速度目标或速度修正
   - 转向/路线建议
   - 状态机迁移
   - fail-safe / allow-motion 结论

触达文件：

- `new/code/port/control_types.hpp`
- `new/code/runtime/control_loop.cpp`
- `new/code/runtime/perception_frontend.cpp`

运行命令：

```bash
(cd new/user && ./build.sh)
```

预期 marker（接口接入运行时后）：

- `semantic.image.decision`

证据文件：

- `phase-c-image-semantics.md`
- `phase-c-strategy-interface.md`

失败回退方向：

- 接口仍污染控制主干：留在 Phase C

### C-3A 为未来距离传感器建立统一扩展接口

目标：
确保未来新增距离传感器时，不会破坏刚建立的解耦边界。

任务：

1. 定义原始距离数据结构，例如：
   - `DistanceSample`
2. 定义高层距离判定结构，例如：
   - `DistanceDecision`
3. 明确距离判定的最小字段：
   - 是否有效
   - 距离类别或阈值触发类别
   - 置信度
   - 生效时间
   - 对速度/方向/状态机的建议动作
4. 明确距离传感器只通过策略层影响控制：
   - 改速度目标
   - 触发减速/停车
   - 触发等待
   - 触发状态切换
5. 约束距离传感器的落地顺序：
   - 先扩 `hardware_profile`
   - 再补 adapter/raw sample
   - 再补高层判定
   - 最后接策略层

验收标准：

1. 距离传感器以后可以作为并列的语义输入接入。
2. 不需要把距离逻辑硬塞进 `camera_logic.cpp` 或 `control_loop.cpp`。

触达文件：

- `new/code/port/control_types.hpp`
- `new/code/port/hardware_profile.hpp`
- `new/code/runtime/control_loop.cpp`

运行命令：

```bash
(cd new/user && ./build.sh)
```

预期 marker（接口接入运行时后）：

- `distance.init.hook`
- `distance.hook.read`
- `distance.read.invalid`

证据文件：

- `phase-c-distance-semantics.md`
- `phase-c-strategy-interface.md`

失败回退方向：

- 新硬件仍需改控制主干才能接入：留在 Phase C

### C-4 数据集与离线验证准备

目标：
为图片识别和后续语义判定建立稳定的数据、标签与离线验证基础。

任务：

1. 采集赛道图片样本。
2. 采集不同光照、不同角度、不同距离的图像。
3. 定义语义类别和标签规则。
4. 建立离线回放或分析工具，避免每次都上车调。

验收标准：

1. 图片识别算法有明确训练/验证输入。
2. 语义类别定义稳定，不在实装阶段反复变化。

触达文件：

- `new/docs/race-finish-series.zh-CN/03-phase-c-perception-and-semantics.md`
- 数据集索引与离线分析脚本所在路径

运行命令：

```bash
(cd new/user && ./build.sh)
```

预期 marker：

- 无强制运行时 marker，以数据集与离线报告为主

证据文件：

- `phase-c-dataset-index.md`
- `phase-c-offline-validation.md`

失败回退方向：

- 数据与标签定义不稳定：留在 Phase C

## 4. Phase C 必须产出的证据

1. 多光照图像样本与结果对比
2. 视觉鲁棒性增强前后效果对比
3. 语义识别接口设计文档
4. 策略层接口设计文档
5. 图片识别类别和策略映射表
6. 图片识别与距离判定的统一映射表
7. 对控制主干影响边界的说明：哪些字段进入策略层，哪些不进入

## 5. Phase C 退出条件

只有同时满足以下条件，才允许进入 Phase D：

1. 基础循迹鲁棒性明显提升
2. 赛道元素逻辑范围已定
3. 策略层接口已经确定
4. 图片识别算法的接口层已经确定
5. 距离传感器的扩展接口已经确定
6. 控制主干只消费 project-owned 策略结果

## 6. 本阶段完成后的结构性结论

Phase C 通过后，项目必须同时满足：

1. 视觉主链路不再只适用于受控环境。
2. 图片识别能力可以作为独立模块接入，而不会迫使重写控制主干。
3. 距离传感器可以作为并列模块接入，而不会迫使重写控制主干。
4. 感知开发、语义开发、辅助传感器开发可以并行推进。
