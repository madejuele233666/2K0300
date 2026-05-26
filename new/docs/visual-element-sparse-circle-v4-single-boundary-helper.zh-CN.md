# Visual Element Sparse Circle V4 附属：单边界法向补线 Helper / Single-Boundary Normal Offset Helper

状态：V4 附属设计记录。本文档只定义可复用的单边界法向补线 helper。V4 主文档仍只记录基础 ordinary reference 的丢线修复，不扩展到元素识别、FSM 或 arbitration。

本文档的目标是把“只看到一侧边线时，如何生成与该边线方向一致的参考线”抽象成一个纯几何 helper，供 ordinary 丢线修复、环岛内边线跟随、出环边线跟随以及未来所有单边界补线场景复用。

## 1. 核心原则

单边补线不是某个场景的私有技巧，而是一个通用 BEV 几何操作：

```text
visible boundary trace
-> estimate local boundary direction
-> offset by signed normal distance
-> resample on target forward rows
-> leading reference samples
```

helper 不知道：

```text
circle
cross
FSM phase
element evidence
candidate arbitration
hold-last continuity
camera image classification
```

helper 只知道：

```text
一条当前帧 BEV 边线 trace；
一组目标 forward_m 采样行；
一个有符号法向偏移距离；
如何输出连续的 BEV path samples。
```

这保证所有场景都复用同一套“沿边线方向补线”的几何语义，而不是在 ordinary、circle、exit trace 等位置分别复制不同版本。

## 2. Helper 语义

建议 helper 名称采用中性语义，例如：

```text
SingleBoundaryOffsetHelper
BuildSingleBoundaryOffsetReference
ComposeOffsetPathFromBoundaryTrace
```

它不是：

```text
CircleInnerPathBuilder
LostLineRecovery
ElementReferenceComposer
```

因为这些名字会把 helper 锁死到某个调用方。

输入语义：

```text
boundary_trace:
  当前帧中一侧可见边线的 BEV 点序列。
  点包含 forward_m 和 lateral_m。
  调用方负责保证这些点来自同一条边线。

target_forward_samples:
  需要输出 reference samples 的 forward_m 序列。

signed_normal_offset_m:
  在同一 forward_m 重采样语义下的有符号法向偏移距离。
  正值表示输出线位于 x 正方向侧；
  负值表示输出线位于 x 负方向侧；
  0 表示贴着原边线输出。
```

输出语义：

```text
present leading path samples;
每个 sample 的 forward_m 来自 target_forward_samples；
每个 sample 的 lateral_m 来自边线局部方向补线；
遇到无法插值、无法估计方向或非有限结果时停止；
不跳过缺失行；
不补洞。
```

## 3. 坐标和公式

BEV 坐标约定：

```text
x = lateral_m
y = forward_m
lateral_m > 0 表示车辆左侧
lateral_m < 0 表示车辆右侧
```

helper 的 `left/right` 不参与几何计算。若调用方使用“左边界/右边界”这样的业务词，必须先在调用方内部映射为：

```text
boundary_trace
signed_normal_offset_m
```

对目标采样行 `y`，helper 先从 `boundary_trace` 得到：

```text
edge_x(y)
s(y) = dx / dy
```

其中 `s(y)` 是该处边线局部方向。第一版实现可以用相邻 trace 点、局部小窗口拟合或其他等价方式估计。该估计是 helper 内部几何细节，不应成为元素识别或 FSM 事实。

输入不变量：

```text
boundary_trace 必须来自同一帧、同一条边线；
boundary_trace 在目标 forward range 内必须能表示为单值 x(y)；
boundary_trace 点应按 forward_m 单调排列或可由 helper 稳定排序；
boundary_trace 至少需要两个有限点，且这些点的 forward_m 不完全相同；
若原始边线存在回折、多分支或不可重采样段，调用方必须先裁剪/选择一个单值分支；
helper 不处理多分支边线拓扑。
```

方向估计失败的条件也只使用无参数几何事实：

```text
点数不足；
forward_m 无差异；
x(y) 不可单值表示；
估计出的 s 或 s(y) 非有限。
```

这些情况下 helper 停止输出 leading samples，不引入 hidden confidence，不使用 fallback slope = 0。

在当前 sparse BEV reference 仍按固定 `forward_m` 行采样的前提下，输出点为：

```text
target_x(y) = edge_x(y) + signed_normal_offset_m * sqrt(1 + s(y)^2)
```

该公式表达：

```text
输出线与可见边线平行；
输出线到可见边线的法向距离为 abs(signed_normal_offset_m)；
输出点在同一个 target forward_m 行上重采样。
```

当 `signed_normal_offset_m = 0` 时：

```text
target_x(y) = edge_x(y)
```

也就是贴着边线生成路径。

这比简单写：

```text
target_x = edge_x + signed_normal_offset_m
```

更一般。后者只在 `s(y) = 0` 时成立。

## 4. 调用方映射

helper 只接受几何输入。各调用方自行把自己的语义映射成 `boundary_trace + signed_normal_offset_m`。

### 4.1 Ordinary 单侧丢线

ordinary reference builder 在一侧边界丢失、另一侧真实边界可见时调用 helper。

low_edge 可见、high_edge 丢失：

```text
boundary_trace = visible low_edge trace
signed_normal_offset_m = +nominal_road_half_width_m
```

high_edge 可见、low_edge 丢失：

```text
boundary_trace = visible high_edge trace
signed_normal_offset_m = -nominal_road_half_width_m
```

