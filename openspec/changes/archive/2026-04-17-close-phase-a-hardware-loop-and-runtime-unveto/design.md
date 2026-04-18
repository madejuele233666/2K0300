## Context

当前 `new/` runtime 已经完成了几项关键收口：

- 默认 `direct-match` profile 已能到达 `imu.init`、`imu.detect`、`startup.complete`、`control.start`
- 执行器 public contract 已经收口为纯差速 `left_pwm + right_pwm + emergency_stop`
- `runtime` 已经具备 project-owned 的 `control.veto.*`、`control.apply.*`、`control.arm.transition` 观测边界

因此，本 change 不再解决“系统能不能启动”或“边界该怎么分层”这类前置问题。它处理的是 Phase A 的剩余闭环义务：把 IMU 样本可信度、编码器方向与量级、电机真实出力与 fail-safe 回退、`control.veto` 稳定解锁、以及相机曝光策略，从零散事实推进为一个统一、可审计、可阶段判定的收口 change。

同时，当前仓库已经有足够清晰的边界约束：

- `new/` 是唯一有效迁移工作区
- `legacy` 继续消费 project-owned contracts
- vendor 头文件、路径和 free function 只能留在 platform-owned bridge
- `run_remote_smoke.sh` 与 phase-scoped verification artifacts 仍是主验证入口

这意味着本 change 的设计重点不是引入新架构，而是让现有架构优雅地承载剩余闭环工作。

## Goals / Non-Goals

**Goals:**

- 用一个主 change 收口 Phase A 剩余硬件闭环，而不是拆成多个相互引用的小 change。
- 让 Phase A 子任务按控制关键路径推进：先传感器可信，再执行器可信，再控制解锁可信，最后定论曝光策略。
- 保持现有 project-owned 边界，不为取证而把 vendor 细节泄漏到 `runtime` 或 `legacy`。
- 让 Phase A 的阶段结论可审计：既能说清 accepted baseline 已证明什么，也能说清仍未证明什么。
- 让后续 implementation/review 可以按 checkpoint 留下板测证据，而不是依赖单次“大而全”的烟囱式板测。

**Non-Goals:**

- 不引入新的比赛能力、图片识别能力或 `Phase B` 低速实车运动能力。
- 不重写现有 `legacy` 控制算法、感知算法或 vendor bridge 布局。
- 不把 adaptation-hook 诊断路径提升成比赛主线。
- 不新增新的运行入口、部署入口或独立的实验工作区。
- 不在 design 层宣称 Phase A 已通过；结论必须由后续实现与证据产生。

## Decisions

### Decision: Use one Phase A closure change with ordered checkpoints

- Problem being solved:
  当前剩余问题横跨 IMU、编码器、电机、控制解锁和曝光策略。如果把它们拆成多个平级 change，会产生前后依赖交叉、证据互相引用、阶段判断分裂的问题。
- Alternatives considered:
  为 IMU/encoder、motor/unveto、exposure 分别创建独立 change；继续只维护阶段文档而不创建主 change；创建一个主 change 并在其中按 checkpoint 分解任务。
- Why this option was chosen:
  剩余问题都服务于同一个阶段退出条件，而且有明显依赖顺序。一个主 change 更适合表达“一个阶段收口，多个检查点”，同时避免把 Phase A 的结论拆散到多个 archive 中。
- Stack Equivalent:
  `accepted baseline -> sensor closure -> actuator closure -> control unlock closure -> exposure policy decision -> Phase A exit judgment`
- Named Deliverables:
  `proposal.md`, `design.md`, `tasks.md`, change-local delta specs, 以及 phase-scoped evidence notes/logs for IMU, encoder, motor, control unlock, and exposure.
- Failure Semantics:
  任一 checkpoint 未闭环，都意味着 change 不能宣称 Phase A 通过；但失败只回退到相应 checkpoint，不重置已接受的 direct-match startup baseline。
