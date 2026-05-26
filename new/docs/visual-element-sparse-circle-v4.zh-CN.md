# Visual Element Sparse Circle V4 基础寻线丢线修复 / Ordinary Reference Lost-Boundary Fix

状态：设计记录。V4 不定义新的环岛 FSM，不修改 circle / cross 元素识别，不改变 visual reference arbitration。V4 记录本轮调试得到的真正基础问题和修复契约：普通寻线在一侧边线消失时，不能继续把白色区间的几何中点当作道路中心。

本文档沿用 V2/V3 的“互不知晓”和“大道至简”原则，但修复目标不属于 CircleV2Scene。它属于基础 ordinary reference 生成链路。

## 1. 问题结论

当前基础寻线的核心错误是：

```text
row white interval
-> center = (interval.left + interval.right) / 2
-> ordinary reference sample
```

这只有在 `interval.left` 和 `interval.right` 都是真实可见道路边界时才成立。

一侧边线消失时，白色区间的一端可能只是：

```text
采样范围边界
视野截断边界
开口/缺线造成的白区延伸端
```

它不再等价于真实道路边界。继续取区间中点，会把不存在的边界当作真实边界参与居中，导致普通寻线中心被开放侧拉偏。

因此，本轮真正要修的是：

```text
ordinary reference builder 如何从一行白色区间解释出道路中心。
```

不是：

```text
CircleV2 FSM
CircleV2 detector
cross detector
visual element pipeline
reference arbitration
```

## 2. 修复边界

V4 只改变普通寻线内部对 row interval 的解释。

允许改动：

```text
BEV row intervals
-> ordinary interval interpretation
-> ordinary current visual reference
-> existing reference continuity / hold
```

不允许改动：

```text
CircleV2 phase transition
CircleV2 event observer
CircleV2 geometry observer
cross evidence
cross takeover
visual element candidate selection
visual reference arbitration priority
```

CircleV2 和 cross 不应该知道 ordinary reference 如何处理丢线。它们只能消费修复后的普通道路事实或原始 row facts。

## 3. 数据流

V4 后的职责分层为：

```text
Camera frame
-> BEV sparse row scan
   -> raw white intervals
   -> sampleable lateral range
-> Ordinary reference builder
   -> interpret interval boundary visibility
   -> estimate visible boundary direction
   -> estimate current-frame ordinary center samples
   -> stop at first unavailable leading sample
-> Reference continuity
   -> if current visual reference is insufficient, use existing hold-last candidate
-> VisualReferenceCandidate
-> SelectVisualReference
```

这里的关键是：

```text
双侧丢线时，ordinary reference builder 不凭空造当前帧中心；
它只是不输出当前视觉点，让已有 reference continuity 层决定是否进入 hold。
```

hold 属于 reference continuity，不属于 row scan，也不属于 FSM。

## 4. 行区间解释

对每一行选中的白色区间，ordinary reference builder 先判断区间两端是否可以当作真实道路边界。

坐标和命名不变量：

```text
x = lateral_m
y = forward_m
lateral_m > 0 表示车辆左侧
lateral_m < 0 表示车辆右侧
```

本文中 `low_edge` / `high_edge` 表示 BEV 横向坐标序，而不是屏幕左右，也不是物理左/右边线语义：

```text
low_edge  = 当前 interval 中 lateral_m 较小的端点
high_edge = 当前 interval 中 lateral_m 较大的端点
```

当前代码字段 `interval.left_m` / `interval.right_m` 在 row scan 中按横向采样序保存，落地时应把它们理解为：

```text
interval.left_m  -> low_edge.x
interval.right_m -> high_edge.x
```

不要仅凭字段名把 `interval.left_m` 理解成物理左边界。物理边线归属应由调用方的边线 trace 选择负责。

定义：

```text
low_edge_visible  = low_edge  没有贴住 row.sampleable_left
high_edge_visible = high_edge 没有贴住 row.sampleable_right
```

“贴住”的容差由横向采样分辨率派生，例如使用 `lateral_step_m` 的一到两格量级。该容差是采样几何误差，不是新的业务调参策略。

解释规则：

```text
low_edge_visible && high_edge_visible:
  center = 0.5 * (low_edge.x + high_edge.x)
  basis = both_edges

low_edge_visible && !high_edge_visible:
  low_edge is the visible boundary
  center is produced by signed normal offset
  signed_normal_offset_m = +nominal_road_half_width_m
  basis = low_edge_normal_offset

!low_edge_visible && high_edge_visible:
  high_edge is the visible boundary
  center is produced by signed normal offset
  signed_normal_offset_m = -nominal_road_half_width_m
  basis = high_edge_normal_offset

!low_edge_visible && !high_edge_visible:
  no current visual center
  basis = unavailable
```

