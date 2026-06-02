# 上赛道完赛系列文档

## 1. 目的

这组文档给出一条从当前代码状态推进到“可以上赛道并完赛”的统一执行序列。

它不是单纯的设计说明，也不是零散问题清单，而是一套按阶段推进的任务书：

- 每个阶段都有明确目标
- 每个阶段都有代码任务、板测任务、实车任务
- 每个阶段都有进入条件、退出条件、验收口径
- 只有前一阶段通过，才进入后一阶段

这套系列文档的目标不是“理论上可行”，而是“按顺序执行，并且每阶段都通过验收后，整车具备完赛所需的最低能力”。

本系列已经吸收并改写了历史设计、调试、硬件矩阵、映射关系和赛道准备文档中的核心思想，因此它不是一个附加索引，而是当前的主执行文档集。

从现在开始，本系列既承担“路线图”角色，也承担“实现与验证契约”角色。后续即使在没有当前对话上下文的情况下接手，也应优先以本系列为准，而不是回退到已归档文档重新拼装结论。

## 2. 适用前提

本系列默认以下前提成立：

1. 目标车体为三轮车，采用纯差速转向，不依赖独立舵机。
2. 目标软件基线为 `new/` 工作区，而不是 `old/`、`old_2/` 或已废弃的错误 baseline。
3. 目标硬件 baseline 仍以 `true_LS2K0300_Library/Seekfree_LS2K0300_Opensource_Library` 为准。
4. IMU、相机、编码器、电机、ADC、定时器最终都需要以可验证的方式进入控制闭环。
5. 后续将增加“赛道图片识别”能力，未来也可能增加距离传感器判定能力；它们都可能影响方向、速度或状态机，因此本系列会把它们作为正式能力预留和接入，而不是赛后附加。

如果以上前提变了，这套文档需要同步修订。

## 3. 主要参考来源

本系列综合了以下现有资料和代码现状：

- `new/docs/superseded/track-readiness-gap.zh-CN.md`
- `new/docs/superseded/race-finish-series-source/hardware-matrix.md`
- `new/docs/superseded/race-finish-series-source/debugging.md`
- `new/docs/superseded/race-finish-series-source/mapping.md`
- `new/docs/superseded/race-finish-series-source/vendor-baseline.md`
- `new/docs/superseded/race-finish-series-source/proposal.md`
- `new/docs/superseded/race-finish-series-source/design.md`
- `new/docs/superseded/race-finish-series-source/tasks.md`
- `new/code/runtime/startup.cpp`
- `new/code/runtime/control_loop.cpp`
- `new/code/runtime/perception_frontend.cpp`
- `new/code/platform/camera_adapter.cpp`
- `new/code/platform/imu_adapter.cpp`
- `new/code/platform/encoder_adapter.cpp`
- `new/code/platform/motor_adapter.cpp`
- `new/code/platform/power_adapter.cpp`
- `new/code/platform/param_store.cpp`
- `new/code/legacy/camera_logic.cpp`
- `new/code/legacy/pid_control.cpp`
- `new/code/legacy/motor_logic.cpp`
- `old/code/camera.c`
- `old/code/PID.c`
- `old/code/Motor.c`
- `old_2/project/code/camera.cpp`
- `old_2/project/code/motor.cpp`
- `old_2/project/code/isr.cpp`
- `old_2/project/user/main.cpp`
- `old_2/project/code/key.cpp`
- `old_2/project/code/save.cpp`
- `old_2/libraries/zf_device/zf_device_uvc.cpp`

这些资料现在主要承担两个角色：

1. 为本系列提供设计依据
2. 在需要更细节时作为查阅材料

但关于“路线怎么走、阶段怎么判、是否可以进下一步”的结论，以本系列为准。

其中，原先单独存在的 `vendor-baseline.md` 已视为被本系列吸收；关于正确 vendor baseline、错误 baseline 仅作历史证据、以及 bridge 隔离约束，均以本系列中的主路线和 Phase A 表述为准。

对于已归档文档中的旧运行描述，也按同样原则处理：

1. 已归档文档只保留历史背景和吸收来源作用。
2. 如果已归档文档与本系列对同一命令、marker、证据路径的说法冲突，以本系列为准。
3. `run_remote_smoke.sh` 的失败行为、日志路径和本地回退语义，以 `06-implementation-and-verification-contract.md` 的现行说明为准，不再沿用旧文档表述。