- Boundary Examples:
  IMU 方向问题阻塞 IMU checkpoint，但不推翻“系统已能 direct-match 启动”。
  电机真实出力未验证阻塞 Phase A exit，但不要求重新设计 actuator contract。
- Contrast Structure:
  Before: 多个零散事实、归档 change 和路线文档共同描述 Phase A，阶段结论分散。
  After: 一个主 change 统一承载 Phase A 剩余闭环，每个 checkpoint 各自产生证据和阶段判断。
- Verification Hook:
  `tasks.md` 必须按 checkpoint 组织，而不是按 loosely related code area 平铺；reviewers 必须能明确看到每个 checkpoint 的进入条件、产物和失败回退。

### Decision: Order the work by control-critical dependency, not by subsystem popularity

- Problem being solved:
  如果先处理“看起来显眼”的问题，例如曝光策略或大文档整理，而不是先处理控制关键路径上的传感器可信度，后续控制解锁结论会继续不稳定。
- Alternatives considered:
  按硬件种类平铺推进；按文档章节顺序推进；按控制关键路径推进。
- Why this option was chosen:
  当前 `control_loop` 的 gate 顺序已经把依赖关系写清楚了：perception freshness / low voltage / perception veto / IMU valid / encoder valid。Phase A 的推进顺序必须尊重这条链。
- Stack Equivalent:
  `IMU valid + encoder valid + perception sane + motor apply -> stable non-veto interval`
- Named Deliverables:
  IMU closure evidence, encoder closure evidence, motor closure evidence, control-unveto evidence, camera-exposure decision note.
- Failure Semantics:
  如果上游 checkpoint 未通过，下游 checkpoint 只能做诊断预采样，不能给出“已闭环”的正式结论。
- Boundary Examples:
  `control.veto.imu_invalid` 频发时，不应直接调 PID 或讨论 Phase B 行为。
  编码器符号未确认时，不应把 `measured_speed` 视为可信闭环速度反馈。
- Contrast Structure:
  Before: 可能按文档顺序或设备顺序来回切换，导致结论互相污染。
  After: IMU -> encoder -> motor -> control unlock -> exposure 的顺序与控制可信度形成严格依赖。
- Verification Hook:
  `tasks.md` 中后续 checkpoint 的成功标准必须引用前序 checkpoint 的闭环前提，而不是假定它们已经成立。

### Decision: Keep corrections localized to the owning boundary

- Problem being solved:
  Phase A 剩余问题里最容易失控的，是为了快速取证把 vendor 细节、设备路径、轴向修正或诊断分支直接塞进 `runtime` 或 `legacy`。
- Alternatives considered:
  直接在 `control_loop` 或 `legacy` 中加硬件特判；只在 board logs 中解释而不调整代码边界；把问题定位到 owning boundary 后局部修正。
- Why this option was chosen:
  当前仓库的长期价值来自 adapter/bridge/runtime/legacy 的分层。如果为了 Phase A 收口破坏边界，后续 Phase B/C 的复杂度会立刻反弹。
- Stack Equivalent:
  `bridge resolves/raw-normalizes resource -> adapter publishes project-owned sample -> runtime evaluates gates -> docs/evidence interpret checkpoint result`
- Named Deliverables:
  可能的 bridge/adapter fixes, runtime-owned diagnostics refinements if needed, and doc/evidence updates. No vendor leakage into public contracts.
- Failure Semantics:
  如果某个问题无法在 owning boundary 内优雅解决，应显式记录为 open question 或 adaptation boundary，而不是用跨层 hack 暂时掩盖。
- Boundary Examples:
  IMU 方向或零偏问题优先修在 `imu_bridge`/`imu_adapter`。
  `control.veto` 解释留在 runtime-owned observability，不把 raw device-node details 暴露给 Phase A docs。
- Contrast Structure:
  Before: 取证压力可能诱发跨层快捷修复。
  After: 每个问题都先问“它属于 bridge、adapter、runtime 还是 docs/evidence”，再决定改动位置。
