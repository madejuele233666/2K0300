# 终极路线图：从当前代码到上赛道完赛

## 1. 文档定位

这份文档是当前项目的终极路线文档。

它不是单纯的任务列表，也不是单纯的设计说明，而是把以下内容统合到一个主文档中：

1. 迁移目标和边界
2. 当前代码结构和运行时事实
3. 硬件、控制、感知、参数、验证的核心约束
4. 从现在到完赛的分阶段执行顺序
5. 每个阶段的进入条件、任务、验收标准和退出条件
6. 最终“可以完赛”的统一闸门

从现在开始，凡是要回答“下一步做什么”“是否达到某个阶段”“是否可以上赛道/完赛”，都应以本文件为主。

系列中的其他文件保留，但它们是支撑材料，不再承担主路线叙事：

- `01-phase-a-hardware-and-closure.md`
- `02-phase-b-low-speed-vehicle-motion.md`
- `03-phase-c-perception-and-semantics.md`
- `04-phase-d-track-operations-and-tuning.md`
- `05-phase-e-finish-gate.md`
- `06-implementation-and-verification-contract.md`

## 2. 最终目标

本路线图中的最终目标不是“程序能跑”，也不是“车辆偶尔能动”，而是：

1. 车辆按比赛配置上电、启动、运行、停机都可重复执行。
2. 车辆能够在目标赛道环境中完成完整路线。
3. 车辆在赛道关键元素处可以依据循迹和图片识别结果，正确改变方向、速度或状态。
4. 车辆在常见光照波动、轻微传感器抖动和短时异常下不至于立刻失控。
5. 现场具备参数切换、回滚、急停、重启和故障定位能力。

这里的“完赛”采用最低工程定义：

- 不要求一开始就追求最快成绩。
- 但要求路线完整、控制稳定、行为可解释、流程可复现。

## 3. 已知前提

本路线图默认以下前提为真：

1. 车辆是三轮车，采用纯差速转向。
2. 当前软件主工作区是 `new/`，不是 `old/`。
3. 目标硬件 baseline 是 `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library`。
4. 当前 Phase-1 运行时仍坚持 fail-closed 原则。
5. 后续必须加入“赛道图片识别算法”，未来也可能加入距离传感器判定能力；二者都可能用于影响方向、速度或状态切换。

如果这些前提变化，这份路线图需要同步重写。

## 4. 迁移背景与结构结论

### 4.1 源系统与目标系统

历史上至少存在两套高价值源系统：

1. `old/` 下的 TC264 裸机工程，提供最早一版算法和状态语义，核心逻辑分散在：

- `old/user/cpu0_main.c`
- `old/user/isr.c`
- `old/code/camera.c`
- `old/code/PID.c`
- `old/code/Motor.c`
- `old/code/ZiTaiJieSuan.c`
- `old/code/key.c`

2. `old_2/` 下的 LS2K/Linux 历史分支，提供平台切换后的运行组织和更完整的赛道启发式，核心逻辑分散在：

- `old_2/project/code/camera.cpp`
- `old_2/project/code/motor.cpp`
- `old_2/project/code/isr.cpp`
- `old_2/project/user/main.cpp`
- `old_2/project/code/key.cpp`
- `old_2/project/code/save.cpp`
- `old_2/libraries/zf_device/zf_device_uvc.cpp`

目标系统是 `new/` 下的 project-owned LS2K0300 工程，其设备访问模型与 TC264 完全不同，也不能直接照搬 `old_2/` 的 Linux 实现细节：

- 相机来自 UVC
- IMU 来自 IIO/sysfs
- 编码器、PWM、GPIO 通过 Linux 设备节点
- 周期控制由 Linux 进程 + 定时桥接完成

因此，项目不是简单“拷贝旧代码”，而是：

1. 保留可复用算法
2. 更换硬件访问方式
3. 重建运行时生命周期
4. 补齐 Linux 环境下的验证和工程化流程

### 4.1A `old_2` 分支的定位

`old_2/` 不是一个普通备份目录，而是一个已经把比赛逻辑搬到 LS2K/Linux 环境下的历史分支。

它对当前迁移最有价值的地方不在于“可以直接编进来”，而在于它补充了 `old/` 中没有说完整的运行语义：

