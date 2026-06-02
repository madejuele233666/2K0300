# Visual Element Sparse Circle V3 入环补线原始思路

状态：历史归档方案。V3 的 `P_est + fixed_slope` 入环补线已经从活跃 CircleV2 运行时移除，当前代码只在 `new/code/archive/circle_v2_v3_fixed_slope_entry_guide/` 保留这套实现作为参考。活跃 `InnerTrace` 已回退为直接观察锁存方向侧内圆边线，并输出 scene-owned `CircleV2ReferencePlan`。

本文档记录 V3 的原始几何思路：在环岛入口处，估计环岛方向侧的外圆入口角点 `P`，并用 `P` 和固定斜率构造一条虚拟对侧边线，覆盖原本直道边线，让普通路径生成自然导入环岛。

V3 继承 V2 的状态机和解耦原则。本文只记录入环路径生成的新思路，不重新定义 Phase1 环岛判定，不引入后黑判定，不把中间事实泄漏为公共 API。

## 1. 背景

V2 的最小状态机已经定义为：

```text
Idle -> Approach -> InnerTrace -> ExitTrace -> Idle
```

另有一个显式兜底转移：

```text
InnerTrace -> Idle
```

该转移只在 `InnerTrace` 停留超过 `CIRCLE_V2_INNER_TRACE_STALL_TIMEOUT_MS`，且方向归一化 yaw 积分仍小于 `CIRCLE_V2_INNER_TRACE_STALL_YAW_MIN_DEG` 时触发。默认值分别为 `4000 ms` 和 `16.5 deg`。

其中：

- `Approach` 对应原 A。
- `InnerTrace` 对应原 B。
- `ExitTrace` 对应原 C。

V2 的入环几何曾倾向于从当前可见内圆边线出发生成路径。但实测和图像复盘表明，入环早期当前画面中的内圆可用段可能不够长，直接依赖内圆补线会导致生成路径不足或过早缺失。

V3 关注的问题是：

```text
如何在入口处生成足够自然的入环参考边界，而不是等待当前内圆边线充分可见。
```

## 2. 核心原始思路

设环岛方向为 `X`：

```text
X = left  表示左环岛
X = right 表示右环岛
```

V3 的入环补线思路是：

```text
在环岛入口处，估计 X 侧外圆边线的入口角点 P；
用该角点和按方向预设的固定斜率构造虚拟对侧边线；
用这条虚拟边线覆盖原本直道上的 X 对侧边线；
之后继续使用普通边界到参考路径的生成逻辑。
```

这条虚拟边线不是内圆边线，也不是把外圆整体平移得到的边线。它是一条入口导向边界，用来替代原本会把车继续带向直道的对侧边线。

V3 第一版不要求观察 `X` 对侧直道近端锚点。只要入口角点估计值 `P` 和固定斜率确定，虚拟边线就确定。

稀疏 row scan 不要求正好采样到真实几何角点。`P` 是由当前稀疏观测估计出来的入口角点，不限定具体估计方式。

## 3. 左环岛示例

对左环岛：

```text
X = left
X 侧 = left
X 对侧 = right
```

图像中左侧外圆在画面左侧。入口更靠远端，存在一个明显角点：

```text
P = left outer circle entrance corner
```

构造虚拟右边线：

```text
virtual_right_edge = line_through(P, fixed_slope_left)
```

然后用 `virtual_right_edge` 覆盖原本直道上的右侧边线。这里的“覆盖”只表示替换边线事实，不表示 CircleV3 自己从这条边线半宽偏移生成最终参考路径。

```text
right boundary := virtual_right_edge
ordinary line/path builder -> reference path
```

这样普通寻线会基于被替换后的右边线自然生成入环参考路径，而不是继续沿原右侧直道边线前行。

该虚拟边线不需要贴合当前右侧直道边线的近端位置。它的职责是覆盖原右侧直道边线并给出入环导向，而不是拟合原直道边界。

## 4. 右环岛镜像

对右环岛：

```text
X = right
X 侧 = right
X 对侧 = left
```

入口角点为：

```text
P = right outer circle entrance corner
```

构造虚拟左边线：

```text
virtual_left_edge = line_through(P, fixed_slope_right)
```

然后用 `virtual_left_edge` 覆盖原本直道上的左侧边线。CircleV3 不直接从该边线向右半宽偏移生成最终路径：

```text
left boundary := virtual_left_edge
ordinary line/path builder -> reference path
```

## 5. 与错误思路的区别

V3 不是：

- 不是找离中线最近的内圆边线作为唯一入环依据。
- 不是把外圆边线整体向内或向外平移。
- 不是把直道中心线硬拽向环岛。
- 不是依赖 rear black / side-rear black frontier。
- 不是依赖 X 对侧直道近端锚点。
- 不是由 adapter 或 arbitration 修补短路径。
- 不是由 CircleV3 把虚拟边线直接偏移半路宽得到最终 reference path。
- 不是允许数学生成的路径穿过黑色背景。

