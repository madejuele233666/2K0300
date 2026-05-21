#include "runtime/steering_frame_perception_pipeline.hpp"

#include <vector>

#include "legacy/steering_otsu_threshold.hpp"
#include "legacy/steering_reference_control_readiness.hpp"
#include "legacy/steering_reference_lateral_error.hpp"
#include "legacy/steering_reference_usability.hpp"
#include "legacy/steering_visual_element_pipeline.hpp"
#include "legacy/steering_visual_reference_orchestration.hpp"
#include "port/perf_counter.hpp"
#include "runtime/runtime_state.hpp"

namespace ls2k::runtime {
namespace {

/// 构建感知结果结构：从各感知子阶段的结果组装最终的 PerceptionResult
/// @param capture              相机捕获数据
/// @param threshold             Otsu 二值化阈值
/// @param health                感知健康状态
/// @param element_evidence      视觉元素证据帧
/// @param visual_selection      视觉参考选择结果
/// @param continuity            参考连续性结果
/// @param selected_usability    选中参考的可用性
/// @param lateral_error         参考横向误差估计
/// @param reference_control     参考控制就绪状态
/// @param publish_time_ms       发布时间戳
/// @return                      组装好的 PerceptionResult
port::PerceptionResult BuildPerceptionResult(
    const port::CameraCapture& capture,
    int threshold,
    const port::PerceptionHealth& health,
    const port::VisualElementEvidenceFrame& element_evidence,
    const port::VisualReferenceSelection& visual_selection,
    const port::ReferenceContinuityResult& continuity,
    const port::ReferenceUsability& selected_usability,
    const port::ReferenceLateralErrorEstimate& lateral_error,
    const port::ReferenceControlReadiness& reference_control,
    uint64_t publish_time_ms) {
    LS2K_PERF_SCOPE(port::PerfStage::kPerceptionResultBuild);
    port::PerceptionResult perception{};
    perception.published = true;
    perception.fresh = true;
    perception.frame_id = capture.frame_id;
    perception.capture_time_ms = capture.capture_time_ms;
    perception.publish_time_ms = publish_time_ms;
    perception.threshold = threshold;
    perception.perception_tag = "bev_simple";
    perception.reference_mode = legacy::ToString(continuity.mode);
    perception.reference_source = continuity.source;
    perception.reference_capture_time_ms = continuity.reference_capture_time_ms;
    perception.reference_path = continuity.reference_path;
    perception.perception_health = health;
    perception.element_evidence = element_evidence;
    perception.visual_reference_selection = visual_selection;
    perception.reference_usability = selected_usability;
    perception.reference_lateral_error = lateral_error;
    perception.reference_control = reference_control;
    return perception;
}

}  // namespace

/// 配置感知管线：初始化 BEV 投影器、重置采样 LUT
/// @param params       运行时参数
/// @param diagnostics  诊断输出接口
/// @return             投影器是否配置成功
bool SteeringFramePerceptionPipeline::Configure(const port::RuntimeParameters& params,
                                                port::DiagnosticSink& diagnostics) {
    projector_configured_ = projector_.Configure(params.bev_projector);
    sample_lut_ = {};
    diagnostics.Emit({projector_configured_ ? port::DiagnosticLevel::kInfo
                                            : port::DiagnosticLevel::kFailSafe,
                      projector_configured_ ? "perception.projector.configured"
                                            : "perception.projector.invalid",
                      projector_configured_ ? "BEV projector configured once for runtime perception"
                                            : "BEV projector configuration failed; perception will publish fail-safe fallback",
                      port::NowMs()});
    return projector_configured_;
}

/// 重置感知记忆（清空参考连续性跟踪状态）
void SteeringFramePerceptionPipeline::ResetMemory() {
    ResetSteeringPerceptionMemory(perception_memory_);
}

/// 处理一帧图像：Otsu 阈值 → BEV 感知 → 元素检测 → 视觉参考选择 → 横向误差计算 → 参考控制就绪评估
/// @param capture   相机捕获数据
/// @param params    运行时参数
/// @return          处理后的感知结果
port::PerceptionResult SteeringFramePerceptionPipeline::ProcessFrame(
    const port::CameraCapture& capture,
    const port::RuntimeParameters& params) {
    int threshold = 0;
    {
        LS2K_PERF_SCOPE(port::PerfStage::kPerceptionOtsu);
        threshold = legacy::ComputeOtsuThreshold(capture.view);
    }

    port::ReferenceContinuityResult continuity{};
    port::ReferenceUsability selected_usability{};
    port::ReferenceLateralErrorEstimate lateral_error{};
    port::ReferenceControlReadiness reference_control{};
    port::PerceptionHealth health{};
    port::VisualElementEvidenceFrame element_evidence{};
    port::VisualReferenceSelection visual_selection{};
    {
        LS2K_PERF_SCOPE(port::PerfStage::kPerceptionBev);
        health.projector_ok = projector_.Valid();
        health.reason = health.projector_ok ? "ok" : "projector_invalid";
        const port::SteeringPerceptionMemory prior_memory = perception_memory_;
        legacy::BEVSimplePerceptionResult current_facts{};
        {
            LS2K_PERF_SCOPE(port::PerfStage::kBevSimple);
            current_facts =
                legacy::RunBEVSimplePerception(capture.view, threshold, params, projector_, &sample_lut_);
        }
        port::VisualReferenceCandidate line_candidate{};
        {
            LS2K_PERF_SCOPE(port::PerfStage::kVisualLineCandidate);
            line_candidate =
                legacy::MakeLineVisualReferenceCandidate(current_facts.reference_path,
                                                         current_facts.reference_source);
        }

        legacy::VisualElementPipelineInput element_input{};
        element_input.sparse_rows = &current_facts.rows;
        element_input.frame = &capture.view;
        element_input.projector = &projector_;
        element_input.threshold = threshold;
        element_input.line_candidate = line_candidate;
        legacy::VisualElementPipelineResult element_result{};
        {
            LS2K_PERF_SCOPE(port::PerfStage::kVisualElementPipeline);
            element_result = legacy::RunVisualElementPipeline(element_input, params);
        }
        element_evidence = element_result.evidence;

        port::ReferenceUsability current_usability{};
        {
            LS2K_PERF_SCOPE(port::PerfStage::kVisualReferenceSelect);
            std::vector<port::VisualReferenceCandidate> candidates;
            candidates.reserve(1U + element_result.candidates.size());
            candidates.push_back(line_candidate);
            candidates.insert(candidates.end(),
                              element_result.candidates.begin(),
                              element_result.candidates.end());
            visual_selection = legacy::SelectVisualReference(candidates);
        }
        {
            LS2K_PERF_SCOPE(port::PerfStage::kReferenceUsability);
            current_usability =
                legacy::EvaluateReferenceUsability(visual_selection.reference_path, params);
        }
        if (current_usability.usable) {
            continuity.reference_path = visual_selection.reference_path;
            continuity.mode = visual_selection.reference_path.mode;
            continuity.source = visual_selection.source;
            continuity.hold_selected = false;
            continuity.reference_capture_time_ms = capture.capture_time_ms;
            continuity.next_hold_state =
                legacy::MakeReferenceHoldState(visual_selection.reference_path,
                                               capture.capture_time_ms,
                                               params);
            selected_usability = current_usability;
        } else {
            port::ReferenceContinuityResult hold_candidate{};
            port::ReferenceUsability hold_usability{};
            {
                LS2K_PERF_SCOPE(port::PerfStage::kReferenceHold);
                hold_candidate =
                    legacy::BuildReferenceHoldCandidate(prior_memory.reference_hold, params);
                hold_usability =
                    legacy::EvaluateReferenceUsability(hold_candidate.reference_path, params);
            }
            if (hold_usability.usable) {
                continuity = hold_candidate;
                selected_usability = hold_usability;
            } else {
                continuity = {};
                {
                    LS2K_PERF_SCOPE(port::PerfStage::kReferenceUsability);
                    selected_usability =
                        legacy::EvaluateReferenceUsability(continuity.reference_path, params);
                }
            }
        }
        {
            LS2K_PERF_SCOPE(port::PerfStage::kReferenceLateralError);
            lateral_error = legacy::ComputeReferenceLateralError(continuity.reference_path,
                                                                selected_usability,
                                                                params);
        }
        {
            LS2K_PERF_SCOPE(port::PerfStage::kReferenceControlReadiness);
            reference_control = legacy::EvaluateReferenceControlReadiness(selected_usability,
                                                                          lateral_error,
                                                                          continuity.hold_selected);
        }
        perception_memory_.reference_hold = continuity.next_hold_state;
    }

    return BuildPerceptionResult(capture,
                                 threshold,
                                 health,
                                 element_evidence,
                                 visual_selection,
                                 continuity,
                                 selected_usability,
                                 lateral_error,
                                 reference_control,
                                 port::NowMs());
}

}  // namespace ls2k::runtime
