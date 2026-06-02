#ifndef LS2K_LEGACY_STEERING_CIRCLE_ELEMENT_EVIDENCE_HPP
#define LS2K_LEGACY_STEERING_CIRCLE_ELEMENT_EVIDENCE_HPP

#include <string>
#include <vector>

#include "vision/bev/bev_projector.hpp"
#include "vision/bev/bev_simple_perception.hpp"
#include "port/bev_element_raster_types.hpp"
#include "port/bev_geometry_types.hpp"
#include "port/camera_frame_types.hpp"
#include "port/runtime_parameter_types.hpp"
#include "port/visual_element_evidence_types.hpp"
#include "port/visual_reference_orchestration_types.hpp"

namespace ls2k::vision {

struct BEVElementRasterFrame;

/// 环形元素证据检测结果，包含左右两侧的 Phase1 原始方向证据
struct CircleElementEvidenceResult {
    port::VisualElementEvidenceRecord left_raw{};   ///< 左侧原始证据记录
    port::VisualElementEvidenceRecord right_raw{};  ///< 右侧原始证据记录
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

}  // namespace ls2k::vision

#endif  // LS2K_LEGACY_STEERING_CIRCLE_ELEMENT_EVIDENCE_HPP
