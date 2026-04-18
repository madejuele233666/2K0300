# introduce-verify-subagent-workflow 实施记录

说明：这是 `introduce-verify-subagent-workflow` 的历史实施记录，不是
当前 `ai-enforced-workflow` 的完整单一规范。当前生效工作流以
`openspec/schemas/ai-enforced-workflow/verification-sequence.md`、
`openspec/schemas/ai-enforced-workflow/index-sequence.md` 和
`openspec/schemas/ai-enforced-workflow/schema.yaml` 为准。

当前 Stage A 口径：
- artifact review 与 implementation review 显式分离
- repository-index 只作为可选 cache helper
- implementation review 优先 resume 可用 `active` verifier
- 只有 `block -> pass` 才会把 agent 标记为 `non_active`
- 终止只依赖当前 `active` verifier 的有效 `pass`
- `review_scope` / `review_coverage` 内嵌在 implementation
  `verifier-evidence.json`

本文件后续若提到 Stage 0、fresh confirmation、冻结 `required_paths` /
`required_axes` 等表述，均应视为旧实现历史，不再代表当前权威合同。

更新时间：2026-04-12

## 1. 目标与结论

本次变更目标是把 `ai-enforced-workflow` 的验证流程从旧口径：

- `Codex local review first`
- `Gemini independent verification second`

统一迁移为新口径：

- 共享序列 `verify-sequence/default`
- 只读验证子代理 `verify-reviewer` 先行本地审查
- Gemini 作为 second opinion，仅在 fresh confirmation pass 且 checkpoint 为 `STRICT` 或显式 dual-gated 时强制执行
- 自动修复仅允许在 `redirect_layer=implementation && auto_fixable=true`

当前状态：

- `openspec instructions apply --change "introduce-verify-subagent-workflow" --json` 返回 `all_done`
- 主任务 `tasks.md` 为 `10/10` 完成
- re-verify 为 `pass_with_warnings` 后，已按 warning 修复 artifact-generation 辅助技能中的旧口径漂移点

## 2. 本次完成的核心实现

### 2.1 新增 verifier 子代理与共享序列

- 新增：`.codex/agents/verify-reviewer.toml`
  - 只读
  - 不继承完整父上下文
  - 输出强制为 normalized JSON
- 新增：`openspec/schemas/ai-enforced-workflow/verification-sequence.md`
  - 定义 `verify-sequence/default`
  - 单点定义最小验证 bundle：
    - `change`
    - `mode`
    - `risk_tier`
    - `evidence_paths_or_diff_scope`
    - `findings_contract`
  - 定义 same-session 收敛 + fresh confirmation 裁决、Gemini confirmation-only 触发策略、runner contract 与恢复路径

### 2.2 重构 schema 与模板

- 更新：`openspec/schemas/ai-enforced-workflow/schema.yaml`
  - 替换旧的 Codex-first 叙述
  - 引入 shared sequence 引用
  - 风险分级描述更新为 verifier-subagent-first + tier-gated Gemini
  - apply 规则明确 auto-fix gate 与双预算
- 更新：`openspec/schemas/ai-enforced-workflow/templates/design.md`
  - 改为 shared-sequence 驱动模板
  - 删除平台命令硬编码，保留逻辑 runner contract
- 更新：`openspec/schemas/ai-enforced-workflow/templates/tasks.md`
  - checkpoint 任务模板改为逻辑合同写法
  - 平台命令仅进入 execution evidence，不写入策略正文

### 2.3 重构 verify/apply/repair 技能口径

- 更新：`$CODEX_HOME/skills/openspec-artifact-verify/SKILL.md`
  - 改为 thin entrypoint（mode/evidence/report path/routing target）
  - 编排委托给 `verify-sequence/default`
- 更新：`$CODEX_HOME/skills/openspec-verify-change/SKILL.md`
  - 同上，改为 thin entrypoint
- 更新：`$CODEX_HOME/skills/openspec-apply-change/SKILL.md`
  - 明确 verifier-subagent 先行
  - Gemini 仅按策略触发
  - 自动修复 gate 与分离重试预算
- 更新：`$CODEX_HOME/skills/openspec-repair-change/SKILL.md`
  - 输入/合并/收敛语言改为 verifier-subagent-first
  - rerun 流程改为 shared sequence + tier-gated Gemini