1. 相机前端已经从裸机采集切到 UVC/OpenCV 路径。
2. 视觉规则链比 `old/` 更完整，包含边界修复、丢线统计、角点、十字、斑马线、环岛、障碍等状态。
3. 转向主变量不再只是 `Err`，而是先形成 `mid_servo`，再由 `Image_middle - mid_servo` 驱动控制。
4. 主循环、定时线程、发车、缓启动、刹停和无刷介入流程已经被组织成 Linux 运行时。
5. `settings.txt + IPS` 形成了可在车上快速调整的轻量调参模式。

因此，当前迁移应把 `old_2/` 视为第二条迁移证据链：

1. `old/` 提供算法血缘和字段原义。
2. `old_2/` 提供 LS2K 时代的运行和赛道经验。
3. `new/` 必须在 project-owned 边界内吸收两者，而不是复刻任何一方的目录结构。

### 4.2 当前认定的正确架构

当前路线基于如下结构结论，这些结论来自现有 proposal/design/specs/代码的一致交集：

1. `new/` 是唯一有效的迁移工作区。
2. `new/code/legacy/` 存放尽量保留的算法逻辑。
3. `new/code/platform/` 和 `new/code/platform/true_ls2k0300/` 负责适配硬件与桥接 vendor API。
4. `new/code/runtime/` 负责启动、前台感知、周期控制、关机。
5. `new/config/` 负责 profile 和参数。
6. `new/docs/` 负责设计、调试、赛道路线与运维文档。
7. `old/` 和 `old_2/` 都只作为迁移证据源，不再作为主线运行工作区。

### 4.2A Vendor Baseline 契约

以下 baseline 结论属于主线硬约束，不再依赖单独文档维护：

1. 唯一接受的 vendor root 是 `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library`。
2. 唯一接受的 build skeleton 是 `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library/project/user`。
3. 当前迁移与验证应以真实 retarget 结果为准，错误 baseline 的历史 change 只能作为反例证据。
4. `LS2K0300_Library/LS2K300_Library/Seekfree_LS2K0300_Opensource_Library` 只保留历史对照价值，不能再作为实现或验收依据。
5. 从错误 baseline 推导出的 `.hpp` wrapper 假设不能直接视为当前真 baseline 的有效前提，必须逐项重验。
6. vendor 头文件、vendor 全局符号、free function 和 vendor C++ 类型只能留在 platform-owned bridge 代码中，不能扩散回 `runtime` 或 `legacy` 公共边界。

### 4.3 不能被破坏的架构原则

这些原则不是建议，而是主线约束：

1. 不能把硬件访问重新泄漏回 `legacy` 层。
2. 不能把 vendor 头文件直接带入 `legacy` 或 `runtime` 的公共边界。
3. 不能依赖隐藏硬件假设，必须通过 `hardware_profile` 或显式 direct-match 证据表达。
4. 不能把 degraded 路径当成最终比赛部署路径。
5. 不能把图片识别语义逻辑直接硬塞进当前循迹主体中。
6. 不能把未来距离传感器的特定场景判定直接焊死进 `camera_logic` 或 `control_loop`。

### 4.4 当前解耦状态与必须补的层

当前代码的 adapter/bridge 隔离是成立的，但策略层解耦还不够。

现状上更接近：

1. `camera/imu/encoder/motor/power` 已经过 adapter 边界隔离
2. `control_loop` 仍直接消费 `perception + imu + encoder`
3. `PerceptionResult` 仍偏向基础循迹结果

这意味着：

1. 新增图片识别算法不会足够顺滑
2. 新增距离传感器也不会足够顺滑
3. 如果不先补策略层，后续会继续在控制主干里堆特例

因此，主路线要求后续必须形成：

1. 原始传感器层
2. 基础感知层
3. 语义判定层
4. 策略层
5. 控制层

控制层最终只消费 project-owned 的策略结果，而不是直接理解识别模型或距离硬件细节。

这里的“只消费策略结果”不是空泛口号，后续至少要把当前直接 veto 的上层信号整理进 project-owned 策略边界：

1. 感知 freshness / published 状态
2. `emergency_veto`
3. 低压导致的运动禁止结论
4. `imu.valid`
5. `encoder.valid`

也就是说，图片识别和未来距离传感器只是新增输入源；当前已经存在的 fail-safe 判定也要一起迁移成可审计的 `StrategyInput -> StrategyDecision` 契约，而不是只给语义功能单独开口子。

## 5. 当前代码与运行时的真实状态

### 5.1 已经达到的状态

当前已经基本达到：

