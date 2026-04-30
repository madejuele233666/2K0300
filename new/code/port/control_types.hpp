#ifndef LS2K_PORT_CONTROL_TYPES_HPP
#define LS2K_PORT_CONTROL_TYPES_HPP

// 控制系统的核心类型定义。
// 包含所有感知管线数据结构：BEV 采样、走廊间隔、元素证据、拓扑假设、
// 场景状态机、参考路径、控制误差模型输出、运行时参数等。
// 该文件是整个转向/感知系统的数据契约层。

#include <array>
#include <cstdint>
#include <string>

namespace ls2k::port {

// 编译期固定摄像头分辨率：320x240
// 注意：运行时 camera_frame_width/height 参数可覆盖这些值

constexpr int kCompiledCameraFrameWidth = 320;
constexpr int kCompiledCameraFrameHeight = 240;

// 摄像头几何适配标记，用于追踪帧的几何适配状态
enum class CameraGeometryMarker {
    kPhase1Adapted,       //!< 已完成 Phase1 几何适配
    kNonPhase1Geometry,   //!< 非 Phase1 几何（旧版/不同标定）
    kEmptyFrame,          //!< 空帧（无像素数据）
    kAdapterNotReady,     //!< 适配器未就绪
    kAdaptationHookRouted //!< 适配钩子已路由
};

// 摄像头灰度帧，固定分辨率 320x240，用于 BEV 投影和采样
struct LegacyCameraFrame {
    std::array<uint8_t, kCompiledCameraFrameWidth * kCompiledCameraFrameHeight> gray{}; //!< 灰度像素缓冲区
    int width = 0;  //!< 实际宽度（可能小于编译期固定值）
    int height = 0; //!< 实际高度

