# Visual Element Sparse Circle V5 路径连通性校验 / Reference Path Connectivity Gate

状态：设计记录。V5 不新增环岛 FSM，不修改 circle / cross 的识别语义，不调整 arbitration 优先级。V5 只修正一个基础路径输出问题：任何生成出来的参考路径，都必须在当前 BEV 白色区域中保持连通，不能跨过黑色区域把两个不连通白区强行连成一条路。

本文档继承 V2/V3/V4 的“互不知晓”和“大道至简”原则：

```text
路径生成模块只生成几何路径；
连通性 helper 只检查路径段是否跨黑；
FSM / element detector / arbitration 不知道连通性检查细节。
```

## 1. 本次修改

V5 固定三件事：

```text
1. 去除路径横向步长/跳变校验：
   将该门限参数设为极大，使它不再作为路径拒绝条件。

2. 新增 BEV 连通性校验 helper：
   对任意两个相邻路径点，零复制读取当前灰度整图，检查二者连线是否存在黑点。
   若存在黑点，则这两个路径点不连通。

3. 将连通性 helper 加入一切当前帧视觉路径输出：
   ordinary line、单边补线、CircleV2 inner/exit、cross exit 以及其它当前帧视觉 reference candidate
   在进入 candidate set / selector 前都必须经过同一套 path connectivity gate。
```

同时新增一个性能调节功能：

```text
BEV_GEOMETRY.SPARSE_ROW_COUNT
```

该参数控制当前帧启用多少条 sparse forward row。默认 `24`。设为 `12` 时，只启用原始 24 条 `FORWARD_SAMPLE_*` 中的前 12 条，即：

```text
FORWARD_SAMPLE_0..FORWARD_SAMPLE_11
```

它不是把 12 条 row 重新均匀分布到原来的近端到远端范围。

这里的“步长校验”指路径点之间横向跳变的业务门，例如当前 ordinary builder 中类似 `ReferenceMaxJump()` / `max_jump_m` 的过滤。它不是 `BEV_GEOMETRY.LATERAL_STEP_M`。

`BEV_GEOMETRY.LATERAL_STEP_M` 是 BEV 横向采样分辨率，不能设为极大；否则会直接破坏 row scan 的白色区间事实。

## 2. 问题结论

现有路径生成中存在一个错误替代关系：

```text
相邻路径点横向变化不大
=> 被当成路径合理
```

但真实需要的是：

```text
相邻路径点之间没有穿过黑色区域
=> 路径在当前 BEV 白色区域中连通
```

横向跳变门不是拓扑判断。它会产生两类问题：

```text
正常弯道 / 环岛内侧：
  真实可走路径可能横向变化较大，被 max_jump 错杀。

不连通白区：
  两个点横向变化可能不大，但中间隔着黑区，仍被拼成一条路径。
```

所以 V5 的方向是：

```text
不要用 lateral jump 猜路径是否合理；
直接用当前灰度整图检查路径段是否跨黑。
```

## 3. 去除步长校验

第一版落地按用户明确要求处理：

```text
reference_lateral_jump_gate_m = very_large_value
```

推荐语义：

```cpp
constexpr float kDisabledReferenceLateralJumpGateM = 1000.0F;
```

调用侧可以保留现有函数形态以降低改动面：

```cpp
float ReferenceMaxJump(const port::RuntimeParameters& params) {
  return params.bev_geometry.reference_lateral_jump_gate_m;
}
```

默认参数：

```text
BEV_GEOMETRY.REFERENCE_LATERAL_JUMP_GATE_M = 1000.0
```

该值远大于当前 BEV 横向搜索范围，正常帧中不会触发拒绝。后续如果代码稳定，可以再机械删除 `max_jump` 相关分支；V5 的语义已经是不依赖该校验。

保留的路径约束：

```text
leading contiguous；
sample finite；
path point count >= downstream min samples；
current BEV connectivity gate。
```

删除/失效化的路径约束：

```text
因为相邻点 lateral_m 差值超过 max_jump 而拒绝候选；
因为单边边线 neighbor lateral_m 差值超过 max_jump 而拒绝补线；
用 lateral jump 代替白区连通性判断。
```

## 4. BEV 连通性 Helper

新增一个纯路径校验 helper，建议命名：

```text
BEVPathConnectivityHelper
ReferencePathConnectivityClipper
SegmentHasNoBlackBetweenPoints
```