- `new/` 可以构建
- 远程 smoke 流程可用
- 板端可部署并可回收日志
- 相机、ADC、编码器、电机、定时器的大部分基础路径已打通
- 编码器和电机桥的驱动返回值问题已经修复
- 默认 direct-match profile 已在 `2026-04-15` 的板端重跑中完成 `imu.init`、`imu.detect`、`startup.complete` 和 `control.start`
- 该结论当前以 `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/runtime-smoke-retry-2026-04-15.log`、`runtime-smoke-retry-2026-04-15.exit`、`hardware-discovery-retry-2026-04-15.log` 为 accepted baseline，并由同目录 `runtime-smoke-execution-evidence.md` 说明其对更早 direct-match 工件的 supersession 关系
- 执行器拓扑与控制观测补充证据已归档到 `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/runtime-smoke-2026-04-15.md`，其中确认了 `control.veto.perception_stale`、`control.apply.emergency_stop`、`control.command.requested_nonzero`、`control.apply.drive` 与 `control.arm.transition`

这意味着项目已经走出“完全不可运行”的阶段。

### 5.2 仍未达到的状态

当前仍未达到：

- IMU direct-match 的静止样本、方向、零偏与持续有效性闭环完成
- 编码器反馈已被证明可直接支撑闭环控制
- 低速实车可控运动
- 视觉在真实赛道环境中的鲁棒性
- 图片识别算法的正式接入点
- 参数与现场运维流程闭环
- 完整路线完赛能力

### 5.3 为什么现在还不能认定可上赛道

当前代码中有几个直接阻止“可上赛道”结论的事实：

1. 启动阶段默认要求 `camera/imu/encoder/motor` 都是 direct-match 且 ready。
2. 控制环中只要 `!imu.valid`、`!encoder.valid`、感知 stale、低压或感知 veto，就进入 project-owned veto 判定，并发出 `control.veto` 以及对应的 `control.veto.*` 原因 marker。
3. `2026-04-17` 的 change-local 证据已经证明启动 veto 会闭合，并可进入持续非 veto 区间；因此“控制永远解不开 veto”不再是当前主阻塞。
4. 当前相机 direct-match 路径对 `exp_light` 有明确限制，且该限制已被写成 bounded mainline policy，而不是开放性未知项。

所以目前最准确的状态是：

- L0 可构建、可部署、可回收日志：已达到
- L1 板上能稳定启动并持续运行：基本达到
- L2 关键传感器有效且控制可解除 veto：部分达到，仍卡在 IMU/encoder control-trust
- L3 低速实车闭环可动：未达到
- L4 可在赛道环境调参：未达到
- L5 稳定完赛：远未达到

## 6. 必须长期遵守的运行时契约

本路线图在执行层面依赖以下运行时契约。

### 6.1 启动契约

启动必须按以下顺序成立：

1. 读取 `hardware_profile`
2. 读取运行参数
3. 应用启动关键参数，例如 `P_Mode` 和 `exp_light`
4. 初始化低压检测
5. 初始化 camera
6. 初始化 IMU
7. 初始化 encoder
8. 初始化 motor
9. 拉起 timer / control loop

任一关键步骤失败，都应 fail-safe，而不是静默降级。

### 6.2 控制契约

控制环必须满足：

1. 感知结果 fresh
2. IMU valid
3. encoder valid
4. 非低压紧急状态
5. 非感知层 emergency veto

只有以上条件同时满足，才允许真实输出。

### 6.3 感知契约

当前基础感知必须至少保证：

1. 输出可解释的循迹误差
2. 在图像不可用时显式给出 marker 或 veto
3. 不允许 silent fallback

未来还要增加：

1. 图片识别语义输出
2. 策略层对语义结果的消费

### 6.4 执行器契约

当前车辆是纯差速转向，因此：

1. 左右轮差速是主转向执行路径
2. `servo` 不属于当前比赛主线执行器
3. `port::ActuatorCommand` 已固定为 `left_pwm + right_pwm + emergency_stop`，`servo_pwm` 不再属于主线 public contract

### 6.5 验证契约

只有“代码 + 板测 + 实车 + 结论”同时存在，才算能力通过。

单独满足以下任一项都不够：

- 代码已写
- 日志看起来正常
- 某一次实车偶然成功
- 参数刚好在某次测试中可用

### 6.6 实现与验证附录契约

除主路线与阶段文档外，后续实现还必须遵守：

- `06-implementation-and-verification-contract.md`

该附录负责固定：

