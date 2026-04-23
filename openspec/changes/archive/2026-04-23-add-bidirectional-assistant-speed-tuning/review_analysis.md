# Review: `add-bidirectional-assistant-speed-tuning`

## 评审目标

对照你的两个根本需求进行验证：

1. **上位机调整下位机参数**（当前为目标速度）
2. **上位机 AI 辅助调整 PID 数值**

同时审查设计是否足够优雅。

---

## 一、需求覆盖度判定

### 需求 1：上位机调整下位机的目标速度 ✅ 完全覆盖

| 设计要素 | 覆盖状况 | 对应位置 |
|---|---|---|
| TCP 双向通道 | ✅ 在已有 `assistant_bridge` 上叠加 JSON-line 命令层 | [design.md L87-131](file:///home/madejuele/projects/2K0300/openspec/changes/add-bidirectional-assistant-speed-tuning/design.md#L87-L131) |
| `set_target_speed` 命令 | ✅ 有明确字段、值域、TTL | [design.md L352-360](file:///home/madejuele/projects/2K0300/openspec/changes/add-bidirectional-assistant-speed-tuning/design.md#L352-L360) |
| 运行时覆盖与回退 | ✅ volatile tuning state + TTL 过期回退到 `Speed_base` | [design.md L143-197](file:///home/madejuele/projects/2K0300/openspec/changes/add-bidirectional-assistant-speed-tuning/design.md#L143-L197) |
| 远程启停 | ✅ `start`/`stop` 走 motion intent，不绕过生命周期 | [design.md L199-252](file:///home/madejuele/projects/2K0300/openspec/changes/add-bidirectional-assistant-speed-tuning/design.md#L199-L252) |
| ACK/state 反馈 | ✅ 明确的 accepted/rejected/cleared 语义 | [design.md L373-406](file:///home/madejuele/projects/2K0300/openspec/changes/add-bidirectional-assistant-speed-tuning/design.md#L373-L406) |
| 结构化遥测 | ✅ 完整的 wheel-level target/measured/pwm 字段 | [design.md L408-426](file:///home/madejuele/projects/2K0300/openspec/changes/add-bidirectional-assistant-speed-tuning/design.md#L408-L426) |
| 上位机工具 | ✅ Python + matplotlib 实时绘图 + CSV 保存 | [design.md L327-340](file:///home/madejuele/projects/2K0300/openspec/changes/add-bidirectional-assistant-speed-tuning/design.md#L327-L340) |

**结论：** 完整覆盖了"上位机运行时调整目标速度"的整条链路。

---

### 需求 2：上位机 AI 辅助调整 PID ⚠️ 部分覆盖 / 存在缺口

> [!IMPORTANT]
> 这是本次审查发现的**最核心缺口**。

| 所需能力 | 设计中是否覆盖 | 说明 |
|---|---|---|
| PID 参数运行时热写 | ❌ **明确列为 Non-Goal** | [design.md L78](file:///home/madejuele/projects/2K0300/openspec/changes/add-bidirectional-assistant-speed-tuning/design.md#L78): "No online hot-write path for PID parameters in the first release; PID updates still go through parameter files plus restart." |
| PID 调参的数据底座 | ✅ 间接覆盖 | 结构化遥测提供了 target/measured/pwm 数据，host 可以 CSV 保存多次跑圈数据 |
| AI 分析 PID 效果 | ⚠️ 仅有基础设施，无 AI 工作流 | Python 脚本 + CSV + matplotlib 提供了数据，但**没有任何 AI/自动推荐 PID 参数的流程** |
| PID 闭环迭代 | ❌ 缺失 | 当前流程：修改 JSON → 重启 → 观测 → 手动分析。没有 AI 参与其中任何一环 |

**具体分析：**

设计把你的需求拆成了两步走：
1. **第一步（本变更）：** 建立"速度调参"的数据采集和远程控制基础设施
2. **第二步（未来）：** 在此基础上构建 AI 辅助 PID 调参

这个分步策略**本身不是问题**，但设计文档完全没有表达"第二步"的存在——Non-Goals 部分直接写了 "No online hot-write path for PID" 和 "No attempt to auto-tune PID gains on-device"，让人觉得 AI 辅助 PID 根本不在计划内。

---

## 二、架构优雅度评审

### 做得好的地方 ✅

**1. 分层清晰，边界锐利**

```
TCP transport (assistant_bridge)
    ↓ raw bytes
Project-owned decoder (assistant_link + parser)
    ↓ typed commands
Runtime consumer (assistant_service)
    ↓ motion intent / tuning state
Control loop (reads snapshot only)
```

这一分层是整个设计最优雅的部分。当前代码中 `assistant_link.cpp` L75-81 的 `ignored_receive` 处理已经预留了接收流量的扩展点，变更不需要重构 transport 层。

**2. 运行时覆盖与静态参数的显式隔离**

```
Static: default_params.json → RuntimeParameters (只在启动时加载)
Volatile: RuntimeTuningState (TTL、freshness、explicit clear paths)
```

这避免了 old_2 时代的 `settings.txt` 混乱——静态配置和运行时调参从模型层就是分开的。

**3. Turn suppression 设计精炼**

raw turn 照常计算，只在注入 wheel target mixer 前置零——单个 clamp 节点，不侵入 legacy PID 代码。干净。

**4. 断线/超时安全**

disconnect → clear tuning snapshot 是正确的 fail-safe pattern，不会在失联后残留调参状态。

---

### 存在的问题 ⚠️

**1. 过度工程化的协议层**

> [!WARNING]
> 对一个发送 6 种命令的 JSON-line 协议来说，design.md 花了约 120 行（L342-468）定义 wire contract，包括 5 种 ACK outcome、4 种 state event、显式的 disabled-mode rejection 语义……这在 **首发版本** 里过重了。

当前你的实际交互模式是：
- 人手动启动 Python 脚本
- 发几条命令（enable_tuning → start → set_speed → stop → disable_tuning）
- 看图、存 CSV

这不是一个需要 `superseded` outcome 或 `override-ttl-expired` reason code 的多客户端系统。

**2. 验证体系与核心功能的比例失调**

[tasks.md](file:///home/madejuele/projects/2K0300/openspec/changes/add-bidirectional-assistant-speed-tuning/tasks.md) 中 4 个 checkpoint 的验证描述（1.4, 2.5, 3.5, 4.3）**每个都有 15-20 行纯验证流程描述**，远超实际功能实现描述。整个 tasks.md 的 58 行中，约 **60% 是验证流程模板文本**，与实际工程产出不成比例。

**3. spec 形式化程度偏高**

[spec.md](file:///home/madejuele/projects/2K0300/openspec/changes/add-bidirectional-assistant-speed-tuning/specs/runtime-speed-tuning-surface/spec.md) 用了 212 行写 12 个 WHEN/THEN/AND 场景。对于一个嵌入式项目的内部调参工具来说，这个规格书更像是在写供应商交付合同而非工程设计。

---

## 三、关键建议

### 建议 A：补上 AI 辅助 PID 的显式 Roadmap（高优先级）

你的需求写得很清楚——"上位机 AI 辅助调整 PID 的数值"。设计应当至少在以下层面做出承诺：

```
┌─────────────────────────────────────────────────┐
│  当前变更 (Phase 1)                              │
│  ✅ 速度覆盖 + 遥测 + CSV 数据采集               │
│  ✅ 上位机可以远程启停 + 设置不同速度跑圈          │
└──────────────────────┬──────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────┐
│  下一步变更 (Phase 2) — 当前缺失                 │
│  ⬜ PID 参数热写命令 (set_wheel_pid)             │
│  ⬜ 上位机 AI 分析 CSV → 推荐 PID 参数           │
│  ⬜ 推荐参数自动下发 → 观测效果 → 迭代            │
│  ⬜ 收敛后持久化到 default_params.json            │
└─────────────────────────────────────────────────┘
```

最小可行动作：在 design.md 的 **Open Questions** 或新增 **Future Work** 部分加一段：

> PID 参数运行时热写和 AI 辅助推荐是预期的后续变更。本变更的结构化遥测和 host 工具是该后续变更的数据基础。具体的 `set_wheel_pid` 命令和 AI 推荐工作流将在独立 change 中覆盖。

### 建议 B：精简首发协议（中优先级）

首发版本的协议可以更实用：

| 当前设计 | 建议精简 |
|---|---|
| 5 种 ACK outcome (accepted/rejected/ignored/cleared/superseded) | 首发保留 `accepted` + `rejected` 即可 |
| 4 种 state event | 首发只需 `tuning_mode_changed` + `override_cleared` |
| `set_target_speed.value` 的 5 种错误分类 | 统一为 `rejected` + `reason` 字符串 |
| TTL 机制 | 保留，但实现中可以用简单的心跳检测替代精确 TTL |

### 建议 C：考虑 PID 热写的前置准备（低优先级，但值得思考）

当前 `control_loop.cpp` 在 `Start()` 时从 `params_` 配置 PID（L368-375），之后 `params_` 是只读的。如果 Phase 2 要做 PID 热写，需要在 timer ISR (`Tick()`) 中安全读取新参数——这意味着要么：

- 在 `RuntimeTuningState` 中预留 PID override 字段（但本变更 Non-Goal 写了不做）
- 后续再加（但要评估 `Tick()` 中重新 Configure PID 的线程安全性）

至少在 design.md 的 Risks/Trade-offs 中提一句"PID 热写需要额外评估 timer-callback 中的线程安全影响"。

---

## 四、总结判定

| 维度 | 评级 | 说明 |
|---|---|---|
| 需求 1 覆盖 | ⭐⭐⭐⭐⭐ | 完整、严谨、有安全回退 |
| 需求 2 覆盖 | ⭐⭐☆☆☆ | 仅提供了数据基础设施，AI 辅助 PID 的核心流程缺失，且未在文档中承诺后续 |
| 分层架构 | ⭐⭐⭐⭐⭐ | transport → decoder → typed command → runtime 分层清晰 |
| 协议设计 | ⭐⭐⭐☆☆ | 正确但对首发来说过重 |
| 文档比例 | ⭐⭐☆☆☆ | 验证模板文本和规格化语言占比过大，淹没了核心工程决策 |
| 实现可行性 | ⭐⭐⭐⭐☆ | 当前代码结构天然支持这个变更，插入点明确 |

> [!IMPORTANT]
> **核心结论：** 这个变更对"需求 1"是一个高质量的设计。但对于你说的"AI 辅助调整 PID"，它只建立了数据采集的地基，而把整栋楼都推到了未来——并且连"这栋楼存在"都没有在文档中提及。建议至少补一个 Future Work 段落，让这个变更与你的完整需求链路建立显式连接。