若该行没有可选白色 interval，也等价于 `no current visual center`。

`nominal_road_half_width_m` 使用已有 `BEV_GEOMETRY.nominal_road_half_width_m`。V4 不引入实时半路宽重算。

单侧可见时，不能把上述规则实现成简单的固定横向平移。弯道中边线有方向，补线必须适应该方向；具体几何公式只在附属 helper 文档中维护。

估计出的 `center` 必须仍落在当前白色区间内部。若方向补线后的中心落到区间外，则该行不输出当前视觉中心。

## 5. 单侧丢线的方向补线

单侧丢线时，ordinary reference builder 不在主文档内重新定义补线公式。完整几何契约由 [V4 单边界法向补线 Helper](visual-element-sparse-circle-v4-single-boundary-helper.zh-CN.md) 统一定义。

V4 主文档只记录 ordinary 调用方语义：

```text
low_edge 可见:
  boundary_trace = visible low_edge trace
  signed_normal_offset_m = +nominal_road_half_width_m

high_edge 可见:
  boundary_trace = visible high_edge trace
  signed_normal_offset_m = -nominal_road_half_width_m
```

ordinary reference builder 负责把当前帧 row intervals 解释为上述 `boundary_trace + signed_normal_offset_m`，然后调用单边界法向补线 helper。

主文档不重复描述：

```text
局部边线方向估计；
signed normal offset 公式；
x(y) 单值 trace 输入不变量；
方向不可用时的停止条件；
helper 单测。
```

这些内容只能维护在附属文档中，避免主文档和 helper 契约漂移。

双边可见时仍优先使用两侧真实观测的中点。V4 首版不把双边可见且宽度异常的问题混入单侧丢线修复。

## 6. 连续性选择

同一行存在多个白色区间时，不能再用原始 `interval.center` 做选择。同时，单侧补线的中心点依赖 boundary trace 的方向，因此实现顺序不能写成“孤立查看每个 interval 后立刻生成 center”。

正确顺序是：

```text
1. 对每个 interval 先做 boundary visibility interpretation。
2. 丢弃 unavailable interval。
3. 对 both_edges interval，直接生成 midpoint center candidate，不调用单边界 helper。
4. 对 single-edge interval，按 low_edge / high_edge 端点分别形成 candidate boundary traces。
5. 对 single-edge candidate trace 调用单边界法向补线 helper 生成 candidate center path。
6. 在所有 center candidates 上，用从近到远的 leading 连续性和本帧内相邻采样行的几何连续性选择 reference trace。
```

也就是说：

```text
先解释边界端点，再形成 center candidates，再做连续性选择。
```

其中 `both_edges` 的 midpoint path 是普通当前帧视觉中心候选，不属于单边界补线；单边界 helper 只服务 `low_edge_normal_offset` / `high_edge_normal_offset` 这类 single-edge candidate。

不能：

```text
先用错误的 raw center 选区间，再事后修正 center。
```

否则一侧丢线时，区间选择本身已经被错误中心污染。

## 7. 严格前导段保持不变

V4 不改变基础寻线的严格前导段原则：

```text
reference 必须从最近端 BEV 行开始连续成立；
遇到第一个无法解释出当前视觉中心的行就停止；
不跳过缺失行；
不从远端重新开始；
不补洞。
```

区别只在于：

```text
一侧丢线不再必然导致当前行失败；
只要另一侧真实边界 trace 可用，就可以按边线方向做法向半路宽补线。
```

双侧丢线仍然表示当前帧该行缺少足够几何事实，ordinary builder 不继续造点。

## 8. 与 hold 的关系

双侧丢线可以进入 hold，但触发方式必须解耦：

```text
ordinary builder:
  当前帧没有足够 leading visual reference。

reference continuity:
  判断是否使用上一帧 reference 的 hold-last candidate。
```

这避免 ordinary builder 保存时间记忆，也避免 FSM 接管基础寻线的丢线恢复。

不应在 ordinary builder 内部做：

```text
上一帧曲率预测
上一帧斜率外推
上一帧中心线直接续写
固定中心线兜底
```

这些都属于时间连续性或预测，不属于当前帧行区间解释。

## 9. 与 CircleV2 / V3 的关系

V4 不是 V2/V3 的新阶段，也不是新的环岛路径生成方式。

V2/V3 中出现的部分异常现象会被 V4 修复影响，是因为它们依赖普通道路模型或普通寻线退出后的行为。但这是下游受益，不是 V4 的职责扩张。

CircleV2 不应新增类似：