1. workspace / vendor / legacy 的边界
2. `hardware_profile` 与参数契约
3. marker 与命令入口
4. 证据与无上下文接手方式
5. 图片识别与距离传感器的统一解耦方式

## 7. 完赛路线的五个阶段

整个路线分成五个阶段。

### 7.1 Phase A：硬件闭环与控制解锁

状态：

- 已于 `2026-04-17` 正式结束

目标：

- direct-match 关键硬件闭环
- 控制环解除长期 veto
- 差速转向执行路径正式固化

必须完成的核心任务：

1. 固化纯差速转向约束，保持 public actuator contract 为 `left_pwm + right_pwm + emergency_stop`，并把 `turn_output` 语义固定为“差速调节量”
2. 在已有启动级证据基础上，完成 IMU direct-match 的静止样本、方向、零偏和持续有效性闭环
3. 做 IMU 零偏、方向、稳定性校验
4. 完成编码器方向与速度基线标定，并把“能读”与“可用于控制”分开验收
5. 验证电机真实出力与 emergency-stop，同时保持任何 bench PWM 测试 env-gated 且默认关闭
6. 让控制环在传感器有效时进入持续非 veto 区间，并记录 veto/unveto interval marker
7. 对相机曝光策略给出明确落地方案

必须产出的证据：

1. 启用 IMU 的 direct-match 板测日志
2. 传感器有效时的控制环日志
3. 架空轮或安全工装下的真实出力记录
4. 编码器方向和速度标定记录
5. 曝光策略结论与样本分析

退出条件：

1. IMU 板上闭环通过
2. 编码器反馈可信
3. 控制环可稳定解除 veto
4. 电机出力和急停均可验证
5. 曝光问题不再是“未定事项”

截至 `2026-04-17`：

1. 第 1 至第 5 项均已拿到 change-local 证据支撑。
2. `Phase A` 退出条件已满足。
3. `Phase B` 允许进入。

未通过时禁止进入实车低速运动阶段。

### 7.2 Phase B：低速实车闭环

目标：

- 让车辆低速、可控、可恢复地真实运动

必须完成的核心任务：

1. 静态安全检查和上电/停机流程固定
2. 架空轮与半载测试
3. 低速直线闭环
4. 轻弯和回正测试
5. 异常工况下 fail-safe 验证

必须产出的证据：

1. 低速直线和轻弯的视频 + 日志
2. 异常注入记录
3. 当前低速参数集

退出条件：

1. 车辆低速可动且行为可解释
2. 车辆可安全停机
3. 轻微转向修正可工作
4. 异常注入可回到 fail-safe
5. 已形成一套可重复的低速参数集

### 7.3 Phase C：感知鲁棒性与语义接口

目标：

- 让系统不再只依赖受控环境下的基础循迹
- 为未来图片识别算法和距离传感器判定建立正式接口

必须完成的核心任务：

1. 提升视觉鲁棒性：
   - 分区/局部阈值
   - 更稳定的边界或中线提取
   - 感知置信度
   - 短时丢线恢复
2. 明确并恢复必要的赛道元素状态机，或正式裁剪其范围
3. 先补 project-owned 的策略层边界，避免继续让控制环直接长出特例
4. 为图片识别算法设计独立接口：
   - 语义识别结果结构
   - 策略层接入点
   - 与循迹输出的解耦方式
5. 为未来距离传感器设计并预留同一策略层接入方式：
   - 原始距离 sample
   - 距离触发的高层判定
   - 与图片识别并列的语义输入位置
6. 准备图片样本、类别定义和离线验证资料

必须产出的证据：

1. 多光照视觉对比结果
2. 鲁棒性增强前后效果对比
3. 策略层接口设计
4. 图片识别接口设计
5. 图片识别与距离传感器的统一策略映射表

退出条件：

1. 基础循迹鲁棒性明显提升
2. 赛道元素逻辑范围已确定
3. 策略层接口已经确定
4. 图片识别接口层已经确定
5. 控制主干可以消费未来语义结果

### 7.4 Phase D：赛道调参与运营流程

目标：

- 建立真正可用于现场调试和比赛的工程化流程

必须完成的核心任务：

1. 参数集版本化
2. 启动、切换、回滚、急停、重启流程标准化
3. 固化关键日志与观测项
4. 按直线 -> 弯道 -> 局部赛段 -> 完整路线逐步测试
5. 量化时序、延迟、负载、图片识别接入后的性能影响
6. 如果已接入距离传感器或其判定逻辑，也要量化其对控制周期和策略延迟的影响