它不是：

```text
CircleConnectivityChecker
OrdinaryLineGuard
CrossFilter
FSMValidator
```

因为它不属于任何元素或状态机。

### 4.1 输入

helper 输入：

```text
gray_frame_view:
  当前帧灰度整图的非拥有视图。
  对应现有 port::LegacyCameraFrameView：
    const uint8_t* gray
    width
    height
    stride
    frame_id
    capture_time_ms

bev_projector:
  当前帧使用的 BEVProjector。
  用于把 BEV path segment 投影回原始图像坐标。

threshold / classification:
  本帧 Otsu threshold；
  本帧 BEV_CLASSIFICATION 参数。
  分类语义必须与 sparse row scan 完全一致。

point_a, point_b:
  同一条 reference path 中相邻的两个 BEV 路径点。
  坐标使用同一 BEV 坐标系：
    x = lateral_m
    y = forward_m
```

helper 不接收复制出来的 dense BEV class grid，也不读取 debug-only `BEVSimpleImage.classes`。`BEVSimpleImage` 仍然只服务展示和离线观察，不能变成控制事实来源。

这里的零复制首先是性能约束：

```text
不要每帧为连通性校验额外构造一张 BEV dense classification raster；
不要复制 320x240 灰度图；
不要把所有 BEV 网格点预分类后只用其中少数路径段；
只对最终候选路径的相邻 segment 做按需采样。
```

也就是说，连通性校验的成本应接近：

```text
O(candidate_path_segment_pixels)
```

而不是：

```text
O(full_frame_pixels) + O(full_bev_grid_pixels)
```

这比“先生成整图再查表”更符合当前控制周期内的性能预算。

推荐公共输入形态：

```cpp
struct ReferenceConnectivityFrameView {
  const port::LegacyCameraFrameView& gray_frame;
  const legacy::BEVProjector& projector;
  int threshold;
  const port::BEVClassificationParameters& classification;
};
```

该 view 不拥有图像数据，不分配整图缓存，不跨帧保存引用。调用方必须保证它和当前 frame / sparse rows / candidate paths 来自同一次 capture。

### 4.2 输出

helper 输出窄语义：

```cpp
enum class BEVSegmentConnectivity {
  kConnected,
  kBlockedByBlack,
};
```

或者更简单：

```cpp
bool SegmentHasNoBlackBetweenPoints(...);
```

V5 不引入置信度、不引入多级 reason 策略、不根据元素类型调整阈值。

### 4.3 规则

对两个路径点 `A` 和 `B`：

```text
将 A、B 通过 BEVProjector 投影到原始图像坐标；
在原始图像中沿投影后的 A-B 线段遍历像素；
直接读取 gray_frame.gray[row * stride + col]；
用本帧 threshold + BEV_CLASSIFICATION 复用 ClassifyBevPixel 语义；
如果线段覆盖到任意 black pixel：
  segment disconnected；
否则：
  segment connected。
```

端点也属于线段。若路径点本身投影到 black 像素上，该 segment 失败。

采样必须覆盖投影线段经过的图像像素，不能只取少量插值点。推荐使用 image-space supercover line traversal 或等价实现，保证不会因为斜率或栅格跨越漏掉中间黑点。

这里选择 image-space traversal，而不是先构造 BEV dense raster，原因是：

```text
BEVProjector 是单应投影，BEV 直线段投影到图像后仍是直线段；
原始灰度整图已经存在；
逐段按需读取 gray 指针即可完成检查；
不需要复制 320x240 灰度图；
不需要构造 BEV 分类整图；
不需要为未被输出的候选路径做整图预处理；
不需要把 debug dense BEV 升级为控制输入。
```

实现上应避免在 helper 内分配 per-frame 大 buffer。允许使用少量栈上局部变量或固定小对象遍历线段；若后续为了 telemetry 记录阻断位置，也只记录 first blocked segment / pixel 这类窄事实，不保存整张中间图。

第一版只检查 black barrier：

```text
black   => 阻断；
white   => 通过；
unknown => 不当作 black；
invalid => 不当作 black。
```

原因是本次用户明确要求“检查两个路径点的连线之间是否存在黑点”。若后续要把 unknown / invalid 也纳入阻断，应作为独立版本讨论，不能混入 V5。

## 5. Path 级校验

在 segment helper 之上提供 path helper：