    // 返回有效像素总数，宽高为 0 时返回 0
    std::size_t PixelCount() const {
        if (width <= 0 || height <= 0) {
            return 0;
        }
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
};

// 摄像头采集结果，包含帧数据和元信息
struct CameraCapture {
    bool has_frame = false;        //!< 是否有有效帧
    LegacyCameraFrame frame{};     //!< 灰度帧数据
    CameraGeometryMarker marker = CameraGeometryMarker::kAdapterNotReady; //!< 几何适配状态
    int source_width = 0;          //!< 源图像宽度（适配前）
    int source_height = 0;         //!< 源图像高度（适配前）
    uint64_t frame_id = 0;         //!< 帧序号
    uint64_t capture_time_ms = 0;  //!< 采集时间戳（毫秒）
};

// 图像坐标系中的像素点（行、列）
struct ImagePoint {
    float row_px = 0.0F; //!< 行坐标（像素）
    float col_px = 0.0F; //!< 列坐标（像素）
};

// BEV（鸟瞰图）车辆坐标系中的点，前向/横向以米为单位
struct BEVPoint {
    float forward_m = 0.0F; //!< 车辆前向距离（米），正前方为正
    float lateral_m = 0.0F; //!< 横向距离（米），右正左负
};

// BEV 标定使用的对应点对数（源/目标各 4 点，解 3x3 单应矩阵至少需要 4 对）
constexpr std::size_t kBevCalibrationPointCount = 4;
// BEV 前向采样层数，每层对应一个前向距离，覆盖从近到远的 24 个离散前向位置
// 这是整个感知管线的核心维度常量，决定了 track estimate、corridor graph、reference path 的数组大小
constexpr std::size_t kBevTrackSampleCount = 24;

// 特殊场景类型枚举，代表感知系统可识别的所有路况类别
enum class SpecialSceneKind {
    kOrdinary,    //!< 普通直行/弯道
    kBend,        //!< 弯道（纯跟线，无需特殊状态机干预）
    kCross,       //!< 十字路口
    kZebra,       //!< 斑马线
    kCircleLeft,  //!< 左侧环岛
    kCircleRight  //!< 右侧环岛
};

// 特殊场景状态机阶段枚举，描述场景的生命周期状态
enum class SpecialScenePhase {
    kIdle,      //!< 空闲/无特殊场景
    kCandidate, //!< 候选：检测到潜在特殊场景，等待确认
    kConfirm,   //!< 确认：特殊场景已确认进入
    kEntry,     //!< 入口：正在进入特殊区域（如环岛入口）
    kInterior,  //!< 内部：在特殊区域内部行驶
    kExit,      //!< 出口：正在离开特殊区域
    kHold,      //!< 保持：保持上次有效路径
    kRelease    //!< 释放：退出特殊场景，返回普通模式
};

// 参考路径生成模式，描述从 track 生成控制参考路径的策略
enum class ReferenceMode {
    kCenterline,               //!< 默认模式：沿中心线行驶
    kInnerOffset,              //!< 内偏移：环岛内侧跟线
    kOuterOffset,              //!< 外偏移：环岛外侧跟线
    kBlend,                    //!< 混合模式：在两种路径之间插值过渡
    kHoldLast,                 //!< 保持上次路径：当感知短暂失效时保持
    kEntryHeadingExtension,    //!< 入口航向延伸：进入特殊区域时延伸当前航向
    kStableBoundaryOffset,     //!< 稳定边界偏移：基于稳定观测边界偏移
    kArcFollow,                //!< 弧线跟随：沿固定曲率弧线行驶
    kBlendToExit,              //!< 混合至出口：从内部路径过渡到出口路径
    kLostPrediction,           //!< 丢失预测：完全丢失感知时的猜测路径
    kCircleInnerIsland         //!< 环岛内圆黑区记忆路径
};

// 路径中的一个采样点，包含有效性标志、BEV 坐标和置信度
struct BEVPathSample {
    bool valid = false;     //!< 该点是否有有效投影/观测
    BEVPoint point{};       //!< BEV 坐标（前向距离 + 横向距离）
    float confidence = 0.0F; //!< 该点的置信度（0-1）
};

// BEV 稀疏采样点的分类结果
enum class BEVSampleClass {
    kInvalidOutsideImage,    //!< 无效点：投影到图像外
    kUnknownLowConfidence,   //!< 低置信度无法分类
    kBackground,             //!< 背景（不可行驶区域）
    kDrivable                //!< 可行驶区域
};

// BEV 稀疏采样点 —— 感知系统的基本证据单元
// 包含 BEV 坐标、图像坐标、像素特征和分类结果
struct BEVSample {
    BEVPoint point{};                     //!< BEV 坐标（米）
    ImagePoint image{};                   //!< 图像坐标（像素）
    BEVSampleClass sample_class = BEVSampleClass::kInvalidOutsideImage; //!< 采样点分类
    float raw_intensity = 0.0F;           //!< 中心像素原始灰度值（0-255）
    float patch_min_intensity = 0.0F;     //!< 采样 patch 内最小灰度
    float patch_max_intensity = 0.0F;     //!< 采样 patch 内最大灰度
    float patch_purity = 0.0F;            //!< patch 内与中心灰度同侧的比例（纯度）
    float confidence = 0.0F;              //!< 分类置信度（0-1）
    bool valid_image_projection = false;   //!< 是否成功投影到图像内
    bool near_image_border = false;        //!< 是否靠近图像边缘（边缘处投影可能不可靠）
};

// 走廊间隔边界证据类型，描述边界的观测来源和可信度
// 只有 kObservedDrivableBackground 表示真实观测到的边缘
enum class BEVBoundaryEvidenceKind {
    kNone,                         //!< 无证据
    kObservedDrivableBackground,   //!< 观测到背景→可行驶的边界（真实边缘，可信）
    kSearchWindowEdge,             //!< 到达搜索窗口边界（可能是截断，不是真实边缘）
    kInvalidOutsideImage,          //!< 投影到图像外（无效）
    kUnknownLowConfidence,         //!< 低置信度区域（无法确定边界）
    kImageBorder,                  //!< 到达图像边界（截断边界）
    kNonBackgroundAdjacent         //!< 相邻点非背景（可能是噪声）
};

// 每层 forward distance 上的可行驶走廊间隔
// 从稀疏采样点中提取，表示在某前向距离下哪段横向区间是可行驶的
struct CorridorInterval {
    float forward_m = 0.0F;                //!< 该层前向距离（米）
    float lateral_min_m = 0.0F;            //!< 可行驶区间左边界（米）
    float lateral_max_m = 0.0F;            //!< 可行驶区间右边界（米）
    float lateral_center_m = 0.0F;         //!< 区间中心横向位置（米）
    float width_m = 0.0F;                  //!< 区间宽度（米）
    bool left_edge_valid = false;          //!< 左边界是否有效（真实观测到非截断边界）
    bool right_edge_valid = false;         //!< 右边界是否有效
    BEVBoundaryEvidenceKind left_boundary_evidence = BEVBoundaryEvidenceKind::kNone;  //!< 左边界的证据类型
    BEVBoundaryEvidenceKind right_boundary_evidence = BEVBoundaryEvidenceKind::kNone; //!< 右边界的证据类型
    float left_opening_score = 0.0F;       //!< 左侧开口程度（越大表示越开阔/无约束）
    float right_opening_score = 0.0F;      //!< 右侧开口程度
    float valid_sample_ratio = 0.0F;       //!< 该区间内有效采样点比例
    float confidence = 0.0F;               //!< 该间隔的置信度
};

// 走廊图边 —— 连接相邻层的走廊间隔，构成路径候选
struct CorridorGraphEdge {
    int from_layer = -1;       //!< 起始层索引
    int from_interval = -1;    //!< 起始间隔索引
    int to_layer = -1;         //!< 目标层索引
    int to_interval = -1;      //!< 目标间隔索引
    float overlap_score = 0.0F;     //!< 前后间隔横向重叠程度（0-1）
    float center_jump_cost = 0.0F;  //!< 中心偏移代价（中心点突变成本）
    float width_change_cost = 0.0F; //!< 宽度变化代价
    float curvature_cost = 0.0F;    //!< 曲率代价（路径弯曲程度）
    float confidence = 0.0F;        //!< 这条边可信度
};

// 路径候选 —— 从走廊图中提取的一个完整路径假设（覆盖所有 24 层）
struct PathCandidate {
    bool valid = false;                                              //!< 是否有效
    ReferenceMode mode = ReferenceMode::kCenterline;                 //!< 路径生成模式
    std::array<BEVPathSample, kBevTrackSampleCount> sampled_path{};  //!< 24 层离散采样路径点
    float confidence = 0.0F;         //!< 路径总体置信度
    float mean_width_m = 0.0F;       //!< 路径全程平均宽度
    float width_stability = 0.0F;    //!< 宽度稳定性（一致性）
    float curvature = 0.0F;          //!< 路径曲率估计
    float curvature_consistency = 0.0F; //!< 曲率一致性
    float start_forward_m = 0.0F;    //!< 路径起始前向距离
    float end_forward_m = 0.0F;      //!< 路径终止前向距离
};

// 道路假设集合 —— 所有可能路径候选的容器
// 参考路径选择器会根据场景和拓扑证据从中选择最终控制路径
struct RoadHypotheses {
    PathCandidate ordinary{};              //!< 普通路径（沿可行走廊中心线）
    PathCandidate left_arc{};              //!< 左弧线（弯道左转倾向）
    PathCandidate right_arc{};             //!< 右弧线（弯道右转倾向）
    PathCandidate forward_exit{};          //!< 前向出口（十字路口直行）
    PathCandidate cross_exit{};            //!< 十字路口出口路径
    PathCandidate circle_inner_left{};     //!< 左侧环岛内侧路径
    PathCandidate circle_inner_right{};    //!< 右侧环岛内侧路径
    PathCandidate circle_outer_guard_left{};  //!< 左侧环岛外侧保持路径
    PathCandidate circle_outer_guard_right{}; //!< 右侧环岛外侧保持路径
    PathCandidate circle_exit_left{};      //!< 左侧环岛出口路径
    PathCandidate circle_exit_right{};     //!< 右侧环岛出口路径
    PathCandidate left_branch{};           //!< 左分支（岔路向左）
    PathCandidate right_branch{};          //!< 右分支（岔路向右）
    PathCandidate zebra_hold{};            //!< 斑马线保持路径
};

// 每层 forward 的行剖面证据：统计该层采样点的分类分布
struct BEVRowProfileEvidence {
    bool valid = false;              //!< 该行剖面是否有效
    float forward_m = 0.0F;          //!< 前向距离
    float valid_lateral_min_m = 0.0F;  //!< 有效采样点横向最小值
    float valid_lateral_max_m = 0.0F;  //!< 有效采样点横向最大值
    float valid_width_m = 0.0F;      //!< 有效采样点覆盖宽度
    float drivable_ratio = 0.0F;     //!< 可行驶采样点比例
    float unknown_ratio = 0.0F;      //!< 未知采样点比例
    float invalid_ratio = 0.0F;      //!< 无效采样点比例（投影到图像外）
    float largest_non_drivable_gap_m = 0.0F; //!< 最大不可行驶间隙宽度
    int valid_sample_count = 0;      //!< 有效采样点数量
    int drivable_sample_count = 0;   //!< 可行驶采样点数量
};

// 十字路口带状证据 —— 检测前方横向可行驶带（过马路的横向通道）
struct BEVCrossBandEvidence {
    bool present = false;               //!< 是否检测到十字路口带
    float score = 0.0F;                 //!< 十字路口存在置信度（0-1）
    float forward_m = 0.0F;             //!< 十字路口带的前向位置
    float drivable_ratio = 0.0F;        //!< 带内可行驶采样点比例
    float valid_width_m = 0.0F;         //!< 有效采样点覆盖宽度
    float largest_non_drivable_gap_m = 0.0F; //!< 最大不可行驶间隙
    float invalid_penalty = 0.0F;       //!< 无效投影惩罚因子
    int consecutive_dense_rows = 0;     //!< 连续稠密行数（稳定性的度量）
};

// 环岛角落证据 —— 检测环岛入口处的路沿转角特征
struct BEVCircleCornerEvidence {
    bool present = false;              //!< 是否检测到环岛转角
    float score = 0.0F;                //!< 转角存在的综合置信度（0-1）
    float forward_m = 0.0F;            //!< 转角前向位置
    float lateral_m = 0.0F;            //!< 转角横向位置
    float angle_score = 0.0F;          //!< 转角角度评分（越接近 90° 评分越高）
    float clarity_score = 0.0F;        //!< 转角清晰度评分
    float smooth_curve_reject_score = 0.0F; //!< 平滑曲线拒斥分（排除将普通弯道误检为环岛）
    float invalid_penalty = 0.0F;      //!< 无效投影惩罚因子
    int support_layers = 0;            //!< 支持该检测的前向层数
};

// 环岛内圆黑区证据 —— 角点附近竖线的 白-黑-白 语义及面向道路的黑区边缘
struct BEVCircleInnerIslandEvidence {
    bool present = false;              //!< 完整 white-black-white 内圆标定是否成立
    bool edge_present = false;         //!< 当前帧是否有可用的 traced road-facing edge
    bool compatible_with_memory = false; //!< 当前 edge 是否与既有记忆兼容
    bool trace_present = false;        //!< 连续内圆边缘追踪是否成立
    float score = 0.0F;                //!< 内圆黑区证据评分
    float scan_lateral_m = 0.0F;       //!< 标定竖线横向位置
    float black_start_forward_m = 0.0F; //!< 黑区前向起点
    float black_end_forward_m = 0.0F;  //!< 黑区前向终点
    float trace_start_forward_m = 0.0F; //!< traced edge 起点
    float trace_end_forward_m = 0.0F;  //!< traced edge 终点
    float trace_confidence = 0.0F;     //!< traced edge 平均置信度
    float invalid_penalty = 0.0F;      //!< 无效/图像边缘惩罚
    int support_lines = 0;             //!< 支持该黑区语义的竖线数量
    int trace_support_layers = 0;      //!< traced edge 的真实观测层数
    int trace_gap_layers = 0;          //!< traced edge 中插值补齐的缺口层数
    int rejected_far_segments = 0;     //!< 未接入 trace 的远端孤立段数量
    std::array<BEVPathSample, kBevTrackSampleCount> raw_road_facing_edge{}; //!< 原始逐层候选调试
    std::array<BEVPathSample, kBevTrackSampleCount> road_facing_edge{}; //!< traced 黑区面向道路的边缘
};

// BEV 元素证据 —— 十字路口和环岛检测的聚合结果
struct BEVElementEvidence {
    bool valid = false;                                           //!< 元素证据是否有效
    std::array<BEVRowProfileEvidence, kBevTrackSampleCount> row_profiles{}; //!< 所有层的行剖面证据
    BEVCrossBandEvidence cross_band{};       //!< 十字路口带状检测结果
    BEVCircleCornerEvidence left_circle_corner{};  //!< 左环岛转角检测结果
    BEVCircleCornerEvidence right_circle_corner{}; //!< 右环岛转角检测结果
    BEVCircleInnerIslandEvidence left_inner_island{};  //!< 左环岛内圆黑区证据
    BEVCircleInnerIslandEvidence right_inner_island{}; //!< 右环岛内圆黑区证据
    float invalid_edge_penalty = 0.0F;       //!< 无效边缘的整体惩罚因子
};

// 拓扑证据 —— 综合评分，描述前方道路拓扑结构
// 这是场景 FSM 和参考路径策略的输入依据
struct TopologyEvidence {
    float ordinary_score = 0.0F;         //!< 普通直行/跟线场景评分
    float bend_curvature_abs = 0.0F;     //!< 弯道曲率绝对值（用于弯道识别）
    float bend_veto_score = 0.0F;        //!< 弯道否决分（否决不当弯道检测）
    float cross_score = 0.0F;            //!< 十字路口存在评分
    float left_circle_score = 0.0F;      //!< 左侧环岛存在评分
    float right_circle_score = 0.0F;     //!< 右侧环岛存在评分
    float zebra_score = 0.0F;            //!< 斑马线存在评分
    float lost_score = 0.0F;             //!< 丢失感知评分（路径完全丢失的程度）
    float bilateral_opening_sync = 0.0F; //!< 双侧开口同步度（左右同时开阔→路口可能性↑）
    float forward_reacquire_score = 0.0F; //!< 前向重获取评分（丢失后重新获取路径的程度）
    float left_opening_score = 0.0F;     //!< 左侧开口程度
    float right_opening_score = 0.0F;    //!< 右侧开口程度
    float invalid_edge_penalty = 0.0F;   //!< 无效边缘惩罚
    BEVElementEvidence element_evidence{}; //!< 底层元素证据（cross band / circle corner）
};

// BEV 投影器标定参数 —— 图像↔BEV 坐标映射所需的 4 对对应点
struct BEVProjectorCalibration {
    bool valid = true;   //!< 标定是否有效
    // 源图像点位（图像坐标系下 4 个角点）
    std::array<ImagePoint, kBevCalibrationPointCount> source_points{
        {ImagePoint{220.0F, 19.0F},
         ImagePoint{220.0F, 305.0F},
         ImagePoint{68.0F, 108.0F},
         ImagePoint{68.0F, 220.0F}}};
    // BEV 对应目标点位（车辆坐标系下 4 个角点）
    std::array<BEVPoint, kBevCalibrationPointCount> target_points{
        {BEVPoint{0.061F, -0.21F},
         BEVPoint{0.061F, 0.21F},
         BEVPoint{0.610F, -0.21F},
         BEVPoint{0.610F, 0.21F}}};
    int debug_grid_width = 160;    //!< 调试网格宽度
    int debug_grid_height = 128;   //!< 调试网格高度
    std::string projector_id = "bev_projector_true_bev_manual_forward_scale_v5";  //!< 投影器标识
    std::string projector_hash = "bev-projector-true-bev-manual-forward-scale-20260428"; //!< 投影器哈希（用于版本追踪）
};

// BEV 几何参数 —— 采样网格范围、车道宽度等几何参数
struct BEVGeometryParameters {
    // 24 层的前向采样距离分布（米），从近到远
    // 近端密集（0.06-0.37m 间距 ~0.06m），远端稀疏（1.06-1.50m 间距 ~0.06m）
    std::array<float, kBevTrackSampleCount> forward_samples_m{
        {0.061000F,
         0.123565F,
         0.186130F,
         0.248696F,
         0.311261F,
         0.373826F,
         0.436391F,
         0.498957F,
         0.561522F,
         0.624087F,
         0.686652F,
         0.749217F,
         0.811783F,
         0.874348F,
         0.936913F,
         0.999478F,
         1.062043F,
         1.124609F,
         1.187174F,
         1.249739F,
         1.312304F,
         1.374870F,
         1.437435F,
         1.500000F}};
    float search_lateral_limit_m = 0.65F;   //!< 横向搜索范围极限（米）
    float lateral_step_m = 0.02F;           //!< 横向采样步长（米）
    float nominal_lane_width_m = 0.42F;     //!< 名义车道宽度（米）
    float min_lane_width_m = 0.20F;         //!< 最小有效车道宽度
    float max_lane_width_m = 0.75F;         //!< 最大有效车道宽度
    float min_visible_range_m = 0.110522F;  //!< 最小可见范围（低于此值视为跟踪丢失）
    float min_track_confidence = 0.25F;     //!< 最小跟踪置信度阈值
    float continuity_break_threshold_m = 0.28F; //!< 连续性断裂阈值（前后帧差异超过此值认为断裂）
    int sample_row_step_px = 4;             //!< 稠密采样的行步长（像素）
    int image_border_truncation_margin_px = 2; //!< 图像边界截断安全边距（像素）
};

// 场景 FSM 参数 —— 特殊场景状态机的阈值和计数参数
struct BEVSceneFsmParameters {
    float bend_severity_confirm = 0.20F;     //!< 弯道严重度确认阈值
    float cross_expand_ratio_min = 1.18F;    //!< 十字路口宽度扩张比最小值
    float cross_bilateral_open_min_m = 0.12F; //!< 十字路口双侧开口最小宽度
    int cross_confirm_cycles = 2;            //!< 十字路口确认所需的连续帧数
    int cross_hold_cycles = 2;               //!< 十字路口保持帧数
    float zebra_transition_density_min = 7.0F; //!< 斑马线过渡密度最小值
    int zebra_hold_cycles = 2;               //!< 斑马线保持帧数
    float circle_open_score_min = 0.18F;     //!< 环岛开口评分最小值
    float circle_contract_score_min = 0.12F; //!< 环岛收缩评分最小值
    float circle_opposite_heading_abs_max = 0.05F; //!< 环岛对向航向角最大绝对值
    int circle_confirm_cycles = 2;           //!< 环岛确认所需连续帧数
    int circle_release_cycles = 3;           //!< 环岛释放所需连续帧数
    float release_track_confidence_min = 0.55F; //!< 释放时跟踪置信度最低要求
};

// 控制误差模型参数 —— 从参考路径到控制指令的转换参数
struct BEVControlModelParameters {
    int near_sample_index = 0;           //!< 近端采样索引（用于近端横向误差）
    int far_sample_index = 2;            //!< 远端采样索引（用于远端航向误差）
    int curvature_sample_index = 3;      //!< 曲率采样索引
    double lookahead_visible_range_ratio = 0.35; //!< 前视距离=可见范围×此比例
    double lookahead_min_m = 0.160043;   //!< 前视距离最小值（米）
    double lookahead_max_m = 0.259087;   //!< 前视距离最大值（米）
    double pure_pursuit_gain = 1.0;      //!< 纯追踪增益
    double heading_curvature_gain = 0.35; //!< 航向曲率增益
    double curvature_feedforward_gain = 0.20; //!< 曲率前馈增益
    double curvature_command_limit = 0.12; //!< 曲率指令限幅
    double curvature_to_w_target_gain = 12000.0; //!< 曲率→角速度目标增益
    double low_confidence_threshold = 0.35; //!< 低置信度阈值
    double steering_suppression_confidence = 0.12; //!< 转向抑制置信度阈值
    double low_visible_range_m = 0.108444; //!< 低可见范围阈值（米）
    double min_gain_scale = 0.25;        //!< 最小增益缩放比例
    double min_speed_limit_scale = 0.35; //!< 最小速度限制缩放比例
    double max_reference_bias_m = 0.20;  //!< 参考路径最大偏差（米）
};

// 拓扑采样器参数 —— BEV 稀疏网格采样的范围和分类阈值
struct BEVTopologySamplerParameters {
    // 与 BEVGeometryParameters 中的 forward_samples_m 一致
    std::array<float, kBevTrackSampleCount> forward_samples_m{
        {0.061000F,
         0.123565F,
         0.186130F,
         0.248696F,
         0.311261F,
         0.373826F,
         0.436391F,
         0.498957F,
         0.561522F,
         0.624087F,
         0.686652F,
         0.749217F,
         0.811783F,
         0.874348F,
         0.936913F,
         0.999478F,
         1.062043F,
         1.124609F,
         1.187174F,
         1.249739F,
         1.312304F,
         1.374870F,
         1.437435F,
         1.500000F}};
    float lateral_min_m = -0.80F;   //!< 横向采样范围最小值（左，米）
    float lateral_max_m = 0.80F;    //!< 横向采样范围最大值（右，米）
    float lateral_step_m = 0.02F;   //!< 横向采样步长（米）
    int sample_patch_radius_px = 1; //!< 采样 patch 半径（像素），用于计算局部特征
    float drivable_confidence_min = 0.55F; //!< 可行驶分类的最低置信度
    float unknown_confidence_min = 0.25F; //!< 未知分类的最低置信度（低于此值视为无效）
};

// 走廊图参数 —— 走廊间隔连接和路径评分的配置
struct BEVCorridorGraphParameters {
    float nominal_lane_width_m = 0.42F;   //!< 名义车道宽度（米）
    float min_interval_width_m = 0.16F;   //!< 最小有效间隔宽度
    float max_interval_width_m = 1.20F;   //!< 最大有效间隔宽度
    float max_center_jump_m = 0.28F;      //!< 相邻层中心点最大允许跳变
    float max_width_change_m = 0.35F;     //!< 相邻层最大宽度变化
    float max_curvature_abs = 0.90F;      //!< 路径曲率最大值（用于过滤噪声路径）
    float prior_carry_confidence_scale = 0.25F; //!< 先验帧携带置信度缩放
};

// 拓扑证据参数 —— 场景进入/释放的评分阈值和衰减系数
struct BEVTopologyEvidenceParameters {
    float cross_enter_score = 0.70F;      //!< 十字路口进入评分阈值
    float cross_release_score = 0.45F;    //!< 十字路口释放评分阈值
    float circle_enter_score = 1.0F;      //!< 环岛进入评分阈值
    float circle_release_score = 0.45F;   //!< 环岛释放评分阈值
    float zebra_enter_score = 1.0F;       //!< 斑马线进入评分阈值
    float zebra_release_score = 0.45F;    //!< 斑马线释放评分阈值
    float ordinary_release_score = 0.75F; //!< 普通场景释放评分阈值
    float evidence_decay = 0.65F;         //!< 证据衰减因子（时序平滑用，低通滤波系数）
};

// 参考路径策略参数 —— 路径选择和保持的参数配置
struct BEVReferencePolicyParameters {
    int hold_last_max_cycles = 8;         //!< 保持上次路径的最大周期数
    int blend_min_cycles = 3;             //!< 混合过渡所需的最小周期数
    float arc_follow_confidence_min = 0.55F; //!< 弧线跟随模式的最低置信度
    float stable_boundary_confidence_min = 0.55F; //!< 稳定边界偏移模式的最低置信度
};

// 路径策略参数 —— 特殊场景下的路径构造参数
struct BEVPathPolicyParameters {
    int cross_exit_min_layers = 3;            //!< 十字路口出口最少连续有效层数
    float cross_exit_after_band_min_m = 0.08F; //!< 十字路口过带后最小距离
    float cross_exit_heading_abs_max_rad = 0.35F; //!< 十字路口出口航向最大偏角（弧度）
    int circle_inner_min_layers = 3;          //!< 环岛内侧路径最少连续有效层数
    float circle_tangent_parallel_abs_max_rad = 0.35F; //!< 环岛切线平行最大偏角（弧度）
    float circle_exit_yaw_deg = 330.0F;       //!< 环岛出口偏航角度（度）
    int reference_blend_cycles = 3;           //!< 参考路径混合周期数
    float trusted_reference_decay = 0.85F;    //!< 可信参考衰减系数
    float reference_compatibility_tau_m = 0.40F; //!< 参考兼容性 tau（米）
    float reference_compatibility_max_error_m = 0.18F; //!< 参考兼容性最大容许误差
};

// BEV 轨迹估计 —— 走廊图的最终输出，描述车道边界和中心线
struct BEVTrackEstimate {
    bool valid = false;                       //!< 轨迹估计是否有效
    bool calibration_valid = false;           //!< 标定是否有效
    bool continuity_valid = false;            //!< 连续帧之间是否连续
    std::array<BEVPathSample, kBevTrackSampleCount> sampled_left_boundary{};   //!< 左边界采样点
    std::array<BEVPathSample, kBevTrackSampleCount> sampled_centerline{};      //!< 中心线采样点
    std::array<BEVPathSample, kBevTrackSampleCount> sampled_right_boundary{};  //!< 右边界采样点
    std::array<BEVPathSample, kBevTrackSampleCount> sampled_drivable_left_boundary{};   //!< 可行驶区域左边界
    std::array<BEVPathSample, kBevTrackSampleCount> sampled_drivable_right_boundary{};  //!< 可行驶区域右边界
    std::array<float, kBevTrackSampleCount> lane_width_profile_m{};    //!< 各层车道宽度分布
    std::array<float, kBevTrackSampleCount> drivable_width_profile_m{}; //!< 各层可行驶宽度分布
    float visible_range_m = 0.0F;         //!< 可见范围（米）
    float track_confidence = 0.0F;        //!< 跟踪置信度（0-1）
    float near_lateral_error = 0.0F;      //!< 近端横向误差（用于控制）
    float far_heading_error = 0.0F;       //!< 远端航向误差
    float preview_curvature = 0.0F;       //!< 预览曲率
    std::string source = "bev_sparse_sampler"; //!< 轨道来源标识
    std::string fallback_mode = "none";    //!< 回退模式（当轨迹无效时的策略）
};

// 车辆上下文 —— 当前帧的车辆状态和传感数据
struct VehicleContext {
    bool low_voltage_emergency = false; //!< 低电压紧急状态标志
    bool imu_valid = false;             //!< IMU 数据是否有效
    float gyro_z = 0.0F;               //!< Z 轴陀螺仪角速度（原始值）
    float yaw_rate_deg_s = 0.0F;       //!< 偏航角速度（度/秒）
    float speed_mps = 0.0F;            //!< 车速（米/秒）
    float encoder_mean = 0.0F;         //!< 编码器均值
    int drive_cycle_count = 0;          //!< 驱动循环计数
    uint64_t frame_id = 0;             //!< 关联的帧 ID
    uint64_t capture_time_ms = 0;      //!< 采集时间戳
};

// 拓扑证据累加器 —— 多帧时间平滑的拓扑证据
struct TopologyEvidenceAccumulator {
    TopologyEvidence value{};  //!< 累加后的拓扑证据
    int update_cycles = 0;     //!< 已更新周期数（用于权重调整）
};

// 控制约束集 —— 定义转向/速度的约束和缩放因子
struct ControlConstraintSet {
    bool steering_suppressed = false;    //!< 转向被抑制（禁止转向输出）
    bool fail_safe_veto = false;         //!< 安全否决（紧急制动）
    bool low_confidence_degraded = false; //!< 低置信度降级模式
    double steering_gain_scale = 1.0;    //!< 转向增益缩放系数
    double speed_limit_scale = 1.0;      //!< 速度限制缩放系数
    double turn_limit_scale = 1.0;       //!< 转弯限制缩放系数
    std::string primary_reason = "none"; //!< 约束主要原因描述
};

// 控制误差模型输出 —— 从参考路径计算得到的转向控制指令
struct ControlErrorModelOutput {
    bool valid = false;                //!< 输出是否有效
    float near_lateral_error = 0.0F;  //!< 近端横向误差（米）
    float far_heading_error = 0.0F;   //!< 远端航向误差（弧度）
    float preview_curvature = 0.0F;   //!< 预览曲率（路径弯曲程度）
    float raw_near_lateral_error = 0.0F; //!< 未经 trusted-error 保护的近端横向误差
    float raw_far_heading_error = 0.0F;  //!< 未经 trusted-error 保护的远端航向误差
    float raw_preview_curvature = 0.0F;  //!< 未经 trusted-error 保护的预览曲率
    float visible_range_m = 0.0F;     //!< 可见范围（米）
    float track_confidence = 0.0F;    //!< 跟踪置信度（0-1）
    float lookahead_distance_m = 0.0F; //!< 前视距离（米）
    float lookahead_lateral_error = 0.0F;  //!< 前视横向误差
    float lookahead_heading_error = 0.0F;  //!< 前视航向误差
    float reference_curvature = 0.0F;      //!< 参考曲率
    float raw_lookahead_lateral_error = 0.0F; //!< 未经 trusted-error 保护的前视横向误差
    float raw_lookahead_heading_error = 0.0F; //!< 未经 trusted-error 保护的前视航向误差
    float raw_reference_curvature = 0.0F;     //!< 未经 trusted-error 保护的参考曲率
    bool trusted_error_active = false;        //!< trusted path 是否参与 error 保护
    float trusted_error_weight_near = 0.0F;   //!< near error 的 trusted 权重
    float trusted_error_weight_far = 0.0F;    //!< far heading 的 trusted 权重
    float trusted_error_weight_lookahead = 0.0F; //!< lookahead error 的 trusted 权重
    float trusted_error_weight_curvature = 0.0F; //!< curvature error 的 trusted 权重
    float curvature_command = 0.0F;        //!< 曲率控制指令
    float yaw_rate_target = 0.0F;          //!< 偏航角速度目标值
    double steering_gain_scale = 1.0;      //!< 转向增益缩放
    double speed_limit_scale = 1.0;        //!< 速度限制缩放
    double turn_limit_scale = 1.0;         //!< 转弯限制缩放
    bool steering_suppressed = false;      //!< 转向被抑制
    bool degraded = false;                 //!< 降级模式标志
    std::string degrade_reason = "none";   //!< 降级原因描述
};

// 参考路径 —— 参考策略最终选定的控制路径
struct BEVReferencePath {
    bool valid = false;                                           //!< 参考路径是否有效
    ReferenceMode mode = ReferenceMode::kCenterline;              //!< 路径生成模式
    float bias_m = 0.0F;                                          //!< 相对于中心线的横向偏置（米）
    std::array<BEVPathSample, kBevTrackSampleCount> sampled_path{}; //!< 离散路径采样点
};

// 控制误差模型输入 —— 将 track、参考路径、车辆状态打包传递给误差模型
struct ControlErrorModelInput {
    BEVTrackEstimate track{};            //!< 原始轨迹估计
    BEVReferencePath reference_path{};   //!< 参考路径（已选择的控制路径）
    BEVReferencePath trusted_error_reference{}; //!< 只用于 error 保护的历史参考路径
    float trusted_error_confidence = 0.0F; //!< trusted_error_reference 的置信度权重
    VehicleContext vehicle{};            //!< 车辆状态
    ControlConstraintSet constraints{};   //!< 控制约束
};

// BEV 场景观测 —— 从拓扑证据和走廊图中提取的场景特征
// 这是场景 FSM 的输入：包含场景候选标志和各种副度量
struct BEVSceneObservation {
    bool valid = false;                   //!< 观测是否有效
    BEVTrackEstimate track{};             //!< 当前轨迹估计
    VehicleContext vehicle{};             //!< 车辆状态
    bool ordinary_bend_veto = false;      //!< 普通弯道否决（排除误检）
    bool cross_candidate = false;         //!< 十字路口候选标志
    bool zebra_candidate = false;         //!< 斑马线候选标志
    bool circle_left_candidate = false;   //!< 左环岛候选标志
    bool circle_right_candidate = false;  //!< 右环岛候选标志
    float bend_severity = 0.0F;           //!< 弯道严重度
    float width_expand_ratio = 1.0F;      //!< 宽度扩张比（路口处车道突然变宽）
    float bottom_transition_density = 0.0F; //!< 底部过渡密度
    float left_open_score = 0.0F;         //!< 左侧开口评分
    float right_open_score = 0.0F;        //!< 右侧开口评分
    float left_contract_score = 0.0F;     //!< 左侧收缩评分
    float right_contract_score = 0.0F;    //!< 右侧收缩评分
    float cross_bilateral_open_score_m = 0.0F; //!< 十字路口双侧开口宽度（米）
    bool cross_bilateral_open = false;    //!< 双侧是否均开放（十字路口特征）
    float left_boundary_heading_abs_rad = 0.0F; //!< 左边界航向角绝对值（弧度）
    float right_boundary_heading_abs_rad = 0.0F; //!< 右边界航向角绝对值（弧度）
    bool circle_left_opposite_straight = false; //!< 左环岛对向是否有直行区域
    bool circle_right_opposite_straight = false; //!< 右环岛对向是否有直行区域
    float left_opposite_straight_confidence = 0.0F; //!< 左对向直行置信度
    float right_opposite_straight_confidence = 0.0F; //!< 右对向直行置信度
};

// BEV 轨迹记忆 —— 跨帧携带的上次有效轨迹信息
struct BEVTrackMemory {
    bool has_previous_track = false; //!< 是否有有效的先前轨迹
    BEVTrackEstimate previous_track{}; //!< 上一帧的有效轨迹
    int carry_cycles = 0;            //!< 已携带的帧数（用于衰减）
};

// 特殊场景 FSM 状态 —— 场景状态机的完整状态
struct SpecialSceneFsmState {
    SpecialSceneKind active_scene = SpecialSceneKind::kOrdinary;  //!< 当前激活的场景
    SpecialSceneKind candidate_scene = SpecialSceneKind::kOrdinary; //!< 候选中的场景
    SpecialScenePhase phase = SpecialScenePhase::kIdle;  //!< 当前阶段
    int candidate_streak = 0;       //!< 候选连续帧计数（用于确认）
    int progress_cycles = 0;        //!< 进度周期计数
    int release_cycles = 0;         //!< 释放周期计数
    bool latched = false;            //!< 是否已锁定（防止状态抖动）
    bool circle_entry_signal_active = false; //!< 环岛入口信号激活
    bool circle_candidate_anchor_valid = false; //!< 环岛候选锚点有效
    float circle_candidate_forward_m = 0.0F;   //!< 环岛候选前向位置（米）
    float circle_candidate_lateral_m = 0.0F;   //!< 环岛候选横向位置（米）
    float circle_heading_delta_deg = 0.0F;     //!< 环岛航向增量（度）
    float circle_yaw_accum_deg = 0.0F;         //!< 环岛偏航累计（度）
    uint64_t circle_yaw_last_time_ms = 0;      //!< 环岛偏航上次更新时间戳
    std::string circle_path_phase = "none";    //!< 环岛路径阶段描述
    std::string circle_direction = "none";     //!< 环岛方向（left/right）
    std::string debug_candidate = "none";      //!< 调试用候选场景名
    float debug_candidate_score = 0.0F;        //!< 调试用候选评分
};

// 参考路径策略状态 —— 参考选择器的跨帧历史状态
struct ReferencePolicyState {
    bool valid = false;                //!< 状态是否有效
    ReferenceMode mode = ReferenceMode::kCenterline; //!< 当前参考模式
    float carry_bias_m = 0.0F;        //!< 跨帧携带的横向偏置
    float trusted_confidence = 0.0F;  //!< 信任的置信度
    int trusted_age_cycles = 0;        //!< 信任状态的龄期（周期数）
    float compatibility_error_m = 0.0F; //!< 参考兼容性误差（米）
    std::string reference_source = "none"; //!< 参考路径来源描述
    int hold_cycles = 0;               //!< 保持模式已持续的周期数
    int lost_prediction_cycles = 0;    //!< 丢失预测已持续的周期数
    std::string circle_reference_phase = "none"; //!< reference policy 实际环岛路径阶段
    bool circle_inner_latched = false;  //!< 是否已锁存内圆跟随
    bool circle_exit_latched = false;   //!< 是否已锁存出环路径
    bool circle_inner_island_memory_active = false; //!< 是否已有标定内圆记忆
    bool circle_inner_island_memory_left = true; //!< 内圆记忆方向，true=左环岛
    int circle_inner_island_memory_age_cycles = 0; //!< 内圆记忆龄期
    int circle_inner_island_missing_edge_cycles = 0; //!< 连续缺失兼容 edge 的周期数
    float circle_inner_island_memory_confidence = 0.0F; //!< 内圆记忆置信度
    float circle_inner_island_scan_lateral_m = 0.0F; //!< 内圆标定竖线横向位置
    float circle_inner_island_black_start_forward_m = 0.0F; //!< 内圆黑区起点
    float circle_inner_island_black_end_forward_m = 0.0F; //!< 内圆黑区终点
    std::array<BEVPathSample, kBevTrackSampleCount> circle_inner_island_edge{}; //!< 已记忆的内圆边缘
    std::array<BEVPathSample, kBevTrackSampleCount> last_reference{}; //!< 最后有效参考路径
};

// 感知结果 —— `AnalyzeFrame()` 的主输出，包含整个感知管线的完整结果
struct PerceptionResult {
    bool published = false;              //!< 感知结果是否已发布
    bool fresh = false;                  //!< 是否是新帧的结果（非重发布）
    bool emergency_veto = true;          //!< 紧急否决（默认 true，安全优先）
    bool low_voltage_veto = false;       //!< 低电压否决
    bool threshold_veto = false;         //!< 阈值无效否决
    bool geometry_veto = false;          //!< 几何无效否决
    float lateral_error = 0.0F;          //!< 横向误差（兼容旧版）
    float heading_error = 0.0F;          //!< 航向误差（兼容旧版）
    float curvature = 0.0F;              //!< 曲率（兼容旧版）
    float track_confidence = 0.0F;       //!< 跟踪置信度
    int threshold = 0;                   //!< 当前使用的二值化阈值
    bool track_valid = false;            //!< 跟踪是否有效
    float gyro_heading_delta_deg = 0.0F; //!< 陀螺仪航向增量（度）
    float gyro_consistency_score = 1.0F; //!< 陀螺仪一致性评分
    bool sign_flip_blocked = false;      //!< 符号翻转被阻止
    bool imu_grace_active = false;       //!< IMU 宽限期激活
    bool roadblock_active = false;       //!< 路障激活标志
    uint64_t frame_id = 0;              //!< 帧序号
    uint64_t capture_time_ms = 0;       //!< 采集时间戳
    uint64_t publish_time_ms = 0;       //!< 发布时间戳
    std::string active_module = "straight";  //!< 当前激活模块名
    std::string scene_phase = "idle";        //!< 场景阶段
    std::string scene_override_source = "none"; //!< 场景覆盖来源
    std::string roadblock_interface_state = "supported_not_implemented"; //!< 路障接口状态
    std::string perception_tag = "none";     //!< 感知标记（调试用）
    std::string circle_direction = "none";   //!< 环岛方向
    std::string circle_reference_mode = "none"; //!< 环岛参考模式
    float circle_heading_delta_deg = 0.0F;  //!< 环岛航向增量
    float circle_yaw_accum_deg = 0.0F;      //!< 环岛偏航累计
    std::string circle_path_phase = "none";  //!< 环岛路径阶段
    float reference_compatibility_error_m = 0.0F; //!< 参考兼容性误差
    std::string reference_source = "none";   //!< 参考路径来源
    bool circle_entry_signal_active = false; //!< 环岛入口信号激活
    bool inner_island_memory_active = false; //!< 内圆黑区记忆是否激活
    int inner_island_memory_age = 0;         //!< 内圆黑区记忆龄期
    float inner_island_memory_confidence = 0.0F; //!< 内圆黑区记忆置信度
    bool left_inner_island_present = false;  //!< 当前帧左内圆白黑白证据
    bool right_inner_island_present = false; //!< 当前帧右内圆白黑白证据
    bool inner_edge_compatible = false;      //!< 当前局部内圆 edge 是否兼容记忆
    bool inner_island_trace_present = false; //!< 当前帧内圆连续 trace 是否存在
    float inner_island_trace_start_forward_m = 0.0F; //!< 当前内圆 trace 起点
    float inner_island_trace_end_forward_m = 0.0F; //!< 当前内圆 trace 终点
    float inner_island_trace_confidence = 0.0F; //!< 当前内圆 trace 置信度
    int inner_island_trace_support_layers = 0; //!< 当前内圆 trace 观测层数
    int inner_island_trace_gap_layers = 0; //!< 当前内圆 trace 插值缺口层数
    int inner_island_rejected_far_segments = 0; //!< 当前内圆 trace 拒绝远端段数
    float near_lateral_error = 0.0F;         //!< 近端横向误差
    float far_heading_error = 0.0F;          //!< 远端航向误差
    float preview_curvature = 0.0F;          //!< 预览曲率
    float visible_range_m = 0.0F;            //!< 可见范围
    std::string reference_mode = "centerline"; //!< 参考模式描述
    BEVTrackEstimate bev_track{};            //!< BEV 轨迹估计
    RoadHypotheses road_hypotheses{};        //!< 道路假设集合
    TopologyEvidence topology_evidence{};     //!< 拓扑证据
    BEVSceneObservation scene_observation{}; //!< 场景观测
    ControlConstraintSet control_constraints{}; //!< 控制约束
    ControlErrorModelOutput control_model{};  //!< 控制误差模型输出
};

// BEV 控制器记忆 —— PID 控制器的跨帧状态
struct BEVControllerMemory {
    float w_target_last = 0.0F;      //!< 上次角速度目标值
    float camera_error_last = 0.0F;  //!< 上次摄像头误差
    float gyro_error_last = 0.0F;    //!< 上次陀螺仪误差
    float gyro_i_accumulator = 0.0F; //!< 陀螺仪积分项累加器
    float last_gain_scale = 1.0F;    //!< 上次增益缩放系数
};

using LegacySteeringControllerMemory = BEVControllerMemory;

// 车道几何锚点数（旧版跟线使用，每侧 3 个锚点描述车道边界）
constexpr int kLaneGeometryAnchorCount = 3;

// 车道历史锚点 —— 旧版图像空间跟线的有限记忆
struct LaneHistoryAnchor {
    bool valid = false;                      //!< 锚点是否有效
    int row = 0;                             //!< 行坐标
    int col = kCompiledCameraFrameWidth / 2; //!< 列坐标（默认居中）
};

// 车道几何历史快照 —— 旧版图像空间车道边界的跨帧记忆
struct LaneGeometryHistorySnapshot {
    bool valid = false; //!< 快照是否有效
    std::array<LaneHistoryAnchor, kLaneGeometryAnchorCount> left_visible_anchors{};  //!< 左边界可见锚点
    std::array<LaneHistoryAnchor, kLaneGeometryAnchorCount> right_visible_anchors{}; //!< 右边界可见锚点
};

// 轨迹历史快照 —— 旧版图像空间轨迹的历史信息（用于跟线连续性）
struct TrackHistorySnapshot {
    bool valid = false; //!< 快照是否有效
    std::array<LaneHistoryAnchor, kLaneGeometryAnchorCount> center_anchors{}; //!< 中心线锚点
    float lane_width_px = 0.0F;         //!< 车道宽度（像素）
    float heading_px_per_row = 0.0F;    //!< 航向（每行的像素偏移）
    float curvature_px_per_row2 = 0.0F; //!< 曲率（每行^2的像素偏移）
    int turn_sign = 0;                  //!< 转向方向符号（-1左, 0直, 1右）
    int last_nonzero_turn_sign = 0;     //!< 上次非零转向符号
    int zero_turn_sign_frames = 0;      //!< 转向符号为 0 的连续帧数
    float track_confidence = 0.0F;      //!< 跟踪置信度
    int flip_candidate_sign = 0;        //!< 翻转候选符号
    int flip_candidate_frames = 0;      //!< 翻转候选持续帧数
};

// 陀螺仪连续性状态 —— IMU 数据连续性跟踪
struct GyroContinuityState {
    uint64_t last_valid_capture_time_ms = 0; //!< 上次有效 IMU 采集时间
    float filtered_yaw_rate = 0.0F;           //!< 滤波后的偏航角速度
    float heading_delta_deg_150ms = 0.0F;     //!< 150ms 窗口内的航向增量（度）
    bool imu_grace_active = false;            //!< IMU 宽限期是否激活（短暂丢失时允许继续使用）
};

// 旧版转向状态 —— 转向系统的完整跨帧状态
// 包括场景 FSM、参考策略、控制器记忆、轨迹历史等
struct LegacySteeringState {
    bool roadblock_active = false;                   //!< 路障激活
    int drive_cycle_count = 0;                        //!< 驱动循环总数
    std::string active_module = "straight";            //!< 当前激活模块
    std::string scene_phase = "idle";                  //!< 场景阶段
    std::string scene_override_source = "none";        //!< 场景覆盖来源
    std::string roadblock_interface_state = "supported_not_implemented"; //!< 路障接口状态
    std::string scene_debug_candidate = "none";        //!< 调试用候选场景
    int scene_debug_candidate_streak = 0;              //!< 调试候选连续帧数
    float scene_cross_candidate_score_last = 0.0F;     //!< 上次十字路口候选评分
    float scene_circle_left_candidate_score_last = 0.0F; //!< 上次左环岛候选评分
    float scene_circle_right_candidate_score_last = 0.0F; //!< 上次右环岛候选评分
    LaneGeometryHistorySnapshot lane_geometry_recent{};  //!< 最近车道几何快照
    LaneGeometryHistorySnapshot lane_geometry_previous{}; //!< 先前车道几何快照
    TrackHistorySnapshot track_history{};               //!< 轨迹历史
    GyroContinuityState gyro_continuity{};              //!< 陀螺仪连续性状态
    BEVTrackEstimate last_bev_track{};                  //!< 上次 BEV 轨迹估计
    BEVTrackMemory bev_track_memory{};                  //!< BEV 轨迹记忆
    SpecialSceneFsmState scene_fsm{};                   //!< 场景状态机状态
    ReferencePolicyState reference_policy{};            //!< 参考路径策略状态
    RoadHypotheses last_road_hypotheses{};              //!< 上次道路假设
    TopologyEvidence last_topology_evidence{};           //!< 上次拓扑证据
    TopologyEvidenceAccumulator topology_evidence_accumulator{}; //!< 拓扑证据累加器
    int lost_prediction_cycles = 0;                     //!< 丢失预测持续周期数
    LegacySteeringControllerMemory controller_memory{};  //!< 控制器记忆
};

// IMU 采样数据 —— 加速度计 + 陀螺仪的 6 轴数据
struct ImuSample {
    bool valid = false;              //!< 数据是否有效
    float acc_x = 0.0F;              //!< X 轴加速度
    float acc_y = 0.0F;              //!< Y 轴加速度
    float acc_z = 0.0F;              //!< Z 轴加速度
    float gyro_x = 0.0F;             //!< X 轴角速度（陀螺仪）
    float gyro_y = 0.0F;             //!< Y 轴角速度
    float gyro_z = 0.0F;             //!< Z 轴角速度（偏航，用于转向控制）
    uint64_t capture_time_ms = 0;   //!< 采集时间戳
};

// 编码器增量 —— 左右轮编码器的脉冲增量
struct EncoderDelta {
    bool valid = false;              //!< 数据是否有效
    int left = 0;                    //!< 左轮编码器增量
    int right = 0;                   //!< 右轮编码器增量
    uint64_t capture_time_ms = 0;   //!< 采集时间戳
};

// 低电压采样 —— 电源电压监测结果
struct LowVoltageSample {
    bool valid = false;               //!< 采样是否有效
    bool emergency = true;            //!< 低电压紧急标志（默认安全）
    int raw_value = -1;               //!< 原始采样值
    int threshold = 0;                //!< 低电压阈值
    uint64_t capture_time_ms = 0;    //!< 采样时间戳
    std::string source = "unavailable"; //!< 采样来源
};

// 执行器指令 —— 最终输出的左右轮 PWM 值
struct ActuatorCommand {
    int left_pwm = 0;           //!< 左轮 PWM 值
    int right_pwm = 0;          //!< 右轮 PWM 值
    bool emergency_stop = true; //!< 紧急停止标志（默认 true，安全优先）
};

// 车轮 PID 参数 —— 单个轮子的 PID 控制器参数
struct WheelPidParameters {
    double p = 240.0;           //!< 比例增益
    double i = 10.0;            //!< 积分增益
    double d = 20.0;            //!< 微分增益
    double integral_limit = 2200.0; //!< 积分项限幅
    double measurement_filter_alpha = 0.3; //!< 测量值滤波系数（低通）
};

// 助理 TCP 参数 —— 外部调试/监控工具的 TCP 连接配置
struct AssistantTcpParameters {
    std::string host = "192.168.2.32"; //!< 主机地址
    int port = 8888;                   //!< 端口号
};

// 运行时参数 —— 系统所有可配置参数的统一容器
// 从 JSON 文件加载（default_params.json），可在运行时动态修改
struct RuntimeParameters {
    // --- 基础速度/转向控制 ---
    double Speed_base = 77.0;                      //!< 基础速度设定
    double see_max = 35.0;                         //!< 最大速度限制
    double pid_turn_camera_p = 14.75;              //!< 转角 PID 摄像头比例增益
    double pid_turn_camera_p_scale = 1.0;          //!< 转角摄像头 P 增益缩放
    double pid_turn_camera_d = 0.0;                //!< 转角 PID 摄像头微分增益
    double pid_turn_gyro_camera_p = 20.0;          //!< 转角 PID 陀螺仪+摄像头比例增益
    double pid_turn_gyro_camera_i = 0.0;           //!< 转角 PID 陀螺仪+摄像头积分增益
    double pid_turn_gyro_camera_d = 9.0;           //!< 转角 PID 陀螺仪+摄像头微分增益
    int P_Mode = 0;                                 //!< P 模式选择（兼容旧版）
    int exp_light = 65;                             //!< 曝光/光照补偿值