V3 真正利用的是：

```text
入口外圆角点 P + 固定斜率 -> 虚拟对侧覆盖边线
```

关键点是 `P`。它位于入口远端，是外圆与入口区域发生几何转折的位置。运行时的 `P` 是对该几何角点的估计值。固定斜率是按方向配置的赛道几何参数，用来定义覆盖原直道对侧边线的入口导向方向。

## 6. 解耦落地边界

V3 保持 V2 的“互不知晓”原则。

状态机不需要知道：

```text
outer entrance corner
fixed slope
virtual edge
edge replacement
```

adapter 不需要知道：

```text
这条 reference path 的上游边线是否经过虚拟覆盖。
```

ordinary road builder 不需要知道：

```text
这是环岛、P 点、固定斜率或 InnerTrace。
```

因此该逻辑应拆成两个互不知晓的层：

```text
CircleV2Scene / CircleV3 boundary override detail
  -> estimate entrance corner P
  -> choose fixed slope by circle direction
  -> compose virtual opposite edge
  -> emit boundary override plan

ordinary line/path builder
  -> consume rows + boundary override facts
  -> compose ordinary reference path
  -> validate leading path is inside white region
```

CircleV3 对外不应输出最终中心路径，而应输出一条“边线替换计划”：

```text
BoundaryOverridePlan
```

## 7. 数据流

V3 的数据流保持简洁：

```text
Camera frame
-> BEV sparse rows
-> Circle scene
   -> Event observer
   -> Reducer
   -> Boundary override observer
      -> estimated entrance corner P
      -> fixed slope by circle direction
      -> virtual opposite boundary override
-> OrdinaryRoadModel / ordinary path builder
   -> apply generic boundary override
   -> generate reference path
   -> white-region leading validation
-> VisualReferenceCandidate
-> SelectVisualReference
```

被修正前的错误数据流是：

```text
Camera frame
-> BEV sparse rows
-> OrdinaryRoadModel
-> SceneFrameView
-> Circle scene
   -> Event observer
   -> Reducer
   -> Geometry observer
      -> estimated entrance corner P
      -> fixed slope by circle direction
      -> virtual opposite edge
   -> Reference composer
-> Scene reference plan
-> VisualReferenceAdapter
-> SelectVisualReference
```

该错误流把 CircleV3 变成了最终路径生成器，绕过了普通寻线的边界组合和白区可行驶约束。修正后的 V3 只改变入环阶段的边线事实，不直接改变最终路径。

V3 允许在 circle scene 内部复用同一个 `X` 侧外扩观察过程：

```text
X-side expansion observation
  -> Phase1 circle cue
  -> Approach entry gate
  -> entrance corner P_est
```

这只是内部复用，不改变公共数据流。外部仍只看到 `CircleV2Scene::Step()` 的 scene result。

## 8. 最小算法草案

给定：

```text
dir = locked circle direction X
rows = current BEV sparse rows
fixed_slope_left / fixed_slope_right
```

几何步骤：

```text
1. 在 X 侧估计远端外圆入口角点 P。
2. 根据 X 选择固定斜率 fixed_slope_X。
3. 构造 line_through(P, fixed_slope_X)，作为虚拟 X 对侧边线。
4. 输出一个只替换 X 对侧边线的 BoundaryOverridePlan。
5. 普通寻线在被替换后的边线事实上重新生成 reference path。
6. reference path 的 leading samples 必须落在白色区域内，否则本帧不输出 takeover path。
```

这里的“覆盖”不应修改全局 `BEVSimpleRowScan` 原始观测，也不应让普通寻线知道“这是环岛”。它应以通用边线覆盖输入的形式进入普通路径生成层。

本次 OpenSpec 变更固定第一版参数语义：

```text
fixed_slope_left_dx_dy  = -1.0
fixed_slope_right_dx_dy =  1.0
```

斜率坐标约定为：

```text
dx/dy = lateral_m / forward_m
```

合法范围：

```text
left  slope: finite, < 0, abs(value) <= 10.0
right slope: finite, > 0, abs(value) <= 10.0
```

虚拟边线采样规则固定为：

```text
1. 使用 ordinary_road.center_path.sampled_path 的 leading finite forward_m 作为 y 坐标。
2. 从 index 0 开始，遇到第一个 absent 或非有限 center sample 后停止。
3. 每个 virtual edge sample 使用公式：
   x = P.x + fixed_slope_dx_dy * (y - P.y)
4. 不使用停止点之后的 later sample 补洞。
5. leading sample 数量不足时，override unavailable，不输出 BoundaryOverridePlan。
```

