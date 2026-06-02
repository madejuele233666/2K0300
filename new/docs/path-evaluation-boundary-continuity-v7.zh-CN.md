# Path Evaluation V7 边界连续性裁剪 / Boundary Trace Continuity Clip

状态：讨论记录。本文档记录 2026-05-29 路径评估相关讨论结论，用于指导后续实现。V7 不修改控制链，不调整既有参数，不在路径结果上做平滑补救；V7 聚焦路径生成前的边界点可信度，并新增一个显式边界连续性距离参数。

本文档继承当前 BEV reference pipeline 的“互不知晓”和“大道至简”原则：

```text
row scan 只产出原始边界事实；
boundary continuity helper 只裁剪边界点；
路径候选生成只消费裁剪后的边界事实；
单边补线 / 双边中点 / 控制链不知道裁剪细节。
```

## 1. 本次问题结论

现场信息流显示，车当前位置附近存在前后帧路径点大幅跳变。已有证据链如下：

```text
path_candidates.count 始终为 1；
跳变点已经存在于 steering_snapshot.visual_reference.path_candidates[0].samples；
不是前端绘制问题。
```

典型相邻发布帧：

```text
frame 40467 -> 40468，dt_capture_ms=16
index 8: -0.370960861444 -> -2.37206792831
index 9: -0.430960863829 -> -2.584638834

frame 40468 -> 40469，dt_capture_ms=16
index 8: -2.37206792831 -> -0.370960861444
index 9: -2.584638834 -> -0.480886131525
```

同时配置快照显示：

```text
SEARCH_LATERAL_LIMIT_M = 1.60000002384
NOMINAL_ROAD_HALF_WIDTH_M = 0.209999993443
REFERENCE_LATERAL_JUMP_GATE_M = 1000
```

超过 `SEARCH_LATERAL_LIMIT_M` 的路径点不能由双边中点直接产生。它们来自单边补线输出，但根因不是“单边补线天然跳变”。真正问题是：

```text
边界点本身发生跳变；
相邻 row 的边界点被错误关联；
BuildSingleBoundaryOffsetReference() 的 normal offset 公式把错误边界斜率放大为大 lateral 输出。
```

因此下一步修复位置应在边界判定 / 边界关联层，而不是：

```text
修改控制参数；
在控制链平滑路径；
限制参考线必须留在屏幕内；
让 single-boundary helper 知道图像边缘或候选语义。
```

## 2. 路径候选的基本语义

当前普通路径候选生成有两类基础来源：

```text
1. 单边补线：
   一侧边界可信，按名义半赛道宽度生成中心参考线。

2. 双边中点：
   左右边界均可信，取双边中点生成中心参考线。
```

这两种方式本身都不必然产生前后帧跳变。跳变来自输入边界事实不稳定：

```text
边界点 A 与边界点 B 属于不同物理边界；
候选生成仍把它们当成同一条边界 trace；
后续单边 offset 对这条错误 trace 做几何外推。
```

所以 V7 的修复目标不是改写“单边补线”和“双边中点”的语义，而是在进入这些语义之前裁剪掉不连续的原始边界点。

## 3. Helper 真实语义

新增一个独立 helper，职责只限于裁剪一侧边界点序列：

```cpp
struct BoundaryTracePoint {
  int row_index;
  float forward_m;
  float lateral_m;
};

struct BoundaryTraceClipOptions {
  float max_adjacent_distance_m;
};

std::vector<BoundaryTracePoint> ClipBoundaryTraceOutliers(
    absl::Span<const BoundaryTracePoint> raw_points,
    const BoundaryTraceClipOptions& options);
```

helper 的输入是原始边界点，输出是裁剪后的边界点。它不返回单边、双边、退化、候选类型、屏幕边缘、hold、circle、cross、控制状态。

第一版规则：

```text
points 按近端到远端排序；
保留第一个点；
每个新点只和 last_kept_point 比较；
允许距离 = max_adjacent_distance_m * row_gap；
若 BEV 平面距离 <= 允许距离，保留该点并更新 last_kept_point；
若 BEV 平面距离 > 允许距离，删除该点，last_kept_point 不变；
继续评估后续点。
```

`row_gap` 表示当前候选点与 `last_kept_point` 跨过的 sparse row 数量。它不是固定 1。

示例：

```text
从下至上有 A、B、C 三个点。

A 保留。
B 与 A 的距离 > 1 * max_adjacent_distance_m，删除 B。
C 与 A 的距离按 2 * max_adjacent_distance_m 判断。
若 C 满足该距离约束，C 保留。
```

这不是“从异常点开始截断前缀”。单个跳点被删除后，后续点继续独立接受评估。

## 4. 退化语义不属于 Helper

helper 不需要知道单边或双边退化。它只裁剪左右两侧各自的边界点序列。

后续候选生成基于裁剪后的边界事实自然得到语义：

```text
左边界该 row 保留，右边界该 row 删除：
  该 row 自然进入左侧单边补线语义。

右边界该 row 保留，左边界该 row 删除：
  该 row 自然进入右侧单边补线语义。

左右边界该 row 都保留：
  该 row 自然进入双边中点语义。

左右边界该 row 都删除：
  该 row 无路径候选。
```

这符合现有程序已经具备的结构：

```text
IntervalSupportsMidpointCandidate()
IsSingleEdgeInterval()
AddSingleEdgeCandidates()
BuildOrdinaryCenterCandidates()
```