```cpp
bool ReferencePathHasNoBlackSegments(const ReferenceConnectivityFrameView& frame,
                                     const port::BEVReferencePath& path);
```

语义：

```text
从 index 0 开始读取 leading present samples；
相邻 present samples 两两检查；
遇到第一个 absent sample 后停止；
若任何相邻 segment blocked_by_black，则该 segment 之后的 sample 不属于 connected leading prefix；
不跳过洞；
不使用后续 sample 补洞。
```

只有一个 present sample 时无法形成 segment。连通性 helper 不负责判断 sample 数量是否足够；最小样本数仍由现有 usability / readiness 层负责。

现场验证后修正：

```text
path-level connectivity 不应把“一处远端断开”升级为“整条 candidate 不存在”。
正确语义是裁剪到第一个 blocked segment 前的 connected leading prefix。
prefix 是否足够长、是否可用于控制，仍由 usability / readiness 层判断。
```

## 6. 接入位置

V5 不要求每个路径生成器复制一段校验逻辑。优雅接入方式是建立一个统一输出门：

```text
path generator
-> BEVReferencePath
-> ReferencePathConnectivityClipper(gray_frame_view + projector)
-> VisualReferenceCandidate
-> SelectVisualReference
```

也就是说：

```text
ordinary builder 不知道 circle；
circle composer 不知道 ordinary；
cross builder 不知道 circle；
connectivity clipper 不知道 candidate priority；
selector 只看已经裁剪后的 candidate；
usability 只看最终 sample 数量和连续性。
```

推荐落地边界：

```text
Build / compose path:
  只负责生成 BEVReferencePath。

Validate generated path:
  统一调用同一个 connectivity helper，裁剪到 connected leading prefix。
  输入是当前灰度帧 view，不是复制出的 BEV 图。

Append / select candidate:
  接收裁剪后的 VisualReferenceCandidate。
```

若当前代码短期内更容易在多个 adapter 处调用 helper，也必须使用同一个 helper，不允许各路径类型实现私有版本。

## 7. 详细落地方案

### 7.1 新增 helper 文件

新增中性 helper：

```text
new/code/legacy/steering_reference_connectivity.hpp
new/code/legacy/steering_reference_connectivity.cpp
```

选择 `legacy/` 的原因：

```text
1. helper 复用 legacy::BEVProjector 和 legacy::ClassifyBevPixel；
2. ordinary / cross 当前都在 legacy 层产出 candidate；
3. runtime pipeline 可以依赖 legacy helper；
4. helper 不属于 CircleV2 detail，也不属于 visual reference selector。
```

不要放在：

```text
runtime/detail/circle_v2/
steering_bev_simple_perception.cpp 内部匿名 namespace
steering_visual_reference_orchestration.cpp
cross evidence 文件
```

这些位置都会把通用路径校验绑定到某个调用方，破坏互不知晓。

### 7.2 Helper 公共 API

第一版只暴露最小 API：

```cpp
struct ReferenceConnectivityFrameView {
  const port::LegacyCameraFrameView& gray_frame;
  const legacy::BEVProjector& projector;
  int threshold;
  const port::BEVClassificationParameters& classification;
};

bool ReferencePathHasNoBlackSegments(
    const ReferenceConnectivityFrameView& frame,
    const port::BEVReferencePath& path);

void AppendConnectedVisualReferenceCandidate(
    const ReferenceConnectivityFrameView& frame,
    const port::VisualReferenceCandidate& candidate,
    std::vector<port::VisualReferenceCandidate>& accepted_candidates);
```

可选内部 helper：

```cpp
bool SegmentHasNoBlackBetweenPoints(
    const ReferenceConnectivityFrameView& frame,
    const port::BEVPoint& a,
    const port::BEVPoint& b);
```

`SegmentHasNoBlackBetweenPoints()` 可以只在 `.cpp` 中保持内部函数。除非测试确实需要直接覆盖 segment 级行为，否则不要扩大 public API。`AppendConnectedVisualReferenceCandidate()` 是 candidate 级统一接入点：它只裁剪 connected leading prefix，不判断 prefix 是否足够用于控制。

不需要：

```text
candidate kind 参数；
circle direction 参数；
reference source 参数；
置信度；
按场景切换策略；
动态阈值；
整图缓存对象。
```

### 7.3 Segment 实现细节

实现步骤：