必须产出的证据：

1. 参数版本目录
2. 测试记录表
3. 失败案例与修复记录
4. 时序/性能记录
5. 现场操作流程

退出条件：

1. 参数切换和回滚可用
2. 测试已覆盖完整路线级别
3. 图片识别已接入策略层
4. 时序和性能在目标负载下可接受

### 7.5 Phase E：完赛闸门

目标：

- 只有全部通过，才允许宣称“具备完赛能力”

必须通过的闸门：

1. 硬件与控制闸门
   - 相机、IMU、编码器、电机、ADC、定时器按比赛配置工作
   - 启动不依赖 degraded 模式
   - 控制环不长期停留在 `control.veto`
   - 出力、停机、急停均可靠
2. 感知与语义闸门
   - 基础循迹稳定
   - 图片识别正确率满足比赛需要
   - 如果比赛方案采用距离传感器，其判定结果也满足比赛需要
   - 语义结果能正确影响方向/速度/状态机
   - 误识别不会直接导致不可恢复错误
3. 赛道运行闸门
   - 完整路线可完成
   - 多次连续运行结果稳定
   - 复杂元素处策略切换正确
   - 常见光照变化不致命
4. 异常恢复闸门
   - 短时感知异常可恢复
   - 传感器抖动不会导致永久失控
   - 低压、停机、重启流程可用
   - 可快速回滚到稳定参数集
5. 运营闸门
   - 发车前检查单
   - 参数切换流程
   - 急停和人工接管流程
   - 主版本和备用版本

只有五类闸门全部通过，才允许认定“可上赛道完赛”。

## 8. 每个阶段都必须交付的东西

### 8.1 代码交付物

1. 代码改动
2. 配置改动
3. 必要的新接口

### 8.2 证据交付物

1. 板测日志
2. 实车测试记录
3. 参数版本记录
4. 失败案例与结论

### 8.3 结论交付物

1. 当前阶段是否通过
2. 剩余风险
3. 是否准入下一阶段

## 9. 关键风险与处理原则

### 9.1 不允许把 degraded 路径当成胜利

`degraded startup` 是诊断工具，不是比赛配置。

如果某项能力只能在 degraded 模式下工作，那么它仍然属于未完成。

### 9.2 不允许跳过实车验证

板测和 smoke 只能证明：

- 程序可启动
- 日志可信
- 某些设备节点可读写

它们不能证明：

- 车辆会稳定运动
- 车辆能在赛道上完赛

### 9.3 不允许把图片识别直接焊死在循迹算法主体里

未来的图片识别必须通过独立接口接入策略层，否则后续维护会非常困难，也会让控制主干被语义逻辑污染。

### 9.3A 不允许把未来距离传感器判定直接焊死在控制主干里

如果后续增加距离传感器，其原始读数应先进入 adapter/raw sample 层，再通过 project-owned 的语义/策略结构影响速度、方向或状态机。

不允许直接在 `control_loop` 里根据某个设备节点的即时读数堆条件分支。

### 9.4 不允许在 Phase E 临时堆补丁后直接宣称完赛

如果最终闸门失败，必须按问题类型回退：

1. 硬件/传感器/执行器问题：回 Phase A
2. 低速运动和闭环问题：回 Phase B
3. 视觉鲁棒性或语义识别问题：回 Phase C
4. 参数与现场流程问题：回 Phase D

## 10. 现在就该怎么做

如果从当前状态继续推进，执行顺序必须是：

1. Phase A
   - IMU direct-match
   - 编码器方向与标定
   - 电机真实出力
   - 控制环解除 veto
   - 曝光策略定论
2. Phase B
   - 低速直线
   - 轻弯修正
   - 异常工况
3. Phase C
   - 视觉鲁棒性
   - 赛道元素状态机
   - 策略层接口
   - 图片识别接口
   - 距离传感器的统一扩展接口
4. Phase D
   - 参数版本化
   - 运维流程
   - 赛道测试流程
   - 时序与性能
5. Phase E
   - 完整路线
   - 多次连续运行
   - 最终闸门

## 11. 一句话总结

从当前代码到“上赛道完赛”，不是一个“把若干 bug 修掉”的问题，而是一条必须依次打通：

硬件闭环 -> 控制解锁 -> 低速实车 -> 感知鲁棒性 -> 语义识别 -> 参数工程化 -> 完赛闸门

的完整工程路线。

只要任何一环没有证据闭合，就不能宣称已经具备完赛能力。
