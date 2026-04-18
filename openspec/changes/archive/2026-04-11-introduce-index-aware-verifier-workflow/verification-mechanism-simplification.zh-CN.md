# 验证机制简化交接文档

状态：仅讨论，目标是作为后续新对话的独立交接输入

相关文档：
- `openspec/changes/introduce-index-aware-verifier-workflow/verifier-session-loop.md`

## 文档目的

这份文档不只是记录思路，而是要足够支撑一个新的对话在不依赖当前聊天上下文的情况下，继续推进验证流程简化。

当前工作流真正的问题，不是“权威确认”本身，而是它试图让每一次 rerun 都表现成一次全新的权威审查。这个选择带来了过多的比较机制、重试机制和 preflight 机制，而这些复杂度并不是核心安全目标真正需要的。

## 执行摘要

目标模型是：

- 只有 `fresh confirmation pass` 才能结束一个 checkpoint
- same-session rerun 允许存在，但它本身不是权威结论
- 如果 fresh confirmation pass 仍然发现问题，这个 fresh verifier 就成为新的活动工作会话
- Gemini 只在 fresh confirmation pass 上运行
- preflight 的职责是定义权威 confirmation 的审查面，而不是强制 every-rerun-is-fresh
- 跨 rerun 的 finding 身份识别不再属于核心 gate 规则

一句话概括：

```text
same-session rerun 负责收敛
fresh confirmation 负责裁决
preflight 定义裁决时到底审什么
Gemini 只在策略要求时辅助裁决
```

## 为什么上一版还不够

上一版已经给出了方向判断，但还不足以成为一个新的实现交接材料，因为它缺少：

- 明确的目标不变量
- 明确的文件修改范围
- 哪些机制删除、哪些缩窄、哪些保留的清单
- 迁移顺序
- 完成标准
- 明确的非目标

本版补齐了这些内容。

## 范围

### 在范围内

- 简化共享验证工作流
- 用 session 收敛 + fresh confirmation 取代每轮都 fresh
- 降低 shared contract 的复杂度
- 将 preflight 缩窄为“权威审查面编译器 + trust/deep-scan 准备器”
- 将 Gemini 限制为 confirmation-only

### 不在范围内

- 不改变 `shared-findings-v1` 这个标准 findings envelope
- 不新增第二种 verifier agent
- 不重写 redirect-layer 路由语义
- 不重写 Gemini runner 的传输细节
- 不保留当前全部 rerun 比较机制

## 当前机制总览

当前工作流实际上是几层机制串联在一起：

```text
artifact-verify
  -> Stage 0 index preflight
  -> verifier-subagent review
  -> Gemini（按需）
  -> repair/apply continuation

apply
  -> checkpoint task 里可能重复调用同一套 verify 流程
  -> 所有任务完成后再做 implementation verification

verify-change
  -> Stage 0 index preflight
  -> verifier-subagent review
  -> Gemini（按需）
  -> auto-fix 或 repair routing

repair-change
  -> 按 redirect_layer 路由 findings
  -> 重跑 artifact 或 implementation verification
```

### Stage 0 现在承担过多职责

当前的 shared preflight 同时在做：

- repository-index trust gate
- refresh / bypass / block 决策
- frozen review-surface 编译
- 跨 specs/schema/sequences/tasks 的大范围 contract lint

### Shared verification 现在也承担过多职责

当前 shared verification sequence 把下面几件事绑在了一起：

- 权威确认
- 普通 rerun
- rerun 身份比较
- retry 停止策略

这正是复杂度的核心来源。

## 核心决策

未来工作流应当明确区分“收敛”和“权威”：

```text
same-session rerun
  = 收敛机制

fresh confirmation pass
  = 权威机制
```

这意味着：

- `verify-reviewer` 仍然是唯一 verifier profile
- 活动 verifier session 可以在修复后继续 rerun
- same-session 零 findings 不能直接结束 checkpoint
- same-session 零 findings 只会触发 fresh confirmation pass
- 只有新启动的 verifier session 且返回零 findings，才能结束 checkpoint
- 如果 fresh confirmation 仍然发现问题，它就成为新的活动工作会话

## 目标工作流

```text
1. 启动 fresh verifier session A
2. A 发现问题
3. 修复
4. 在 A 内持续 rerun，直到 A 返回零 findings
5. 启动 fresh confirmation verifier B
6. 重新计算权威 confirmation 审查面
7. 在 B 上执行 verifier review
8. 仅在策略要求时，对 B 运行 Gemini
9. 如果 B 返回零 findings，则结束
10. 如果 B 返回 findings，则 B 成为新的活动工作会话
11. 重复
```

## 必须保持的不变量

简化后的系统必须保留以下不变量。

### 1. 只有 fresh confirmation 才能结束 checkpoint

- 任何 same-session pass 都不能直接完成 gate
- 任何 repair 步骤都不能直接完成 gate