- Verification Hook:
  delta specs and implementation review MUST confirm that vendor headers, raw `/dev/zf_*` paths, and board-specific hacks do not leak into `new/code/runtime/*` or `new/code/legacy/*`.

### Decision: Treat exposure policy as a closure decision, not an architectural rewrite

- Problem being solved:
  曝光问题确实影响 perception veto，但它在当前 Phase A 中属于“主线策略定论”而不是“新感知系统设计”。如果把它上升成独立架构课题，会拖慢整个阶段收口。
- Alternatives considered:
  先单独拆出曝光 change；在本 change 内把曝光视为最后一个 closure checkpoint；把曝光问题继续悬空。
- Why this option was chosen:
  当前需要的是明确回答：direct-match 路径是否支持真实曝光控制；如果不支持，当前主线怎么运行、怎么记录限制。这个问题可以在同一个 Phase A 收口 change 内完成，而无需另起架构主题。
- Stack Equivalent:
  `camera direct-match capability -> perception threshold behavior -> exposure policy decision`
- Named Deliverables:
  exposure decision note, sample analysis evidence, and any minimal code/docs updates needed to make the policy explicit.
- Failure Semantics:
  即使 exposure policy 不能变得更强，也必须给出明确、可执行的当前主线结论；不能继续保持“以后看”。
- Boundary Examples:
  如果 vendor API 不支持曝光控制，允许结论是“固定曝光 + 环境约束 + 图像预处理”为当前 Phase A 主线。
  不允许把 exposure 未决状态伪装成“不会影响 Phase A 退出”。
- Contrast Structure:
  Before: 曝光问题是开放事项，容易与感知算法改造混在一起。
  After: 曝光问题被收口为一个明确的 Phase A closure decision，与新能力设计解耦。
- Verification Hook:
  docs and evidence MUST state whether direct-match exposure control is supported, unsupported, or routed through a named adaptation boundary, and MUST tie that statement to perception/emergency-veto behavior.

### Decision: Exposure policy may be promoted earlier when it is the dominant unlock blocker

- Problem being solved:
  虽然曝光策略本质上是一个 closure decision，但它直接影响 perception freshness 和 `perception_emergency_veto`。如果控制长窗口的 dominant veto cause 已经指向 perception/exposure，再把曝光固定放在最后处理，会让 control-unlock 任务顺序自相矛盾。
- Alternatives considered:
  始终把曝光放在最后；一开始就把曝光独立成先行任务；默认后置，但允许在 control diagnostics 指向 perception/exposure 时前置处理。
- Why this option was chosen:
  这保留了“曝光不是新的架构主题”的边界，同时尊重 control gate 的实际依赖链。只有在 exposure/perception 已经成为主阻塞时，才把它从最后一个 closure decision 提前为当前阻塞修复。
- Stack Equivalent:
  `control diagnostics -> dominant veto cause classification -> if perception/exposure dominates, resolve exposure policy -> resume final control-unlock judgment`
- Named Deliverables:
  updated control-unveto note when needed, `phase-a-camera-exposure.*`, and final `phase-a-exit-judgment.md`.
- Failure Semantics:
  如果 dominant veto cause 是 perception/exposure，则任何“stable control unlock”结论都必须暂停，直到曝光策略被明确并重新采样；不能跳过该依赖直接宣称 control unlock 已收口。
- Boundary Examples:
  `control.veto.perception_emergency` 持续主导时，应先完成 exposure policy decision 再给出最终 unveto 结论。
  `control.veto.imu_invalid` 主导时，不应被曝光问题打断当前 checkpoint。
- Contrast Structure:
  Before: 曝光被固定成最后一步，可能与真实 blocker 顺序冲突。
  After: 曝光默认后置，但一旦成为 dominant blocker，就被显式提升为当前修复前提。
