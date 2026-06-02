#ifndef LS2K_LEGACY_STEERING_VISUAL_ELEMENT_PIPELINE_HPP
#define LS2K_LEGACY_STEERING_VISUAL_ELEMENT_PIPELINE_HPP

#include <vector>

#include "legacy/steering_bev_element_raster.hpp"
#include "legacy/steering_bev_projector.hpp"
#include "legacy/steering_bev_simple_perception.hpp"
#include "port/camera_frame_types.hpp"
#include "port/runtime_parameter_types.hpp"
#include "port/visual_element_evidence_types.hpp"
#include "port/visual_reference_orchestration_types.hpp"

namespace ls2k::legacy {

/// 视觉元素管线输入，runtime V1 只接受稀疏行事实和按需 ROI 上下文。
struct VisualElementPipelineInput {
    const std::vector<BEVSimpleRowScan>* sparse_rows = nullptr;  ///< 稀疏行扫描结果指针（可为nullptr）
    const port::LegacyCameraFrameView* frame = nullptr;          ///< 当前灰度帧（Phase2 ROI按需采样）
    const BEVProjector* projector = nullptr;                     ///< BEV投影器（Phase2 ROI按需采样）
    BEVPixelClassificationModel classification_model{};          ///< 当前帧灰度分类模型（Phase2 ROI按需采样）
    port::VisualReferenceCandidate line_candidate{};             ///< 车道线参考候选
};

/// Raster 兼容入口输入，仅供 probe/legacy 测试显式调用，不进入 runtime 热路径。
struct VisualElementRasterCompatibilityInput {
    const std::vector<BEVSimpleRowScan>* sparse_rows = nullptr;  ///< 可选：用于兼容路径中的 cross 事实
    const BEVElementRasterFrame* element_raster = nullptr;       ///< 兼容 circle raster 事实
    port::VisualReferenceCandidate line_candidate{};             ///< 车道线参考候选
};

/// 视觉元素管线输出，包含证据帧、候选列表和环形入口诊断信息
struct VisualElementPipelineResult {
    port::VisualElementEvidenceFrame evidence{};                    ///< 元素证据帧
    std::vector<port::VisualReferenceCandidate> candidates{};       ///< 构建的视觉参考候选列表
};

/// 运行完整的视觉元素管线
/// 依次执行十字出口检测和环形入口检测，将所有候选加入结果列表
/// @param input 管线输入
/// @param params 运行时参数
/// @return 管线结果
VisualElementPipelineResult RunVisualElementPipeline(const VisualElementPipelineInput& input,
                                                     const port::RuntimeParameters& params);

/// 运行显式 raster 兼容视觉元素管线。
/// 普通 runtime 不应调用此入口；它保留给 scene probe 和旧测试覆盖 raster 兼容行为。
VisualElementPipelineResult RunVisualElementRasterCompatibilityPipeline(
    const VisualElementRasterCompatibilityInput& input,
    const port::RuntimeParameters& params);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_VISUAL_ELEMENT_PIPELINE_HPP