    // --- 运行时策略值 ---
    int emergency_threshold = 40;                   //!< 紧急阈值
    int control_period_ms = 5;                      //!< 控制周期（毫秒）
    int perception_stale_ms = 120;                  //!< 感知数据过时阈值（毫秒）
    int pwm_limit = 9000;                           //!< PWM 输出限幅
    int raw_turn_output_limit = 3000;               //!< 原始转向输出限幅
    int pwm_floor = 0;                              //!< PWM 最小值（死区补偿）
    bool prohibit_reverse_pwm = false;              //!< 是否禁止反向 PWM
    int prohibit_reverse_pwm_step_limit = 280;      //!< 禁止反向 PWM 的步进限制
    int motion_unveto_confirm_cycles = 3;           //!< 运动解除否决的确认周期数
    int motion_spinup_ms = 600;                     //!< 运动启动加速时间（毫秒）
    double motion_turn_limit_spinup = 0.35;         //!< 运动转弯限制加速因子
    int motion_pwm_step_limit = 280;                //!< 运动 PWM 步进限制
    int motion_stop_ms = 300;                       //!< 运动停止时间（毫秒）
    int motion_stop_encoder_threshold = 8;          //!< 运动停止编码器阈值
    int motion_fault_rearm_hold_ms = 600;           //!< 运动故障重新就绪保持时间
    double wheel_turn_target_scale = 35.0;          //!< 车轮转向目标缩放
    WheelPidParameters left_wheel_pid{};            //!< 左轮 PID 参数
    WheelPidParameters right_wheel_pid{};           //!< 右轮 PID 参数