对历史代码源还要补一条角色区分：

1. `old/` 主要提供 TC264 裸机时期的算法血缘和字段语义。
2. `old_2/` 主要提供 LS2K/Linux 分支上的运行时组织、赛道规则链和现场调参 workflow。
3. 两者都属于迁移证据源，不属于当前可直接复制进主线的运行工作区。

## 4. 系列结构

按执行顺序使用以下文件：

1. `00-master-roadmap.md`
2. `01-phase-a-hardware-and-closure.md`
3. `01a-phase-a-retrospective-and-lessons.md`（Phase A 附属复盘，供后续阶段借鉴）
4. `02-phase-b-low-speed-vehicle-motion.md`
5. `03-phase-c-perception-and-semantics.md`
6. `04-phase-d-track-operations-and-tuning.md`
7. `04a-phase-d-wheel-vibration-closure.md`
8. `05-phase-e-finish-gate.md`
9. `06-implementation-and-verification-contract.md`
10. `07-current-progress.md`

## 5. 使用规则

1. 任何阶段都不允许跳过退出条件直接进入下一阶段。
2. 代码修改和验证证据必须成对存在。
3. 一项能力只有在“代码存在 + 板测通过 + 实车通过”后，才算真正完成。
4. 如果某阶段发现前一阶段假设失效，必须回滚到相应阶段修补，而不是继续堆任务。
5. 赛道图片识别能力和未来距离传感器判定能力都必须通过独立接口接入，不允许直接把语义逻辑硬塞进现有循迹算法主体。

## 5A. 系列内部的角色分工

- `00-master-roadmap.md`：终极路线主文档，负责总目标、总约束、总阶段和最终判定
- `01` 到 `05`：阶段执行文档，负责把主路线展开成可操作任务、证据和退出条件
- `06-implementation-and-verification-contract.md`：实现边界、参数、profile、marker、命令、证据和未来扩展解耦契约

因此，本系列现在既有“总纲”，也有“分阶段执行书”，也有“实现与验证附录”。

## 5B. 无上下文接手方式

如果后续 AI 或开发者没有当前对话上下文，默认按以下顺序接手：

1. 读 `README.md`
2. 读 `07-current-progress.md`
3. 读 `00-master-roadmap.md`
4. 读 `06-implementation-and-verification-contract.md`
5. 只读当前要执行的阶段文档

如果需要做代码更改，必须同时参考阶段文档和 `06` 中的实现/验证契约，不能只看路线标题就直接改。

对于相机几何、策略层、验证脚本这三类边界，接手者不得只看归档文档或代码注释推断，必须以本系列中的显式契约为准。

如果当前任务直接涉及 assistant 双向调速、远程 `start` / `stop`、或 PID 现场调参，还必须额外读取：

1. `04-phase-d-track-operations-and-tuning.md`
2. `new/verification/phase-d-speed-tuning.md`
3. `new/user/tune_speed.py`

并以 `new/user/debug.sh tuning ...` 作为 accepted host workflow 入口，而不是临时拼装 ad hoc socket 脚本。

## 6. 完赛的定义

本系列中的“完赛”按最低工程定义理解：

1. 车辆可在目标赛道环境中完成完整路线而不中途失控、长时间停车或因软件缺陷退出。
2. 车辆在赛道关键元素处可以按预期识别并执行方向、速度或状态切换。
3. 车辆在光照变化、轻微图像波动、短时传感器抖动下仍保持可恢复运行。
4. 现场操作具备可重复的启动、参数切换、回滚和故障处理流程。

## 7. 当前状态定位

当前代码更接近：

- 已达到：L0 可构建、可部署、可收日志
- 基本达到：L1 板上程序可稳定启动并持续运行
- 未达到：L2 关键传感器闭环有效且控制可解除 veto
- 未达到：L3 低速实车闭环可动
- 未达到：L4 可在真实赛道环境中调参
- 远未达到：L5 稳定完赛

因此，本系列的实际起点是 Phase A，而不是从零开始。

当前最新推进快照见：

- `07-current-progress.md`