ordinary builder 仍负责：

```text
判断 low_edge / high_edge 是否真实可见；
选择同一条连续边线 trace；
验证输出 center 是否落在当前白色区间内；
当前视觉 reference 不足时交给 reference continuity。
```

helper 不负责 hold。

### 4.2 环岛 InnerTrace 边线跟随

当环岛 InnerTrace 需要沿锁存方向侧内圆边线生成路径时，也应复用 helper。

例如当前策略是“贴着内圆边线”：

```text
boundary_trace = observed inner boundary trace
signed_normal_offset_m = 0
```

如果后续参数要求离内圆边线保持一定距离，则只改变调用方传入的 `signed_normal_offset_m`，不复制一套 circle 私有补线算法。

CircleV2 geometry observer / composer 仍负责：

```text
找到锁存方向侧内圆边线；
决定该边线 trace 是否可用；
决定本帧是否输出 CircleV2 reference plan。
```

helper 不知道 InnerTrace、circle direction 或 FSM phase。

### 4.3 出环或其他单边界跟随

任何“从一侧可见边线生成路径”的场景都应映射为同一 helper 调用：

```text
boundary_trace = caller-owned visible boundary trace
signed_normal_offset_m = caller-owned signed normal offset
target_forward_samples = caller-owned sample rows
```

包括但不限于：

```text
ExitTrace 外侧边线跟随；
未来虚拟边线覆盖后的路径生成；
局部普通弯道中只剩一侧边线的路径生成。
```

这些调用方可以拥有自己的“是否输出 candidate / plan”的规则，但不应拥有自己的边线方向补线公式。

## 5. 放置边界

helper 应放在中性几何/参考构造层，而不是 circle detail 内部。

推荐落地边界：

```text
single_boundary_offset helper:
  consumes BEV trace points + target forward samples + signed normal offset;
  returns BEV reference/path samples.

ordinary reference builder:
  calls helper for one-side-lost current visual center.

circle geometry/composer:
  calls helper for inner/outer boundary trace path when needed.
```

不推荐：

```text
把 helper 放进 CircleV2Scene detail 后再让 ordinary 反向依赖 circle；
把 helper 放进 visual element pipeline；
让 helper 输出 VisualReferenceCandidate；
让 helper 读取 RuntimeParameters 整体；
让 helper 读取 FSM memory。
```

若未来代码需要命名文件，优先选择表达几何能力的文件名，例如：

```text
steering_single_boundary_offset.hpp
steering_single_boundary_offset.cpp
```

而不是：

```text
steering_circle_inner_path.cpp
steering_lost_line_recovery.cpp
```

## 6. 输入责任和输出责任

调用方责任：

```text
1. 从自己的事实源选择一条边线 trace。
2. 决定 signed normal offset。
3. 提供目标 forward samples。
4. 决定 helper 输出不足时如何处理。
5. 做调用方特有的白区、候选、plan 或 usability 约束。
```

helper 责任：

```text
1. 按当前帧边线 trace 插值 edge_x(y)。
2. 估计局部方向 s(y)。
3. 按 target_x = edge_x + signed_normal_offset * sqrt(1 + s^2) 输出同 y 重采样路径。
4. 保持 leading contiguous，不补洞。
```

helper 不承担：

```text
边线检测；
左右开口判定；
circle/cross 识别；
FSM 转移；
reference hold；
white-region 判断；
candidate arbitration；
控制安全门。
```

## 7. 大道至简约束

第一版 helper 不引入复杂策略。

不需要：

```text
独立置信度体系；
场景类型参数；
circle-specific 分支；
cross-specific 分支；
历史帧预测；
运行时宽度重估；
fallback slope = 0。
```

只需要：

```text
finite trace;
local direction;
signed normal offset;
leading contiguous output.
```

当输入不足时，helper 返回不足的 leading samples。调用方按自己的层级处理：ordinary 可进入已有 hold，circle 可不输出本帧 plan，arbitration 继续按已有候选规则工作。

## 8. 测试契约

helper 单测应与场景无关，直接构造 BEV trace：

```text
straight_zero_slope:
  s = 0, signed_normal_offset 后等价于 target_x = edge_x + signed_normal_offset。

nonzero_slope:
  s != 0, target_x = edge_x + signed_normal_offset * sqrt(1 + s^2)。

zero_offset:
  signed_normal_offset = 0，输出贴合 edge_x(y)。

positive_and_negative_offsets:
  正负 signed_normal_offset 分别输出到 x 正/负方向侧。

leading_stop:
  trace 不覆盖某个 target y 时，从该点停止，不补洞。

direction_unavailable:
  点数不足、forward_m 无差异、无法表示为单值 x(y) 或估计方向非有限时，停止输出，不默认 s = 0。

caller_equivalence:
  ordinary 单侧丢线、CircleV2 贴边路径、ExitTrace 单边路径都只改变输入 trace / signed_normal_offset，不改变 helper 行为。
```

这些测试不应构造 FSM 状态，也不应构造 element evidence。它们只验证单边界法向补线这个几何能力。

## 9. 与 V4 主文档的关系

V4 主文档定义：

```text
基础 ordinary reference 在单侧/双侧丢线时的语义。
```

本文定义：

```text
单侧可见边线如何被复用地转换为方向一致的 offset path。
```

因此主文档可以继续保持普通寻线修复边界；本文作为实现附属契约，约束所有需要“单边界生成路径”的调用方都复用同一个 helper。
