## 0. Verification Contract

- Shared sequence:
  - `openspec/schemas/ai-enforced-workflow/verification-sequence.md#verify-sequence/default`
- Shared JSON verification contract:
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-core-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-openspec-adapter-v1.json`
  - `openspec/schemas/modules/verification-cycle/contracts/verification-cycle-agent-table-v1.json`
- Shared field groups:
  - `invocation_common_required`
  - `output_paths_required`
  - `verifier_evidence_required`
  - `valid_pass_requirements`
  - `partial_scope_rule`
- Runtime profile:
  - `.codex/agents/verify-reviewer.toml`
- Orchestration authority:
  - `openspec/schemas/modules/verification-cycle/orchestrator/CALLER-INTEGRATION.md`
- Artifact-completion gate ownership:
  - 当本 `tasks.md` 使 `applyRequires` 集合完成时，当前 artifact 创建调用者（`openspec-propose` 或 `openspec-continue-change`）必须立即运行 docs-first review
  - `openspec-apply-change` 不拥有该 docs-first gate
  - 在 docs-first gate 达成 valid active pass 之前不得进入实现
- Agent lifecycle:
  - 可用 `active` agent 必须优先续跑
  - 同一 `active` agent 仍打开时优先 `send_input`
  - 若 `send_input` 返回 `agent_not_found`，先对同一 `active` agent 执行 `resume`
  - 仅由 `continuation_probe.resume_result` 区分 `resume_active`、`active_agent_missing` 与 `active_agent_not_resumable`
  - 仅当 `agent-table.json` 字面上不存在 `active` agent，或 `continuation_probe` 明确要求恢复分支重建时，才允许 `spawn_active`
  - 只有 `block -> pass` 可以将 agent 标为 `non_active`
  - 终止依赖 valid active pass，而不是 `close` / `exit`

## 1. 参考对齐与 docs-first 入口修订

- [x] 1.1 使用 `openspec-align` 对 `old_2/project/code/camera.cpp`、`camera.h`、`motor.cpp`、`motor.h`、`FUZZY_PID.cpp`、`FUZZY_PID.h`、`all_init.cpp` 重新提取契约，并把 change 文档中的规范输入分辨率修订为 320x240、执行器语义修订为 `turn_output` 差速末端。
- [x] 1.2 在文档中显式记录本次不再使用 servo 命名字段、roadblock 只保留接口不实现绕行、straight/bend/circle entry/interior/exit/zebra/cross 必须模块化拆分，并保留 runtime-owned legacy steering state、reset-path、多周期控制与 assistant-disabled 验收这些基础契约；同时补齐 retained/removed/added 字段合同表与 post-reset clean baseline。
- [x] 1.3 docs-first artifact-completion gate 已由 `verification/artifact-completion/agent-table.json` 记录的 2026-04-25 valid active pass 满足；后续 apply-stage 不重跑 docs-first review，也不改写既有 attempt 证据文件。
- [x] 1.4 [Checkpoint] `refactor-old-2-steering-control` 的 docs-first artifact-completion surface 继续以现有 `verify-sequence/default` pass 为权威结果；caller/orchestrator 仍以 `verification/artifact-completion/agent-table.json` 为唯一状态记录，不新增伪造的 docs-first verifier attempt。

## 2. 320x240 感知与模块化场景迁移

- [x] 2.1 将 steering 感知规范输入明确为 320x240，并同步更新 `new/code/legacy/camera_logic.*`、`new/code/port/control_types.hpp`、`new/code/platform/param_store.cpp`、相关参数默认值与 320x240 fixtures。
- [x] 2.2 将 straight、bend、circle entry、circle interior、circle exit、zebra、cross 拆成独立逻辑模块，由主编排层统一调度；这些逻辑不得继续与直道代码混写。
- [x] 2.3 在 `runtime_state` 与相关输出面中建立 runtime-owned legacy steering state，承接 `highest_line` / `farthest_line` 上下文、prior-cycle controller memory、roadblock 控制接口与状态占位；本次不得实现 roadblock 绕行 steering。
- [ ] 2.4 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for the 320x240 perception port, the modular scene logic split, the runtime-owned steering-state surface, and the roadblock-interface-only surface. Use the verification contract above for field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Require `review_goal=implementation_correctness`; the verifier-subagent must return authoritative findings JSON and verifier evidence JSON for persistence at `openspec/changes/refactor-old-2-steering-control/verification/checkpoint-1/attempt-<n>/findings.json` and `openspec/changes/refactor-old-2-steering-control/verification/checkpoint-1/attempt-<n>/verifier-evidence.json`, and the caller/orchestrator must persist those payloads, reconcile them, and write `openspec/changes/refactor-old-2-steering-control/verification/checkpoint-1/agent-table.json`.

## 3. turn_output 控制迁移与字段裁剪

- [x] 3.1 将 `old_2` 对齐的模糊 P、camera PD、gyro damping、跨周期记忆与 clamp 语义迁入 `new/code/legacy/pid_control.*` 和 `new/code/legacy/fuzzy_pid_ucas.*`，但只面向 `turn_output` 差速末端，不得恢复 servo 语义或 servo 字段。
- [x] 3.2 在 `new/code/runtime/control_loop.*`、`new/code/runtime/control_debug_snapshot.hpp`、`new/code/port/control_types.hpp` 中执行“删旧增必需”的字段更新：复用有价值字段、删除无用字段、只新增实现与验收必需字段，同时保留 raw/applied turn、多周期解释和 assistant-disabled 验收所需最小字段集合。
- [x] 3.2.1 为 `RuntimeParameters`、`PerceptionResult`、`ControlDebugSnapshot::steering`、`control.steering_snapshot` 与 `new/user` 消费方建立一份一一对应的 retained/removed/added 字段合同表，并按该表落地字段改造与脚本同步。
- [x] 3.3 将 reset 路径扩展到 `pid_control`、runtime-owned legacy steering state 和 prior-cycle turn memory，并把字段更新同步到 `new/code/platform/steering_media_protocol.*`、`new/code/runtime/control_debug_reporter.*`、`new/code/runtime/assistant_service.*`、`new/code/runtime/steering_media_service.*` 等调试链路。
- [x] 3.3.1 明确实现并验证 post-reset clean baseline：`highest_line=0`、`farthest_line=0`、`steering_reference_col=160`、`active_module=straight`、`scene_phase=idle`、`scene_override_source=none`、`roadblock_interface_state=supported_not_implemented`、`last_special_scene_correction=none`、roadblock `active=false`、prior-cycle memory 清零、且首个 post-reset 周期满足 `raw_turn_output=0` / `applied_turn_output=0` 或从零记忆状态重新起算。
- [ ] 3.4 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for the turn_output-oriented control port, reset path, multi-cycle carry-over handling, and field-pruning/update surface. Use the verification contract above for field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Require `review_goal=implementation_correctness`; the verifier-subagent must return authoritative findings JSON and verifier evidence JSON for persistence at `openspec/changes/refactor-old-2-steering-control/verification/checkpoint-2/attempt-<n>/findings.json` and `openspec/changes/refactor-old-2-steering-control/verification/checkpoint-2/attempt-<n>/verifier-evidence.json`, and the caller/orchestrator must persist those payloads, reconcile them, and write `openspec/changes/refactor-old-2-steering-control/verification/checkpoint-2/agent-table.json`.

## 4. `new/user` 调试程序、脚本与最终验收

- [x] 4.1 同步更新 `new/user/main.cpp`、`debug.sh`、`steering_media_capture.py`、`tune_steering.py`、`steering_media_selftest.cpp`、相关 README/验证脚本，使其消费更新后的字段集和模块化 steering 语义。
- [ ] 4.2 实现 source-first 验收夹具与测试：320x240 golden-frame 比较（straight、bend、circle entry/interior/exit、zebra、cross）、多周期控制器测试、reset-path 测试、diagnostics tests、assistant-disabled 快照验收，以及 roadblock 接口未启用的非误报证明。
- [ ] 4.3 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for the full source-first change surface after implementation, including changed code, changed tests, directly impacted code, `new/user` scripts, approved docs, shared verification contracts, and runtime evidence. Use the verification contract above for field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Require `review_goal=implementation_correctness`; the verifier-subagent must return authoritative findings JSON and verifier evidence JSON for persistence at `openspec/changes/refactor-old-2-steering-control/verification/checkpoint-3/attempt-<n>/findings.json` and `openspec/changes/refactor-old-2-steering-control/verification/checkpoint-3/attempt-<n>/verifier-evidence.json`, and the caller/orchestrator must persist those payloads, reconcile them, and write `openspec/changes/refactor-old-2-steering-control/verification/checkpoint-3/agent-table.json`.
- [ ] 4.4 [Checkpoint] Run verifier-subagent review using `verify-sequence/default` for the final fixture bundle, diagnostics surface, host-side script compatibility, and differential-drive non-regression proof. Use the verification contract above for field groups in `verification-cycle-core-v1.json` and `verification-cycle-openspec-adapter-v1.json`. Require `review_goal=implementation_correctness`; the verifier-subagent must return authoritative findings JSON and verifier evidence JSON for persistence at `openspec/changes/refactor-old-2-steering-control/verification/checkpoint-4/attempt-<n>/findings.json` and `openspec/changes/refactor-old-2-steering-control/verification/checkpoint-4/attempt-<n>/verifier-evidence.json`, and the caller/orchestrator must persist those payloads, reconcile them, and write `openspec/changes/refactor-old-2-steering-control/verification/checkpoint-4/agent-table.json`. Block closure unless the final active verifier pass proves the actuator terminal remains the current differential-drive path and all `new/user` consumers are aligned to the revised field contract.