```text
if ordinary lost then special circle patch
```

也不应把 circle 内部事实反向塞回 ordinary builder。

正确关系是：

```text
ordinary reference builder 先正确处理基础丢线；
CircleV2 / cross / arbitration 继续按原边界消费结果。
```

## 10. 输出编码和调试事实

V4 改变的是 ordinary reference builder 如何估计当前帧中心线，不改变 visual reference arbitration 的语义。

第一版落地可以保持：

```text
ReferenceMode::kIntervalCenter
reference_source = "simple_interval_center"
```

这里的 `IntervalCenter` 应理解为“当前帧 ordinary visual reference”，不再要求每个点都由 raw interval midpoint 生成。这样可以避免为了命名精确而误改 `SelectVisualReference()` 或候选优先级。

`basis` 只属于 ordinary builder 的内部/调试事实：

```text
both_edges
low_edge_normal_offset
high_edge_normal_offset
unavailable
```

它不进入 arbitration priority，不改变 candidate kind，不让 FSM 或元素识别读取。

若后续新增类似：

```text
BEVPathPointSource::kBoundaryOffset
```

它也只能作为调试来源标记，不能改变视觉参考选择规则。

## 11. 参数原则

V4 不新增业务参数。

使用已有参数：

```text
BEV_GEOMETRY.nominal_road_half_width_m
BEV_GEOMETRY.lateral_step_m
BEV_CLASSIFICATION.hold_last_max_cycles
```

其中：

```text
nominal_road_half_width_m:
  单侧边界可见时，作为边线到中心线的法向距离。

lateral_step_m:
  用于派生“是否贴住采样边界”的几何容差。

hold_last_max_cycles:
  继续由 reference continuity 控制 hold 的最长周期。
```

不引入：

```text
single_side_lost_enabled
low_edge_lost_threshold
high_edge_lost_threshold
circle_innertrace_lost_patch
runtime road width re-estimation
```

## 12. 测试契约

最小测试应覆盖：

```text
both_edges:
  interval 两端都不贴采样边界，输出 midpoint，且不调用单边界 helper。

low_edge_normal_offset:
  high_edge 贴住 sampleable_right，ordinary builder 以 low_edge trace 和 +nominal_road_half_width_m 调用单边界 helper。

high_edge_normal_offset:
  low_edge 贴住 sampleable_left，ordinary builder 以 high_edge trace 和 -nominal_road_half_width_m 调用单边界 helper。

direction_unavailable:
  单边界 helper 输出不足时，ordinary builder 不补洞，不自行 fallback，当前视觉 reference 不成立。

unavailable:
  low_edge 和 high_edge 都贴住采样边界，或该行没有可选白色 interval，不输出当前视觉点。

continuity selection:
  多 interval 时，同时保留 both_edges midpoint candidates 和 single-edge helper candidates，再在 center candidates 上做连续性选择。

strict leading:
  遇到 unavailable 行后停止，不补洞。

hold bridge:
  当前 visual reference 不足时，由已有 hold-last 机制接管。

single-boundary helper:
  单边界补线的公式、输入不变量和纯几何单测由附属文档定义，主文档不重复维护。
```

回放验证应至少包含：

```text
普通弯道中一侧边线消失的帧；
环岛内部一侧边线消失的帧；
稳定误判右环岛位置的帧；
正常直道双边可见帧。
```

验证目标不是让 CircleV2 进环，而是确认：

```text
ordinary center 不再被缺失侧边界拉偏；
当前视觉 reference 不足时进入已有 hold；
CircleV2 / cross / arbitration 没有因为本修复新增耦合。
```

## 13. 架构不变量

V4 落地后应保持以下不变量：

```text
Row scan 只输出原始行事实，不输出道路中心语义。

Ordinary reference builder 是“行区间 -> 当前视觉中心”的唯一所有者。

Reference continuity 是 hold-last 的唯一所有者。

CircleV2 不知道 ordinary builder 如何处理单侧丢线。

Cross 不知道 ordinary builder 如何处理单侧丢线。

Visual reference arbitration 只选择候选，不重新解释边线。

ReferenceMode / candidate kind 不承载 both_edges 或 single-edge 细节；这些 basis 只能是 ordinary builder 的内部/调试事实。

双侧丢线不在当前帧凭空造 reference。

单侧丢线只用当前帧可见真实边界 trace、边线方向和名义半路宽生成 center。

单侧补线是沿边线法向的等距中心线重采样，不是固定 lateral 平移。
```

这就是 V4 的最小修复：把基础寻线中“白色区间中点”等价于“道路中心”的隐含假设，替换为明确的边界可见性解释。
