#ifndef LS2K_LEGACY_STEERING_CIRCLE_ELEMENT_EVIDENCE_HPP
#define LS2K_LEGACY_STEERING_CIRCLE_ELEMENT_EVIDENCE_HPP

#include <string>
#include <vector>

#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_bev_simple_perception.hpp"
#include "port/bev_element_raster_types.hpp"
#include "port/bev_geometry_types.hpp"
#include "port/camera_frame_types.hpp"
#include "port/runtime_parameter_types.hpp"
#include "port/visual_element_evidence_types.hpp"
#include "port/visual_reference_orchestration_types.hpp"

namespace ls2k::legacy {

struct BEVElementRasterFrame;

/// 环形交叉口入口路径事实，包含道路宽度、方向偏差和路径点信息
struct CircleEntryPathFacts {
    bool present = false;                               ///< 入口路径是否存在
    std::string reason = "not_evaluated";                ///< 状态原因描述
    float road_half_width_m = 0.0F;                     ///< 道路半宽（米）
    float direction_delta_lateral_m = 0.0F;             ///< 路径方向横向变化量（米）
    float direction_delta_forward_m = 0.0F;             ///< 路径方向前向变化量（米）
    std::vector<port::BEVPoint> near_centerline_points{}; ///< 近端中心线点集
    std::vector<port::BEVPoint> frontier_points{};       ///< 边界前沿点集
    std::vector<port::BEVPoint> centerline_points{};     ///< 中心线点集
};

/// 环形交叉口入口管线诊断信息
struct CircleEntryPipelineDiagnostics {
    CircleEntryPathFacts left{};   ///< 左侧入口路径事实
    CircleEntryPathFacts right{};  ///< 右侧入口路径事实
};

/// 环形元素证据检测结果，包含左右两侧的原始证据和入口路径事实
struct CircleElementEvidenceResult {
    port::VisualElementEvidenceRecord left_raw{};   ///< 左侧原始证据记录
    port::VisualElementEvidenceRecord right_raw{};  ///< 右侧原始证据记录
    CircleEntryPathFacts left_entry{};  ///< 左侧入口路径事实
    CircleEntryPathFacts right_entry{}; ///< 右侧入口路径事实
};

/// ROI metric 采样结果，语义等价于局部 BEV element cell fact
struct BEVMetricClassSample {
    port::BEVElementRasterProjectionState projection_state =
        port::BEVElementRasterProjectionState::kProjectionFailed; ///< 投影状态
    port::BEVElementRasterCellClass class_kind =
        port::BEVElementRasterCellClass::kInvalid; ///< 分类结果
};

/// 局部 BEV metric 采样器：只回答单个 metric point 的 sampleable/class fact
class BEVMetricClassSampler {
public:
    BEVMetricClassSampler(const port::LegacyCameraFrameView& frame,
                          int threshold,
                          const port::RuntimeParameters& params,
                          const BEVProjector& projector);

    /// 采样一个 BEV metric point，不保存全帧栅格
    BEVMetricClassSample Sample(const port::BEVPoint& point) const;

    /// 采样器上下文是否可用
    bool Valid() const;

private:
    const port::LegacyCameraFrameView& frame_;
    int threshold_ = 0;
    const port::RuntimeParameters& params_;
    const BEVProjector& projector_;
};

/// 检测环形交叉口 Phase1 证据（左右两侧的开口/弯曲分析）
/// @param rows sparse BEV row facts
/// @param params 运行时参数
/// @return 环形元素证据检测结果
CircleElementEvidenceResult DetectCircleElementEvidence(
    const std::vector<BEVSimpleRowScan>& rows,
    const port::RuntimeParameters& params);

/// Legacy/test compatibility overload. Runtime V1 should call the sparse rows overload.
CircleElementEvidenceResult DetectCircleElementEvidence(
    const BEVElementRasterFrame* raster,
    const port::RuntimeParameters& params);

/// 构建环形交叉口 Phase2 入口路径事实
CircleEntryPathFacts BuildCircleEntryPathFacts(const BEVMetricClassSampler& sampler,
                                               const std::vector<BEVSimpleRowScan>& rows,
                                               const port::RuntimeParameters& params,
                                               bool left_side);

/// 构建环形交叉口入口视觉参考候选
/// @param evidence 视觉元素证据记录
/// @param entry 入口路径事实
/// @param kind 参考候选类型（左侧或右侧入口）
/// @param params 运行时参数
/// @param summary [输出] 候选摘要信息
/// @return 构建的视觉参考候选
port::VisualReferenceCandidate BuildCircleEntryVisualReferenceCandidate(
    const port::VisualElementEvidenceRecord& evidence,
    const CircleEntryPathFacts& entry,
    port::VisualReferenceCandidateKind kind,
    const port::RuntimeParameters& params,
    port::VisualElementCandidateSummary& summary);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_CIRCLE_ELEMENT_EVIDENCE_HPP