- Verification Hook:
  `tasks.md` and final evidence MUST state whether exposure remained a final closure decision or was promoted earlier because perception/exposure dominated the control-unlock blocker profile.

## Independent Verification Plan (STANDARD/STRICT)

Document verification using shared sequence `verify-sequence/default` from:
`openspec/schemas/ai-enforced-workflow/verification-sequence.md`
and shared JSON verification contract:
`openspec/schemas/modules/review-loop/contracts/review-loop-core-v1.json`
and OpenSpec subject adapter:
`openspec/schemas/modules/review-loop/contracts/review-loop-openspec-adapter-v1.json`

Stage A flow:
- Shared loop semantics:
  - every checkpoint uses `review_goal=implementation_correctness`
  - `review_phase=docs_first` for document-primary checkpoints
  - `review_phase=source_first` for code/test/impacted-source checkpoints
  - same-session working reruns handle convergence
  - challenger is always fresh and is the only closure authority
- Docs-first gate:
  - `proposal.md`, `design.md`, `tasks.md`, and change-local `specs/**` are the
    primary surface
  - passing challenger review closes the docs-first gate and allows
    implementation entry
  - blocking findings stop implementation entry and reroute to docs repair
- Source-first checkpoints:
  - changed code, changed tests, directly impacted code, and checkpoint evidence
    are the primary surface
  - approved docs are reference material
  - repository-index support is optional cache help only

Runtime profile policy:
- Use verifier runtime profile from `.codex/agents/verify-reviewer.toml`.
- Use index-maintainer runtime profile from
  `.codex/agents/index-maintainer.toml` when cache refresh is useful.

Loop rule:
- Same-session working reruns MAY continue inside the active working reviewer
  session after repair.
- Working zero findings are convergence-only.
- Challenger MUST run in a newly spawned verifier session.
- If challenger returns findings, write `review-loop-reopen-record-v1.json` and
  promote that challenger session into the next working baseline.
- Only challenger with zero findings may close a checkpoint.
- Only the main orchestrator may authorize challenger entry or closure, and it
  must not substitute its own judgment for the prior working sub-agent output.
- Callers MUST normalize resolver input and execute the shared transition
  resolver decision exactly instead of improvising transitions in prompt prose.

### Docs-First Change-Bundle Verification

- Sequence reference: `verify-sequence/default`
- Review goal: `implementation_correctness`
- Review phase: `docs_first`
- Review pass types:
  - `working`
  - `challenger`