注意：上述采样只能产生虚拟边线，不产生最终 reference path。最终 reference path 必须由普通寻线基于覆盖后的边线事实重新生成。

## 9. P 点估计原则

`P` 的语义固定为：

```text
X 侧外圆入口边界与入口区域发生几何转折的位置。
```

但 V3 不要求稀疏采样行正好扫到真实 `P`。运行时使用的是：

```text
P_est = sparse rows 对入口角点 P 的估计
```

允许的估计方式包括但不限于：

- 由相邻稀疏行的外扩状态变化估计。
- 由 X 侧边界脱离直道基准的位置估计。
- 由远端入口外圆边界形态和近端直道边界形态的交界估计。
- 由后续更稳定的局部几何拟合方式估计。

按侧估计时，某一侧的 reach / 外扩 / 直线基准只消费边界确实落在该侧的行：

```text
left-side observation:  使用 lateral_m < 0 的左边界行
right-side observation: 使用 lateral_m > 0 的右边界行
```

如果某一帧最宽白色区间跳到了另一侧，不应把它作为该侧 `reach = 0` 的有效观测；它只是该侧边界本帧没有可用观测。否则会在真实环岛图像中制造虚假的“先消失再外扩”，污染 Phase1 cue、entry gate 和 `P_est`。

本文不固定 `P_est` 的具体计算公式。只要求它保持 V3 几何语义：

```text
P_est 表示 X 侧外圆入口角点，
而不是内圆点、外圆最外点、普通直道边界点或后黑 frontier 点。
```

### 9.1 初步可能实现：分别估计 P.x 和 P.y

在 BEV 坐标中：

```text
P.x = lateral_m
P.y = forward_m
```

一个初步可能实现是：

```text
P.y 由 X 侧外扩区域的远端交界确定；
P.x 由 P.y 处的 X 侧直道基准边界确定。
```

也就是说，`P.y` 和 `P.x` 的来源不同：

```text
P.y:
  来自外扩搜索，表示外圆入口影响区开始的位置。

P.x:
  来自直道基准边界，表示入口角点落在 X 侧直道边界上。
```

左环岛：

```text
P.y = left expansion component 的远端交界 forward
P.x = base_left(P.y)
```

右环岛：

```text
P.y = right expansion component 的远端交界 forward
P.x = base_right(P.y)
```

这里的 `base_left(y)` / `base_right(y)` 是 `X` 侧直道基准边界，可以由同一个 `CircleSideExpansionObservation` 内部估计。第一版可以是若干基准行的常数均值；后续也可以替换为非外扩行拟合出的直线。

关键约束：

```text
不要用外扩后的 observed edge 直接作为 P.x。
```

原因是外扩后的 observed edge 已经进入外圆区域，可能偏向外侧。V3 需要的是入口角点：外圆入口与 X 侧直道基准边界发生连接/转折的位置。

## 10. 外扩观察三合一

V3 不应让 Phase1 cue、entry gate 和 `P_est` 各自实现一套外扩搜索。它们应共享同一个 circle scene 内部几何观察：

```text
CircleSideExpansionObservation
```

该观察只回答 sparse rows 中 `X` 侧边界的外扩几何，不直接承担状态切换职责，也不直接生成 reference。

内部职责拆分为：

```text
Expansion observer:
  从 rows 提取 X 侧边界序列、reach、外扩连续段、外扩交界。

Event observer:
  从 expansion observation 推导 Phase1 circle cue 和 Approach entry gate。

Boundary override observer:
  从 expansion observation 估计 P_est，并生成虚拟 X 对侧边线。

Reducer:
  只读取 event，不知道外扩搜索。

Ordinary path builder:
  只读取通用边线覆盖事实，不知道 P_est、固定斜率或 circle phase。
```

这实现三合一：

```text
同一套外扩搜索
  1. 支撑 Idle -> Approach 的 circle cue；
  2. 支撑 Approach -> InnerTrace 的 entry gate；
  3. 支撑 InnerTrace 入环补线的 P_est。
```

但它不是把旧 `DetectCircleElementEvidence()` 的最终结果反向喂给 geometry。最终 evidence 只表达 `left/right present`、置信度和 reason，已经丢失了 `P_est` 需要的外扩位置。V3 复用的是更底层的行观测和外扩搜索原语：

```text
widest white interval
row observation
side reach
sustained expansion
expanded component / transition
```

因此，推荐落地边界是：

```text
detail::ObserveCircleSideExpansion(...)
  -> detail::ObserveCircleV2Events(...)
  -> detail::ObserveCircleV2BoundaryOverride(...)
  -> ordinary path builder applies boundary override
  -> white-region reference validation
```