```text
1. ProjectVehicleToImage(a)；
2. ProjectVehicleToImage(b)；
3. 若投影失败或越界，按 V5 第一版语义不视为 black 阻断；
4. 对投影后的 image-space 线段做 supercover / Bresenham-like 遍历；
5. 对每个覆盖像素直接读取：
   gray_frame.gray[row * gray_frame.stride + col]
6. 调用 ClassifyBevPixel(gray, threshold, classification)；
7. 只要任一像素为 BEVSimplePixelClass::kBlack，则返回 false；
8. 遍历结束未遇到 black，则返回 true。
```

第 3 点不是防御性兜底，而是 V5 的窄语义：

```text
本 helper 只回答“是否跨 black”；
unknown / invalid / out-of-frame / projection-failed 都不等价于 black；
投影器健康、感知 stale、reference sample 数量由现有 health / usability / safety gate 负责。
```

遍历要求：

```text
必须覆盖线段经过的像素；
不能只按固定少量 t 插值；
不能先生成 dense BEV classification raster；
不能复制灰度图。
```

实现可以只使用局部整数和少量栈变量。不要在 helper 内分配 per-frame buffer。

### 7.4 Path 实现细节

`ReferencePathHasNoBlackSegments()` 的语义：

```text
从 sampled_path[0] 开始；
读取 leading present finite samples；
相邻 present samples 两两调用 SegmentHasNoBlackBetweenPoints()；
遇到第一个 absent / 非有限 sample 后停止；
若没有形成任何 segment，则返回 true；
任一 segment 返回 false，则整个 path false。
```

它不负责：

```text
判断 sample 数量是否够；
判断 reference 是否 usable；
选择 line/cross/circle；
更新 hold；
记录 CircleV2 状态。
```

最小样本数仍由 `EvaluateReferenceUsability()` / downstream readiness 保持职责。

### 7.5 Runtime pipeline 接入点

当前统一汇总点在：

```text
new/code/runtime/steering_frame_perception_pipeline.cpp
```

现有结构是：

```text
line_candidate
element_result.candidates
circle_candidate
-> candidates
-> SelectVisualReference(candidates)
```

V5 应改成：

```text
line_candidate
element_result.candidates
circle_candidate
-> current_frame_visual_candidates
-> ReferencePathConnectivityClipper
-> connected_prefix_visual_candidates
-> SelectVisualReference(connected_prefix_visual_candidates)
```

建议伪代码：

```cpp
const legacy::ReferenceConnectivityFrameView connectivity_frame{
    capture.view,
    projector_,
    threshold,
    params.bev_classification,
};

std::vector<port::VisualReferenceCandidate> candidates;

auto append_connected_prefix =
    [&](const port::VisualReferenceCandidate& candidate) {
      if (!candidate.present) {
        candidates.push_back(candidate);
        return;
      }
      legacy::AppendConnectedVisualReferenceCandidate(connectivity_frame,
                                                      candidate,
                                                      candidates);
    };

append_connected_prefix(line_candidate);
for (const auto& candidate : element_result.candidates) {
  append_connected_prefix(candidate);
}
if (circle_candidate.has_value()) {
  append_connected_prefix(*circle_candidate);
}

visual_selection = legacy::SelectVisualReference(candidates);
```

如果实现时希望 `candidate_paths` 只展示 selector 实际看见的候选，则在过滤后 append：

```text
connected_prefix_visual_candidates
-> candidate_paths
-> SelectVisualReference
```

如果后续需要 debug 被裁剪的候选，应新增窄 telemetry 字段，而不是让 selector 理解黑点、像素或场景来源。

### 7.6 不修改的模块

V5 落地不应修改以下职责：

```text
CircleV2Scene:
  仍只输出 CircleV2ReferencePlan。

CircleV2ReferenceAdapter:
  仍只把 plan 包装成 VisualReferenceCandidate。

RunVisualElementPipeline:
  仍只输出 cross / element candidates。

BuildCrossExitVisualReferenceCandidate:
  不知道 connectivity。

MakeLineVisualReferenceCandidate:
  不知道 connectivity。

SelectVisualReference:
  仍只在已经给到的候选里按现有规则选择。
```

原因：

```text
路径生成者负责生成路径；
连通性 helper 负责检查路径；
selector 负责选择候选；
三者互不知晓。
```

### 7.7 Hold 不纳入 V5 gate