- Verifier agent path: `.codex/agents/verify-reviewer.toml`
- Invocation template id: `verify-reviewer-inline-v2`
- Primary review surface: `proposal.md`, `design.md`, `tasks.md`, and change-local `specs/**`
- Default cache mode: `bypassed`
- Working summary paths:
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/docs-first-working-spawn-decision.json`
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/docs-first-working-findings.json`
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/docs-first-working-evidence.json`
- Challenger summary paths:
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/docs-first-challenger-findings.json`
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/docs-first-challenger-evidence.json`
- Required run layout:
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/review-runs/docs-first-gate/working/attempt-1/spawn-decision.json`
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/review-runs/docs-first-gate/working/attempt-<n>/findings.json`
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/review-runs/docs-first-gate/working/attempt-<n>/verifier-evidence.json`
  - later `working/attempt-<n>/spawn-decision.json` is required only when an exception forces a fresh working session
  - `working/attempt-<n>/reopen-record.json` is required whenever challenger findings promote the loop into the next working baseline
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/review-runs/docs-first-gate/challenger/attempt-<n>/spawn-decision.json`
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/review-runs/docs-first-gate/challenger/attempt-<n>/findings.json`
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/review-runs/docs-first-gate/challenger/attempt-<n>/verifier-evidence.json`
- Acceptance rule: implementation entry is allowed only after docs-first
  challenger returns zero findings with `closure_authority=challenger_confirmed`
- Skill entry point: `openspec-artifact-verify`

### Source-First Checkpoint Verification

- Sequence reference: `verify-sequence/default`
- Cache-helper sequence reference: `index-sequence/default`
- Review goal: `implementation_correctness`
- Review phase: `source_first`
- Review pass types:
  - `working`
  - `challenger`
- Verifier agent path: `.codex/agents/verify-reviewer.toml`
- Index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- Invocation template id: `verify-reviewer-inline-v2`
- Primary review surface: changed code, changed tests, directly impacted runtime/platform/legacy/docs files, and change-local evidence files
- Optional cache-helper report path:
  `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/index-preflight.json`
- Authoritative verifier-subagent findings JSON path:
  `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/implementation-<pass>-findings.json`
- Verifier execution evidence JSON path:
  `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/implementation-<pass>-evidence.json`
- Inline implementation evidence:
  - `review_scope`
  - `review_coverage`
- Loop behavior:
  - same-session reruns handle convergence
  - challenger pass starts only after the orchestrator validates the previous
    working agent's recorded outputs
  - challenger findings require `reopen-record.json` and promote the failed
    challenger session into the next working baseline
- Required run layout:
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/review-runs/<run-name>/working/attempt-1/spawn-decision.json`
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/review-runs/<run-name>/working/attempt-<n>/findings.json`
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/review-runs/<run-name>/working/attempt-<n>/verifier-evidence.json`
  - later `working/attempt-<n>/spawn-decision.json` is required only when an exception forces a fresh working session
  - `working/attempt-<n>/reopen-record.json` is required whenever challenger findings promote the loop into the next working baseline
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/review-runs/<run-name>/challenger/attempt-1/findings.json`
  - `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/review-runs/<run-name>/challenger/attempt-1/verifier-evidence.json`
  - every challenger attempt MUST also write `spawn-decision.json`
- Mechanical closure gate:
  - before claiming final closure, validate the review run with
    `python3 openspec/schemas/modules/review-loop/bin/review_loop_guard.py --run-dir <review-run-dir>`
- Continuation target on pass:
  explicit Phase A exit decision and either archive or follow-on Phase B proposal work
- Skill entry point: `openspec-verify-change`

### Optional Gemini Dual Review

Use only when repository or checkpoint policy explicitly enables dual review.

- Runner contract: `gemini-capture`
- Raw report path:
  `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/gemini-raw.json`
- Normalized report path:
  `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/gemini-report.json`
- Maximum attempts:
  1 per checkpoint unless repository policy says otherwise
- Recovery behavior:
  - `input_raw_path -> report_path`
- Resolved command goes in execution evidence, not workflow policy prose

## Repository Index Cache Plan (When Useful)

Document repository-index support explicitly when cache artifacts can help
review orientation.

Required fields:
- Index contract id: `repo-index-v1`
- Canonical repository-index root:
  `docs/repo-index`
- Shared cache-helper sequence: `index-sequence/default`
- Optional refresh scoping hints:
  `new/code/platform`, `new/code/runtime`, `new/code/legacy`, `new/docs/race-finish-series.zh-CN`, and this change's `verification/`
- Fallback policy (`refresh|bypass`):
  `bypass`
- Verifier invocation template: `verify-reviewer-inline-v2`
- Index-maintainer agent path: `.codex/agents/index-maintainer.toml`
- Index skill entry points:
  - `openspec-index-preflight`
  - `openspec-index-maintain`
- Cache-helper evidence path convention:
  `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/index-*.json`
- Findings path convention:
  `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/*-findings.json`
- Verifier execution evidence path convention:
  `openspec/changes/close-phase-a-hardware-loop-and-runtime-unveto/verification/*-evidence.json`
- Cache handoff fields:
  - `index_context.contract`
  - `index_context.manifest_path`
  - `index_context.manifest_present`
  - `index_context.preflight_report_path`
  - `index_context.cache_mode`
  - `index_context.fallback_policy`

Shared field groups from `review-loop-core-v1.json` and
`review-loop-openspec-adapter-v1.json`:
- `bundle_required`
- `index_context_optional`
- `output_paths_required`
- `output_paths_optional`
- `verifier_evidence_required`
- `implementation_evidence_required`
- `challenger_entry.source_review_required`

Review completion contract:
- execution evidence MUST record:
  - `review_goal`
  - `review_pass_type`
  - `cache_mode`
  - `closure_authority`
  - `verifier_output_path`
  - `reviewed_paths`
  - `skipped_paths`
  - `reviewed_axes`
  - `unreviewed_axes`
  - `coverage_status`
  - `saturation_status`
  - optional `early_stop_reason`
  - optional `skip_reasons`
- implementation review MUST additionally record inline:
  - `review_scope`
  - `review_coverage`

## Migration Plan

1. Freeze the accepted Phase A starting point.
   Capture a short change-local baseline note that references the already accepted `2026-04-15` direct-match evidence bundle and states what is already proven versus still open.

2. Close sensor trust before control conclusions.
   Implementation work begins with IMU and encoder evidence/repairs because `control unlock` is not a trustworthy target until `imu.valid` and `encoder.valid` are both well explained.

3. Close actuator trust before Phase A exit.
   After sensors are trustworthy, verify motor output and emergency-stop semantics under safe bench conditions so later control-unveto runs do not rely on inferred actuator behavior.

4. Use longer runtime evidence to decide whether `control.veto` is a transient guard or a stable blocker.
   The change-local evidence bundle must separate “entered `control.start`” from “sustained non-veto interval exists”.

5. Finalize exposure policy as a closure decision, or promote it earlier if diagnostics prove it is the active blocker.
   Once control diagnostics show whether perception/exposure is upstream of the remaining unlock gap, either keep exposure as the final closure decision or resolve it immediately and then resume the final control-unlock judgment.

6. Update Phase A docs and stage-exit judgment.
   The final step is not another architectural rewrite; it is a concise synchronization of roadmap/progress/task docs with the change-local evidence and the Phase A pass/fail conclusion.

Rollback posture:

- If a checkpoint fails, retain prior accepted baseline and record the failure as the active Phase A blocker.
- Do not revert to degraded startup or wrong-baseline assumptions to manufacture a pass.
- If one sub-area requires deeper redesign than Phase A should absorb, stop at that checkpoint and spin a follow-on change rather than widening this one indefinitely.

## Open Questions

- IMU closure是否只需要 bridge/adapter 层的轴向/偏置修正，还是会暴露出更深的 vendor-side scaling 差异？
- 编码器量级结论能否仅通过 Phase A 基础标定得到，还是需要引入更正式的速度换算 contract？
- 电机真实出力验证是否能完全依赖架空轮/工装证据，还是还需要最小实车滑行补证？
- direct-match 相机路径是否存在可用的 vendor public exposure control，还是必须明确接受固定曝光策略？
- 如果 `control.veto` 在长窗口内仍频繁在多个原因间切换，是否需要额外的 runtime-owned aggregation evidence，还是现有 marker 已经足够？

## Risks / Trade-offs

- 一个主 change 承载整个 Phase A 收口，优点是阶段结论统一；代价是任务跨度较大，需要严格的 checkpoint discipline，避免 scope creep。
- 证据优先会让部分代码修复看起来“推进变慢”，但它能防止在不可信传感器或执行器前提上错误推进到 Phase B。
- 保持边界纯净意味着某些“临时好用”的跨层 hack 会被拒绝；短期看实现更克制，长期看后续阶段成本更低。
- 曝光策略被放在最后一个 closure checkpoint，优点是不会喧宾夺主；代价是如果 perception veto 主因最终就是曝光，前面的 control-unveto 证据需要一次回看。
- 如果多个 checkpoint 同时暴露真实硬件问题，本 change 可能在 archive 前保留较长时间；但这仍然比拆成多个相互依赖的小 change 更可读、也更利于阶段判断。
