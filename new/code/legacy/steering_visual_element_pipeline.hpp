#ifndef LS2K_LEGACY_STEERING_VISUAL_ELEMENT_PIPELINE_HPP
#define LS2K_LEGACY_STEERING_VISUAL_ELEMENT_PIPELINE_HPP

#include <vector>

#include "legacy/steering_bev_element_raster.hpp"
#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_bev_simple_perception.hpp"
#include "legacy/steering_circle_element_evidence.hpp"
#include "port/camera_frame_types.hpp"
#include "port/runtime_parameter_types.hpp"
#include "port/visual_element_evidence_types.hpp"
#include "port/visual_reference_orchestration_types.hpp"

namespace ls2k::legacy {

/// 视觉元素管线输入，包含稀疏行扫描、元素栅格和车道线候选
struct VisualElementPipelineInput {
    const std::vector<BEVSimpleRowScan>* sparse_rows = nullptr;  ///< 稀疏行扫描结果指针（可为nullptr）
    const BEVElementRasterFrame* element_raster = nullptr;       ///< legacy/probe兼容字段，V1 runtime不读取
    const port::LegacyCameraFrameView* frame = nullptr;          ///< 当前灰度帧（Phase2 ROI按需采样）
    const BEVProjector* projector = nullptr;                     ///< BEV投影器（Phase2 ROI按需采样）
    int threshold = 0;                                            ///< 当前帧Otsu阈值（Phase2 ROI按需采样）
    port::VisualReferenceCandidate line_candidate{};             ///< 车道线参考候选
};

/// 视觉元素管线输出，包含证据帧、候选列表和环形入口诊断信息
struct VisualElementPipelineResult {
    port::VisualElementEvidenceFrame evidence{};                    ///< 元素证据帧
    std::vector<port::VisualReferenceCandidate> candidates{};       ///< 构建的视觉参考候选列表
    CircleEntryPipelineDiagnostics circle_entry_diagnostics{};      ///< 环形入口管线诊断信息
};

/// 运行完整的视觉元素管线
/// 依次执行十字出口检测和环形入口检测，将所有候选加入结果列表
/// @param input 管线输入
/// @param params 运行时参数
/// @return 管线结果
VisualElementPipelineResult RunVisualElementPipeline(const VisualElementPipelineInput& input,
                                                     const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_VISUAL_ELEMENT_PIPELINE_HPP
