## Why

`new/` 主线已经跨过“默认 direct-match profile 可以启动并进入 control loop”的门槛，但 `Phase A` 仍未完成退出条件。当前阻塞点不再是工作区、vendor baseline、执行器拓扑或粗粒度控制观测，而是剩余硬件闭环事实还没有被收口成可审计的板测证据和清晰 contract：IMU 样本可信度、编码器方向与量级、电机真实出力与 fail-safe 回退、`control.veto` 的稳定解锁，以及相机曝光主线策略。

如果继续把这些问题分散在文档、零散板测和临时结论里，`Phase A` 会长期停留在“局部已通但整体未收口”的状态，也会让后续 `Phase B` 建立在不稳定前提上。本 change 的目标是把剩余闭环工作整理成一个优雅、解耦、可 apply 的 Phase A 收口 change：保留现有 project-owned 边界，不引入新比赛能力，只补齐剩余 contract、证据与阶段判断。

## What Changes

- 冻结并引用 `2026-04-15` accepted direct-match baseline，明确其已证明的启动级事实和仍未证明的闭环事实。
- 收口 IMU direct-match 样本闭环要求：区分“能启动/能探测”与“持续 valid、方向正确、静止样本可解释”。
- 收口编码器 phase-1 闭环要求：明确 baseline、jump、方向和速度量级的验证口径。
- 收口电机真实出力与 fail-safe 回退要求：把“软件写入成功”与“真实机械出力、急停可靠”分开验证。
- 收口 `control.veto` 稳定解锁要求：要求证据能说明 veto 原因、arming 变化、apply 结果，以及连续非 veto 区间是否成立。
- 收口相机曝光主线策略：要求对 direct-match 曝光能力给出明确结论，而不是继续保持开放问题。
- 更新 Phase A 路线与进展文档，使退出条件、当前状态和证据文件命名一致。

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `true-ls2k0300-port-workspace`: Phase A 路线、证据命名和阶段退出判断需要从“启动级证明”推进到“硬件闭环与 control unlock 收口”，并要求 accepted baseline、phase-scoped evidence 和文档状态保持一致。
- `tc264-to-true-ls2k0300-adapter-layer`: adapter/runtime contract 需要明确 Phase A 剩余硬件闭环验收点，包括 IMU 样本可信度、编码器 delta 解释、电机真实出力/fail-safe 回退，以及 `control.veto` 解锁证据应如何保持 project-owned、解耦且可审计。

## Risk Tier

- `STANDARD`: 该 change 不引入新的产品能力或新的运行入口，但它会修改现有 workspace/spec contract、Phase A 验收要求、runtime/adapter 边界上的验证义务，并可能触达 IMU、编码器、电机、camera exposure、control observability 等多个已有子系统，因此需要完整 proposal/design/specs/tasks 和后续实现验证。

## Impact

- Affected code: `new/code/platform/imu_adapter.cpp`, `new/code/platform/true_ls2k0300/imu_bridge.cpp`, `new/code/platform/encoder_adapter.cpp`, `new/code/platform/true_ls2k0300/encoder_bridge.cpp`, `new/code/platform/motor_adapter.cpp`, `new/code/platform/true_ls2k0300/motor_bridge.cpp`, `new/code/runtime/control_loop.cpp`, `new/code/runtime/perception_frontend.cpp`, `new/code/legacy/camera_logic.cpp`, and related diagnostics/evidence helpers as needed.
- Affected docs: `new/docs/race-finish-series.zh-CN/01-phase-a-hardware-and-closure.md`, `00-master-roadmap.md`, `07-current-progress.md`, plus change-local verification evidence notes.
- Affected specs: `openspec/specs/true-ls2k0300-port-workspace/spec.md` and `openspec/specs/tc264-to-true-ls2k0300-adapter-layer/spec.md`.
- Affected systems: Phase A board smoke, hardware evidence collection, runtime control unlock review, and stage-exit judgment for entry into `Phase B`.
- Dependencies: accepted board evidence under `openspec/changes/archive/2026-04-15-fix-board-runtime-hardware-detection/verification/` and `openspec/changes/archive/2026-04-15-align-phase-a-actuator-topology-and-control-observability/verification/`, current `new/user/build.sh` and `new/user/run_remote_smoke.sh`, and existing project-owned control observability markers.
- Expected skills: `openspec-propose`, `openspec-apply-change`, and `openspec-verify-change`.