### 2. confirmation 必须基于明确的权威审查面

- confirmation 必须有一组声明清楚的 `required_paths`
- confirmation 必须有一组声明清楚的 `required_axes`
- confirmation evidence 必须证明自己覆盖了这组 surface

### 3. confirmation 仍然必须可审计

- 必须存在权威 findings JSON
- 必须存在权威 execution evidence JSON
- execution evidence 必须指向它所认证的 findings 输出

### 4. Gemini 仍然只是次级角色

- Gemini 不替代 verifier-subagent
- Gemini 只在策略要求的 confirmation pass 中运行

### 5. 路由语义仍然明确

- `redirect_layer` 仍然决定修复路由
- implementation auto-fix 仍然只允许用于显式符合条件的 implementation finding

## 必须保留的内容

这些机制在简化后仍然成立。

### 1. 单一 verifier profile

保留：

- `.codex/agents/verify-reviewer.toml`
- 仅一个 verifier 角色

不要新增第二种 acceptance-only reviewer。

### 2. 权威 review-surface 字段

保留：

- `required_paths`
- `required_axes`

它们对权威 confirmation 仍然是必要的。

### 3. review-completion evidence

保留：

- `verifier_output_path`
- `reviewed_paths`
- `skipped_paths`
- `reviewed_axes`
- `unreviewed_axes`
- `coverage_status`
- `saturation_status`
- 有需要时的 `early_stop_reason`
- 有需要时的 `skip_reasons`

### 4. findings 路由语义

保留：

- `redirect_layer`
- `blocking`
- `auto_fixable`

### 5. Gemini raw/report 恢复逻辑

如果 confirmation pass 中 Gemini 是强制的，就继续保留其 runner recovery contract。

## 应删除的内容

这些项不应继续作为 shared workflow contract 的一部分保留。

### 1. 每次 rerun 都必须 fresh verifier

删除所有等价表述：

- every verify pass must use a fresh verifier instance
- every rerun must use a fresh verifier instance

替换为：

- working rerun 可以留在当前 active verifier session 内
- confirmation rerun 必须使用 fresh verifier session

### 2. 全局跨 rerun 稳定 finding-ID 语义

删除 shared contract 中把“跨 rerun finding ID 稳定”当作默认规则的部分。

原因：

- stable ID 解决的是比较问题
- stable ID 并不定义 authority

### 3. `rerun_context.prior_findings_path` 作为 shared-core 概念

从 shared contract 中删除，除非未来明确重新引入一个可选 comparison feature。

它不应继续属于 minimal shared verifier invocation contract。

### 4. 基于 `id + redirect_layer` 的 repeated-blocker stop 逻辑

删除 shared sequence 中类似这样的规则：

- 同一个 blocking finding 重复出现时停止 auto-fix

如果未来仍然需要，这应当留在局部 orchestration policy，而不是 shared review contract。

### 5. retry budget 作为核心工作流语义

降级：

- `artifact_rerun_budget`
- `implementation_auto_fix_budget`

它们可以继续作为运行时保护存在，但不应继续是 shared verification contract 的核心概念。

## 应缩窄的内容

这些部分仍然有价值，但当前角色过重。

### 1. preflight

保留 preflight，但将其职责缩窄为：

- 编译权威 confirmation 审查面
- 判断 repository-index 输入是否足够可信可用
- 在 confirmation 前声明 deep-scan 要求

preflight 不应继续承担：

- 广义的 artifact-contract lint
- rerun-comparison 引擎
- 强制 every-rerun-is-fresh 的机制

### 2. repository-index policy

将其心智模型简化为：

- 有可信 index -> 用 index 加速并定义审查面
- 没有可信 index -> confirmation 直接对权威 surface 做 source review

这意味着 shared workflow 不应继续重度依赖庞大的
`reused|refreshed|bypassed|block` 分类体系。

### 3. index-maintainer

将 maintainer 的角色缩窄为：

- 维护
- 加速
- 文档 / index 更新

不要再把它当作“权威 review 是否可能”的决定者。

## 目标 shared contract

目标 shared verification contract 应当收缩到类似这样的形态。

### 最小 verifier 调用 bundle

- `change`
- `mode`
- `risk_tier`
- `evidence_paths_or_diff_scope`
- `findings_contract`

可选的 confirmation-only 支持数据：

- `index_context`
- `output_paths`

不再属于 minimal shared contract 的内容：

- 通用 rerun-comparison 输入
- 全局跨 rerun ID continuity 规则
- shared repeated-blocker stop 语义

### confirmation evidence 要求

权威 confirmation 至少需要：

- findings JSON
- execution evidence JSON
- 用于证明 coverage 的 review-completion 字段
- 对 governed review 而言，只在定义 confirmation surface 的意义上需要
  preflight evidence
- 只有在策略要求时才需要 Gemini outputs

## 文件修改范围

后续新对话应预期至少修改以下文件面。

### Shared contracts 和 schema