`ObserveCircleSideExpansion()` 属于 circle scene 内部 helper，不进入 `SceneFrameView`，不进入 `OrdinaryRoadModel`，不进入 adapter，也不成为跨模块 facts API。

## 11. 已修复的落地错误

已识别并修复的错误不是 Phase1 判定，也不是 P 点固定斜率本身，而是 **V3 落地时曾把“替换边线”错误实现成了“直接生成最终路径”**。

错误实现可以概括为：

```text
P_est + fixed_slope
-> virtual opposite edge
-> offset by road_half_width
-> CircleV2ReferencePlan.reference_path
-> adapter enters arbitration
```

该实现的问题是：

```text
CircleV3 直接半宽偏移，越过了普通寻线；
最终 reference path 不再由原始边线组合逻辑自然生成；
数学生成的路径没有证明自己落在白色可行驶区域；
只要 P、slope 和采样行存在，就可能在黑色背景中生成 circle_v2_inner。
```

当前实现的职责边界为：

```text
CircleV3:
  只负责生成 X 对侧的 virtual boundary override。

ordinary path builder:
  只负责在覆盖后的边线事实中生成 ordinary reference path。

white-region validator:
  只负责验证最终 leading reference samples 是否在白色区域内。

adapter / arbitration:
  只负责包装和选择已经有效的 reference candidate。
```

也就是说，修复后的数据流应为：

```text
P_est + fixed_slope
-> virtual opposite boundary
-> BoundaryOverridePlan
-> ordinary line/path builder
-> reference path
-> white-region leading validation
-> VisualReferenceCandidate
```

白区约束是硬约束：

```text
1. 最终替换后生成的 leading reference samples 必须落在当前 BEV row 的白色 interval 内。
2. 检查从 index 0 开始连续进行，遇到第一个 absent / 非有限 / 非白区 sample 后停止。
3. 连续白区 sample 数量不足时，本帧不输出 takeover path。
4. 不允许用后续 sample 补洞。
```

该约束不应进入 FSM。FSM 只决定 phase；几何层决定是否有 boundary override；普通路径层决定如何由边线生成路径；白区验证层决定路径是否可用于接管。

当前代码不应再存在 InnerTrace 的：

```text
virtual_edge + road_half_width -> final reference_path
```

若后续再次出现，应视为架构回退，而不是 V3 的最终形态。

当前代码中的 `reference_offset_m` 只服务 `ExitTrace` 的外侧边线跟随；`InnerTrace` 不使用半路宽偏移生成最终 reference path。

## 12. 已固定与暂不定义的内容

本次 OpenSpec 变更已固定：

- 固定斜率参数名、默认值和 `dx/dy` 坐标约定。
- 虚拟边线由 `P_est + fixed_slope` 定义。
- CircleV3 只负责替换 X 对侧边线，不负责直接生成最终 reference path。
- 最终替换路径必须经过白区 leading validation。

仍暂不固定：

- 入口角点 `P` 的最终估计公式细节。
- `CircleSideExpansionObservation` 的最终字段名和存储形态。
- `BoundaryOverridePlan` 的最终字段名和存储形态。
- 普通 path builder 如何接收和应用 boundary override 的最终接口。

V3 当前固定的是几何意图、参数语义和模块边界：

```text
使用 X 侧外圆入口角点 P 和固定斜率构造 X 对侧虚拟覆盖边线，
让普通寻线在被替换后的边线事实中自然生成入环 reference。
```

## 13. 归档状态

V3 现在不是活跃入环策略。它的代码归档边界为：

```text
new/code/archive/circle_v2_v3_fixed_slope_entry_guide/
```

活跃运行时不再包含：

```text
CircleV2BoundaryOverridePlan
BuildReferencePathWithBoundaryOverride()
CIRCLE_V2_ENTRY_FIXED_SLOPE_LEFT_DX_DY
CIRCLE_V2_ENTRY_FIXED_SLOPE_RIGHT_DX_DY
InnerTrace 的 P_est + fixed_slope path
```

当前活跃策略回到更简单的 Circle V2 内圆路径：

```text
InnerTrace
-> 观察锁存方向侧、离当前中线最近的内圆边线
-> 直接生成 CircleV2ReferencePlan
-> adapter 包装为 circle_v2_inner candidate
```

这次回退保留的解耦边界是：

```text
EventObserver 只产生状态事件；
Reducer 只推进 FSM；
GeometryObserver 只观察当前 reference role 所需几何；
ReferenceComposer 只把几何组成 scene reference plan；
Adapter 只包装 plan，不修复、不补线、不调用普通 path builder。
```

若未来重新启用 V3，需要通过新的 OpenSpec change 重新定义 active spec、参数面和测试闭环，不能直接 include 归档代码。