V5 的连通性 gate 只覆盖当前帧视觉候选，不覆盖 hold-last。

明确不做：

```text
BuildReferenceHoldCandidate
-> ReferencePathConnectivityClipper
```

原因：

```text
hold 是 reference continuity 的历史桥接结果；
它不是当前帧视觉路径生成器输出；
本轮 V5 目标是阻止当前帧视觉 candidate 把不连通白区拼成路径；
强行把 hold 接入灰度帧 / projector 会扩大职责边界。
```

如果未来证据表明 hold 自身需要当前帧可行性约束，应另起一个 selected-reference safety gate 讨论，而不是混进 V5 的 current-frame visual candidate gate。

### 7.8 去除 lateral jump 门

当前 ordinary builder 中仍有横向跳变门：

```text
ReferenceMaxJump(params)
```

V5 第一版按最小改动落地：

```text
新增 BEV_GEOMETRY.REFERENCE_LATERAL_JUMP_GATE_M；
默认 1000.0；
ReferenceMaxJump(params) 返回该参数；
参数解析和 media config snapshot 暴露该值。
```

这样做的好处：

```text
不大规模删除现有函数和调用点；
不误用 BEV_GEOMETRY.LATERAL_STEP_M；
让旧 jump gate 在行为上失效；
后续确认稳定后再做机械清理。
```

禁止：

```text
把 LATERAL_STEP_M 设大来绕过 jump gate；
在 ordinary/circle/cross 各自加私有 max_jump；
用 lateral jump 继续代替黑点连通性判断。
```

### 7.9 SPARSE_ROW_COUNT 已独立落地

`SPARSE_ROW_COUNT` 是性能调节参数，和连通性 helper 解耦。

落地边界：

```text
BEVGeometryParameters:
  保存 sparse_row_count，默认 24。

param_store:
  读取并校验范围 1..24。

ScanSparseRows:
  只扫描 forward_samples_m 的前 sparse_row_count 项。

BEVSampleProjectionLut:
  只构建 active sparse rows 的投影 entries。

BEVReferencePath:
  仍保持 24 容量，未启用行保持 absent。
```

不要把 `SPARSE_ROW_COUNT` 和连通性 helper 混在一起：

```text
row 数量决定生成多少候选点；
connectivity 决定这些候选点之间是否跨 black；
两者互不知晓。
```

## 8. 覆盖范围

V5 的连通性 gate 应覆盖：

```text
ordinary line candidate；
ordinary 单边补线生成的中心路径；
CircleV2 InnerTrace reference plan；
CircleV2 ExitTrace reference plan；
cross exit candidate；
未来任何当前帧视觉 reference candidate。
```

不覆盖：

```text
hold-last reference continuity result。
```

## 9. 与 V4 单边补线 Helper 的关系

V4 单边补线 helper 仍只负责：

```text
boundary_trace
-> normal offset
-> resampled reference points
```

它不检查白区、不检查黑点、不知道 BEV 图。

V5 的新 helper 位于其后：

```text
SingleBoundaryOffsetHelper
-> BEVReferencePath
-> ReferencePathConnectivityClipper
-> VisualReferenceCandidate
```

这样职责保持清楚：

```text
一个 helper 负责生成几何；
一个 helper 负责验证当前 BEV 拓扑；
二者互不知晓。
```

## 10. 与 CircleV2 的关系

CircleV2 FSM 不变：

```text
Idle -> Approach -> InnerTrace -> ExitTrace -> Idle
```

CircleV2 geometry observer / composer 仍只负责找到内圆边线或外侧边线并生成 reference plan。

V5 只在 reference plan 输出后增加统一路径连通性 gate：

```text
CircleV2Scene
-> CircleV2ReferencePlan
-> AdaptCircleV2ReferencePlan
-> ReferencePathConnectivityClipper
-> candidate set
```

若 CircleV2 生成的路径跨黑：

```text
本帧 candidate 裁剪到 connected leading prefix；
若 prefix 样本不足，则由 usability / readiness 自然拒绝；
不回退 FSM；
不改变 CircleV2Memory；
不触发新的 circle/cross 判定。
```

路径可用性和状态推进保持解耦。

## 11. 数据流

V5 后的路径输出链路为：