- `openspec/schemas/ai-enforced-workflow/verification-sequence.md`
- `openspec/schemas/ai-enforced-workflow/index-sequence.md`
- `openspec/schemas/ai-enforced-workflow/schema.yaml`
- `openspec/schemas/ai-enforced-workflow/templates/design.md`
- `openspec/schemas/ai-enforced-workflow/templates/tasks.md`

### Verifier agent surface

- `.codex/agents/verify-reviewer.toml`

### Skills

- `$CODEX_HOME/skills/openspec-artifact-verify/SKILL.md`
- `$CODEX_HOME/skills/openspec-verify-change/SKILL.md`
- `$CODEX_HOME/skills/openspec-repair-change/SKILL.md`
- `$CODEX_HOME/skills/openspec-apply-change/SKILL.md`
- `$CODEX_HOME/skills/openspec-index-preflight/SKILL.md`
- 视情况修改 `$CODEX_HOME/skills/openspec-index-maintain/SKILL.md`

### change 内本地设计文档

如果仓库仍然把本 change 目录中的设计文档当成本地事实来源，也应同步更新：

- `openspec/changes/introduce-index-aware-verifier-workflow/design.md`
- `openspec/changes/introduce-index-aware-verifier-workflow/tasks.md`
- 任何还在复述旧 rerun 模型的 change-local specs

## 迁移顺序

最稳妥的迁移顺序是：

1. 重写 shared verification semantics
   - 删除 every-rerun-is-fresh
   - 定义 confirmation-only authority
2. 重写 preflight semantics
   - 将 Stage 0 缩窄为 surface 编译 + trust/deep-scan 准备
3. 重写 verifier agent instructions
   - 区分 working rerun 和 confirmation
4. 重写 schema/templates
   - 让生成出的工作流文案不再教授旧模型
5. 重写 skills
   - 让 orchestration 行为符合新契约
6. 如有必要，更新 change 内本地 docs/specs
7. 只有在 contract 重新一致后，再重跑验证 evidence

## 完成标准

只有以下条件都满足，才能认为简化完成。

### 契约一致性

- 不再有任何 shared sequence 声称“每次 rerun 都必须 fresh”
- 不再有 schema/template 教授 fresh-on-every-rerun
- 不再有 skill 还把 generic rerun comparison 当作 minimal verifier contract 的一部分

### confirmation 语义

- 工作流只剩一个 stop rule：
  fresh confirmation pass with zero findings
- same-session 零 findings 不再直接结束 checkpoint
- fresh confirmation 如果返回 findings，会把自己变成新的 active working session

### Gemini 策略

- Gemini 只在 confirmation pass 上运行，并且仅在策略要求时运行

### preflight 缩窄

- preflight 不再被描述成庞大的 rerun-governance 机器
- preflight 被描述成权威审查面编译器 + trust/deep-scan 准备器

### comparison 简化

- 跨 rerun stable finding ID 不再属于 shared gate law
- repeated-blocker detection 不再属于 shared gate 规则
- retry budgets 不再被描述成 workflow 核心语义

## 后续新对话的验证检查清单

后续实现对话在宣称完成之前，应显式检查以下几点。

- 搜索 `fresh verifier instance`、`no inherited verifier memory` 等旧表述，确认它们已从 schema、sequence、template、skill 中按新语义更新。
- 搜索 `prior_findings_path`、repeated-finding detection、stable-ID 等表述，确认它们不再作为 shared core 规则存在。
- 搜索 Gemini 相关表述，确认它只绑定到 confirmation pass。
- 搜索 preflight 相关表述，确认 Stage 0 不再被描述成大而全的 contract-lint gate。
- 检查 verifier agent instructions，确认它不再暗示 every rerun 都是 authoritative。
- 检查 generated guidance，确认 `required_paths`、`required_axes` 和
  review-completion evidence 仍被保留。

## 新对话建议起始提示词

后续实现对话可以直接从下面这段开始：

```text
Use `openspec/changes/introduce-index-aware-verifier-workflow/verification-mechanism-simplification.md`
as the source of truth.

Goal: replace the fresh-on-every-rerun verification model with:
- same-session reruns for convergence
- fresh confirmation pass as the only authoritative stop condition
- Gemini only on fresh confirmation
- preflight narrowed to confirmation-surface compilation and trust/deep-scan preparation

Do not preserve global cross-rerun stable-ID semantics, generic `prior_findings_path`,
or repeated-blocker stop rules unless they are reintroduced as explicitly optional
non-core features.
```

## 最终结论

是的，经过这次更新之后，这份英文文档已经足够支撑一个新的对话在不依赖当前聊天记录的前提下继续执行我们讨论后的方向。

它仍然是设计交接文档，不是可执行代码；但现在已经具备：

- 目标模型
- 保留 / 删除 / 缩窄清单
- 预期修改文件面
- 迁移顺序
- 完成标准
- 下一轮对话的起始 brief
