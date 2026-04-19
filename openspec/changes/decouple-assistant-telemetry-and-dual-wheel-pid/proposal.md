## Why

`new/` 当前已经形成 project-owned 的 Phase B 生命周期和差速执行器契约，但控制主线内部仍保留“均速 PID + PWM 差速混控”的耦合结构，同时缺少一条与安全主线解耦的逐飞助手观测/图传侧链。这会让双轮闭环重构、在线观测和后续扩展继续混在线程、vendor 接口和控制逻辑之间，削弱 `new/` 作为主工作区的清晰边界。

## What Changes

- 新增 project-owned 的逐飞助手侧链能力，把 TCP/逐飞助手协议、只读波形发布和 E07_04 风格图传封装成可选 support service，而不是塞进 `runtime` 安全主线或 `platform` 关键启动链路。
- 新增 project-owned 的双轮分别 PID 控制能力，用左右轮目标速度和左右轮独立闭环替换当前 `mean_pwm + turn_output` 的内部生成路径。
- 引入统一的控制调试快照边界，用 project-owned snapshot 承载 Phase B 关键观测量，供 diagnostics、结构化验证导出、逐飞助手波形和图传侧链复用。
- 修改适配层契约，明确逐飞助手 vendor API 只能留在 owning bridge 内，辅助遥测不得重新泄漏 vendor 头文件、路径或全局状态到 `legacy` / `runtime`。
- 修改 workspace 契约，要求 `new/user/` 的构建、入口和验证流程把逐飞助手 support service 视为可选侧链，并保持其失败不影响主控制生命周期。
- 为双轮闭环补充参数、验证和迁移要求，使后续板端/实车 Phase B 证据能直接观察左右轮目标、反馈和输出，而不是继续依赖均值速度推断。
- 明确首版合同：逐飞助手只提供只读观测能力，不开放在线写参数；transport 边界预留扩展点，但本变更仅实现 TCP。

## Capabilities

### New Capabilities
- `assistant-telemetry-sidecar`: 定义 `new/` 中逐飞助手的可选侧链集成、控制快照发布、图像发布、前台线程轮询，以及只读观测边界。
- `dual-wheel-motion-control`: 定义 `new/` 中左右轮目标生成、左右轮独立 PID 闭环、命令组装和与 Phase B 生命周期兼容的控制语义。

### Modified Capabilities
- `tc264-to-true-ls2k0300-adapter-layer`: 增加逐飞助手 vendor 协议/传输的 owning-bridge 边界，以及左右轮反馈/执行归一化继续留在 project-owned 端口契约内的要求。
- `true-ls2k0300-port-workspace`: 更新 `new/` 的构建和运行入口契约，使 support service 集成、调试快照、逐飞助手可选启用和 Phase B 验证口径保持一致。

## Risk Tier

- `STRICT`: 该变更同时触及真实车辆控制路径和观测/图传侧链。双轮 PID 重构会改变左右轮输出生成方式、参数面和 Phase B 实车行为；逐飞助手侧链会引入额外的传输与观测路径，如果边界设计错误，容易把 vendor 依赖、线程时序或辅助链路副作用重新注入主控制链。因此需要显式设计、规格和 docs-first 验证。

## Impact

- Affected code: `new/code/runtime/control_loop.*`, `new/code/runtime/runtime_state.hpp`, `new/code/legacy/pid_control.*`, `new/code/legacy/motor_logic.*`, `new/user/main.cpp`, `new/user/CMakeLists.txt`, `new/config/default_params.json`, 以及新增的 control snapshot / wheel PID / wheel target mixer / assistant sidecar 相关文件。
- Affected APIs and contracts: `port::ActuatorCommand` 继续保持 `left_pwm/right_pwm/emergency_stop` 不变，但内部控制生成路径从均速混控切换为左右轮目标 + 左右轮闭环；逐飞助手集成新增 project-owned telemetry/image sidecar 边界，并明确首版无在线写参。
- Affected dependencies and systems: true baseline `seekfree_assistant` 组件、TCP client 驱动、E07_04 图传参考、Phase B diagnostics、RuntimeParameters、前台主循环与控制观测。
- Affected verification and docs: `new/docs/race-finish-series.zh-CN/02-phase-b-low-speed-vehicle-motion.md`、`07-current-progress.md`、Phase B 验证包与变更本地 verification 证据将需要反映左右轮闭环、结构化非助手证据面，以及逐飞助手侧链的 accepted 行为。
- Participating skills: `openspec-propose`, `openspec-architect`, `openspec-artifact-verify`, 后续实现时使用 `openspec-apply-change` 和 `openspec-verify-change`。