V7 不应让 helper 输出“退化为单边”这种高层结论。

## 5. 应用位置

裁剪应发生在路径候选生成之前，尤其是单边补线调用 `BuildSingleBoundaryOffsetReference()` 之前。

推荐数据流：

```text
row scan 原始 intervals
-> 提取左右侧 raw boundary trace
-> ClipBoundaryTraceOutliers(left)
-> ClipBoundaryTraceOutliers(right)
-> 用裁剪后的边界事实重建该 row 的候选可见性
-> 双边中点 / 单边补线自然生成候选
-> connectivity gate
-> selector
```

最小实现也可以先只覆盖当前错误放大入口：

```text
AddSingleEdgeCandidates()
-> 构造当前 row 与邻近 row 的 raw boundary points
-> ClipBoundaryTraceOutliers()
-> 若该 row 对应边界点被裁掉，直接删去该 row 候选
-> 若裁剪后不足两个边界点，不调用 BuildSingleBoundaryOffsetReference()
```

完整实现更干净：先在普通路径候选生成层把左右边界点序列裁剪成新事实，再让双边中点和单边补线共同消费这份事实。

## 6. 距离参数

`max_adjacent_distance_m` 是 helper 的入参，来源必须是显式参数，不在调用点现场构造。

建议参数名：

```text
BEV_GEOMETRY.BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M
```

对应运行时字段：

```cpp
float boundary_trace_max_adjacent_distance_m = 0.45F;
```

该参数需要进入：

```text
port::BEVGeometryParameters；
default_params.json；
default_params.md；
参数加载与默认值测试。
```

第一版实现约束：

```text
不修改 REFERENCE_LATERAL_JUMP_GATE_M；
不修改 NOMINAL_ROAD_HALF_WIDTH_M；
不修改 LATERAL_STEP_M；
不从 NOMINAL_ROAD_HALF_WIDTH_M 推导距离；
不从 LATERAL_STEP_M 推导距离；
不加入量化容差；
调用点只读取 BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M 并传入 BoundaryTraceClipOptions。
```

距离计算使用 BEV 平面欧氏距离。比较规则必须保持直接：

```text
distance = hypot(delta_forward_m, delta_lateral_m)
allowed_distance = max_adjacent_distance_m * row_gap

distance <= allowed_distance:
  保留该点。

distance > allowed_distance:
  删除该点。
```

这里检查的是“同一侧边界 trace 在 sparse rows 上是否连续”，不是路径点 lateral jump gate，也不是白区连通性 gate。它们的职责分别是：

```text
boundary continuity clip:
  输入边界点，删除边界跳点。

path connectivity gate:
  输入路径点，检查路径段是否跨黑区。

reference/control readiness:
  输入已选 reference，决定控制链是否可用。
```

## 7. 不做的事

V7 不做以下改动：

```text
不要求参考线必须在屏幕内；
不禁止单边补线生成到屏幕外；
不改变 unknown 屏幕边缘的既有语义；
不在 helper 内判断屏幕边缘；
不让 helper 读取图像灰度；
不让 helper 知道 CircleV2、cross、ordinary 场景；
不在控制链做路径平滑；
不修改 REFERENCE_LATERAL_JUMP_GATE_M；
不修改 NOMINAL_ROAD_HALF_WIDTH_M；
不修改 row scan 阈值。
```

路径生成到屏幕外在单边补线语义中是合理的。V7 要删除的是不连续边界点，不是屏幕外路径本身。

## 8. 测试要求

至少覆盖以下场景：

```text
1. 连续边界：
   A、B、C 均在距离约束内，全部保留。

2. 单个跳点：
   A 保留，B 超限删除，C 按 A->C 的 row_gap 扩大阈值后保留。

3. 连续跳点：
   A 保留，B/C 均超限删除，D 若仍无法满足 A->D 的扩大阈值则删除。

4. 双边退化：
   左侧该 row 被裁掉，右侧保留，后续候选生成自然进入单边语义。

5. 双边中点保留：
   左右两侧该 row 均通过裁剪，后续仍生成双边中点候选。

6. 单边补线入口保护：
   裁剪后不足两个边界点时，不调用 BuildSingleBoundaryOffsetReference()。

7. 参数来源：
   BOUNDARY_TRACE_MAX_ADJACENT_DISTANCE_M 能从默认参数和 JSON 配置加载；
   ordinary candidate 生成只读取该参数，不从其它几何量现场构造距离。
```

回归验证应包含：

```text
run_bev_simple_perception_test.sh
run_visual_reference_orchestration_test.sh
```

若改动触及 CircleV2 共享路径生成事实，再补充：

```text
run_steering_circle_v2_scene_test.sh
run_visual_element_evidence_test.sh
```

## 9. 验收标准

实现完成后，路径评估层应满足：

```text
边界跳点在进入单边 offset 之前被删除；
单点异常不会截断整条后续边界；
双边一侧不连续时自然退化为单边；
双边两侧均连续时仍走中点；
helper 不知道候选语义；
控制链、selector、connectivity gate 不承担边界跳点修复职责。
```

在新的现场信息流里，应重点检查：

```text
前后帧同一 index 的 lateral_m 是否仍出现米级跳变；
异常 frame 中单边补线是否仍输出超过搜索范围的大 lateral 点；
裁剪前后的边界点数量和被删除点行号；
被删除点是否集中在视觉上被屏幕截断或边界误判的 row。
```