### 2.4 修复 re-verify 的 implementation warning（artifact-generation 技能漂移）

根据 re-verify 报告，补齐了 3 个辅助技能中的旧口径：

- `$CODEX_HOME/skills/openspec-propose/SKILL.md`（约第 101 行）
- `$CODEX_HOME/skills/openspec-ff-change/SKILL.md`（约第 92 行）
- `$CODEX_HOME/skills/openspec-continue-change/SKILL.md`（约第 116 行）

均已改为：

- `verify-sequence/default`
- `verify-reviewer` 本地审查先行
- Gemini 仅在 `STRICT` 或显式 dual-gated checkpoint 强制

## 3. Checkpoint 与验证证据

证据目录：

- `openspec/changes/introduce-verify-subagent-workflow/verification/`

### 3.1 Checkpoint 1（schema/contract）

- 本地子代理评审：
  - `schema-contract-subagent-review.json`
- Gemini 输出：
  - `checkpoint-1-gemini-raw.json`
  - `checkpoint-1-gemini-report.json`

### 3.2 Checkpoint 2（模板与 verify skill 入口重构）

- 本地子代理评审：
  - `entrypoint-subagent-review.json`
- Gemini 输出：
  - `checkpoint-2-gemini-raw.json`
  - `checkpoint-2-gemini-report.json`

### 3.3 Checkpoint 3（repair/apply 流程）

- 本地子代理评审：
  - `repair-loop-subagent-review.json`
- Gemini 输出：
  - `checkpoint-3-gemini-raw.json`
  - `checkpoint-3-gemini-report.json`

### 3.4 Runner 恢复与运行证据

- 恢复示例输出：
  - `recovery-demo-report.json`
- 运行命令与证据汇总：
  - `execution-evidence.md`
- 结构化补充证据：
  - `schema-driven-invocation-evidence.json`
  - `retry-budget-exercise.json`
  - `exercise-evidence.md`

### 3.5 on-demand recheck 报告

你提供并用于修复闭环的 recheck 证据：

- `on-demand-implementation-subagent-review-recheck.json`
- `on-demand-implementation-gemini-report-recheck.json`
- `on-demand-implementation-gemini-raw-recheck.json`

## 4. 关键策略（当时该 rollout 的实施口径）

1. 本地评审角色
- 必须是只读 `verify-reviewer` 子代理，且使用最小 bundle

2. Gemini 触发
- `STRICT` 或显式 dual-gated checkpoint：仅 fresh confirmation pass 必跑
- 其他场景：也只允许在 fresh confirmation pass 上作为可选 second opinion

3. 自动修复边界
- 仅当 `redirect_layer=implementation && auto_fixable=true`
- 非 implementation 或 `auto_fixable=false` 一律路由到 repair 流程

4. 重试预算
- artifact rerun 与 implementation auto-fix loop 独立计数，不互相消耗

5. 平台命令治理
- 策略文本写 logical runner contract（`gemini-capture`）
- Linux/Windows 解析命令只记录到执行证据，不回写策略正文

6. 子代理与技能边界
- 子代理负责有边界的执行单元，不负责流程编排
- `verify-reviewer` 只做只读审查；working session 内可 same-session rerun，只有 fresh confirmation pass 需要新 verifier session；若 fresh confirmation 仍返回 findings，该 session 接管为新的 active working session
- `index-maintainer` 只做 repository-index 维护，可共享主流程上下文，但不能输出最终 review verdict
- skills 负责阶段编排、路径归一化、证据落盘和子代理之间的交接

## 5. 当前剩余缺口（非阻塞）

Gemini 标注的残余测试缺口仍存在：

- 目前仅做了 Windows/Linux 命令解析的静态文档核查
- 未完成跨平台命令解析的运行时验证（runtime validation）

这不阻塞当前修复，但如果要清零 warning，建议新增专门的跨平台 runner-resolution 运行测试任务与日志证据。

## 6. 建议的下一步

1. 触发一次新的 implementation re-verify（子代理 + Gemini）确认 warning 清零。
2. 若目标是无 warning，再补一条 Windows/Linux 命令解析运行时验证证据。
3. 通过后执行归档流程（`openspec-archive-change`）。