```text
Camera frame
-> Otsu threshold
-> BEV projection / sparse row classification
   -> sparse rows
   -> current gray frame view
   -> BEV projector view
-> path generators
   -> ordinary path
   -> single-boundary offset path
   -> circle path
   -> cross path
-> Current-frame Visual Candidate Connectivity Gate
   -> ReferencePathConnectivityClipper
   -> clip path at first adjacent segment crossing black
-> VisualReferenceCandidate set
-> SelectVisualReference
-> reference usability / tracking geometry / readiness
-> safety gate
-> control
```

`gray frame view`、`sparse rows`、`threshold` 和 `BEVProjector` 来自同一帧、同一阈值、同一 BEV 标定。任何 helper 都不能跨帧保存这些 view。

## 12. 参数

新增或固定参数：

```text
BEV_GEOMETRY.REFERENCE_LATERAL_JUMP_GATE_M = 1000.0
BEV_GEOMETRY.SPARSE_ROW_COUNT = 24
```

`REFERENCE_LATERAL_JUMP_GATE_M` 只用于失效化旧步长/跳变门。它不是调参主路径。

`SPARSE_ROW_COUNT` 是 sparse row 活跃前缀长度：

```text
合法范围：1..24；
默认值：24；
设为 12：只启用 FORWARD_SAMPLE_0..11；
不重新计算 forward_samples_m；
不改变 FORWARD_SAMPLE_* 的物理位置；
不改变 BEV_REFERENCE path 的固定容量，只让后续 sample absent。
```

这样调小 row 数量时，性能收益来自少扫描远端行、少构建对应 LUT 条目、少生成候选路径段，而不是改变采样分布。

不新增：

```text
circle 专用连通性阈值；
cross 专用连通性阈值；
ordinary 专用连通性阈值；
黑点数量容忍度；
按 confidence 加权的复杂策略。
```

BEV 连通性 helper 使用现有灰度图和分类语义：

```text
LegacyCameraFrameView.gray
current frame Otsu threshold
BEV_CLASSIFICATION.WHITE_CONFIDENCE_MIN
BEV_CLASSIFICATION.UNKNOWN_CONFIDENCE_MIN
```

V5 不定义新的颜色分类阈值。

## 13. 测试要求

### 13.1 Segment helper 单测

构造小灰度图和简单 projector：

```text
投影线段经过的像素全 white：
  任意相邻路径点 segment connected。

投影线段中间有 black pixel：
  segment blocked。

端点投影像素在 black：
  segment blocked。

斜线投影穿过 black pixel：
  必须 blocked，不能因采样稀疏漏过。
```

单测不需要构造 dense BEV class grid。它应验证 helper 直接读取传入的灰度 buffer；修改灰度 buffer 中某个像素后，下一次 helper 调用立即反映该变化，证明没有隐藏缓存副本。

### 13.2 Path helper 单测

```text
leading path 所有相邻 segment 无 black -> 通过；
任一相邻 segment 有 black -> 不通过；
遇到 absent 后停止，不检查后续 samples；
不因单点 path 自行决定可用性。
```

### 13.3 集成测试

至少覆盖：

```text
ordinary line candidate 经过统一 gate；
CircleV2 inner candidate 经过统一 gate；
CircleV2 exit candidate 经过统一 gate；
cross exit candidate 经过统一 gate；
不连通白区中生成的候选会裁剪到 connected leading prefix；
prefix 样本不足时由 usability / readiness 拒绝；
正常弯道不再因为 lateral jump 被 max_jump 错杀。
```

### 13.4 证据字段

debug / media 可以新增窄事实：

```text
candidate_connectivity.checked = true/false
candidate_connectivity.blocked_by_black = true/false
candidate_connectivity.first_blocked_segment_index = N
candidate_connectivity.connected_prefix_samples = N
```

这些只是观测事实，不参与 selector 的优先级评分。

## 14. 不变量

V5 落地后必须满足：

```text
路径横向跳变不再是业务拒绝条件；
路径是否跨黑由统一 BEV connectivity helper 判断并裁剪；
所有当前帧视觉 reference candidate 使用同一个 helper；
connectivity failure 不改变 FSM；
connectivity failure 不改变 detector；
connectivity failure 不改变 arbitration priority；
connectivity helper 不知道路径来自 ordinary、circle 还是 cross。
```

最终目标：

```text
路径可以大幅弯曲；
路径可以贴着内圆；
路径可以来自单边补线；
但路径不能跨过当前灰度图里的黑色阻断。
```