    // --- 调试/监控发布 ---
    int control_snapshot_emit_interval_ms = 100;    //!< 控制快照发布间隔（毫秒）
    bool assistant_enabled = false;                  //!< 助理功能使能
    int assistant_waveform_publish_interval_ms = 40; //!< 助理波形发布间隔
    int assistant_image_publish_interval_ms = 80;   //!< 助理图像发布间隔
    AssistantTcpParameters assistant_tcp{};          //!< 助理 TCP 连接参数
    bool steering_media_enabled = false;             //!< 转向媒体服务使能
    int steering_media_port = 8890;                  //!< 转向媒体服务端口
    int steering_media_publish_interval_ms = 80;     //!< 转向媒体发布间隔

    // --- 感知/控制参数 ---
    bool pid_turn_camera_use_fuzzy = false;          //!< PID 转角是否使用模糊控制
    int camera_frame_width = 320;                    //!< 摄像头帧宽度
    int camera_frame_height = 240;                   //!< 摄像头帧高度
    BEVProjectorCalibration bev_projector{};         //!< BEV 投影器标定参数
    BEVGeometryParameters bev_geometry{};            //!< BEV 几何参数
    BEVSceneFsmParameters bev_scene_fsm{};           //!< 场景 FSM 参数
    BEVControlModelParameters bev_control_model{};   //!< 控制误差模型参数
    BEVTopologySamplerParameters bev_topology_sampler{}; //!< 拓扑采样器参数
    BEVCorridorGraphParameters bev_corridor_graph{}; //!< 走廊图参数
    BEVTopologyEvidenceParameters bev_topology_evidence{}; //!< 拓扑证据参数
    BEVReferencePolicyParameters bev_reference_policy{}; //!< 参考路径策略参数
    BEVPathPolicyParameters bev_path_policy{};       //!< 路径策略参数
    bool startup_critical_applied = false;            //!< 启动关键参数是否已应用
    bool loaded_from_defaults = false;                //!< 是否从默认值加载
    bool parse_failure = false;                       //!< JSON 解析是否失败
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_CONTROL_TYPES_HPP
