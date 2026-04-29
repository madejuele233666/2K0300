#include "legacy/camera_logic.hpp"

// 相机逻辑实现 —— 感知管线顶层入口。
// AnalyzeFrame() 串联完整 BEV 拓扑感知流程：
// 投影 → 稀疏采样 → 元素证据 → 走廊图 → 拓扑假设/证据 → 场景 FSM →
// 参考策略 → 控制误差模型，输出 PerceptionResult。

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "legacy/steering_bev_sparse_sampler.hpp"
#include "legacy/steering_bev_geometry.hpp"
#include "legacy/steering_control_error_model.hpp"
#include "legacy/steering_corridor_graph.hpp"
#include "legacy/steering_corridor_intervals.hpp"
#include "legacy/steering_gyro_continuity.hpp"
#include "legacy/steering_observation_assembly.hpp"
#include "legacy/steering_reference_policy.hpp"
#include "legacy/steering_scene_fsm.hpp"
#include "legacy/steering_topology_evidence.hpp"
#include "legacy/steering_topology_hypotheses.hpp"

namespace ls2k::legacy {
namespace {

// Otsu 快速阈值计算（2 倍步长降采样提速）—— 用于图像二值化分割
int OtsuThresholdFast(const port::LegacyCameraFrame& frame) {
    std::array<int, 256> hist{};
    if (frame.width <= 0 || frame.height <= 0) {
        return 0;
    }

    int samples = 0;
    for (int row = 0; row < frame.height; row += 2) {
        for (int col = 0; col < frame.width; col += 2) {
            const uint8_t pixel =
                frame.gray[static_cast<std::size_t>(row) * static_cast<std::size_t>(frame.width) + col];
            ++hist[pixel];
            ++samples;
        }
    }
    if (samples == 0) {
        return 0;
    }

    double sum = 0.0;
    for (int value = 0; value < 256; ++value) {
        sum += static_cast<double>(value * hist[value]);
    }

    double sum_background = 0.0;
    int weight_background = 0;
    double max_variance = -1.0;
    int threshold = 0;
    for (int value = 0; value < 256; ++value) {
        weight_background += hist[value];
        if (weight_background == 0) {
            continue;
        }
        const int weight_foreground = samples - weight_background;
        if (weight_foreground == 0) {
            break;
        }
        sum_background += static_cast<double>(value * hist[value]);
        const double mean_background = sum_background / static_cast<double>(weight_background);
        const double mean_foreground = (sum - sum_background) / static_cast<double>(weight_foreground);
        const double between_variance =
            static_cast<double>(weight_background) * static_cast<double>(weight_foreground) *
            (mean_background - mean_foreground) * (mean_background - mean_foreground);
        if (between_variance > max_variance) {
            max_variance = between_variance;
            threshold = value;
        }
    }
    return threshold;
}

// 返回车道几何历史锚点的采样索引（近/中/远三层）
std::array<std::size_t, port::kLaneGeometryAnchorCount> AnchorIndices() {
    return {1U, 3U, 6U};
}

// 构建车道几何历史快照 —— 将 BEV 边界锚点反投影回图像平面
port::LaneGeometryHistorySnapshot BuildLaneHistorySnapshot(const port::BEVTrackEstimate& track,
                                                           const BEVProjector& projector,
                                                           int frame_width,
                                                           int frame_height) {
    port::LaneGeometryHistorySnapshot snapshot{};
    const std::array<std::size_t, port::kLaneGeometryAnchorCount> indices = AnchorIndices();
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const std::size_t sample_index = std::min(indices[i], port::kBevTrackSampleCount - 1);
        if (track.sampled_left_boundary[sample_index].valid) {
            port::ImagePoint image{};
            if (projector.ProjectVehicleToImage(track.sampled_left_boundary[sample_index].point, image)) {
                snapshot.left_visible_anchors[i].valid = true;
                snapshot.left_visible_anchors[i].row =
                    std::clamp(static_cast<int>(std::lround(image.row_px)), 0, std::max(0, frame_height - 1));
                snapshot.left_visible_anchors[i].col =
                    std::clamp(static_cast<int>(std::lround(image.col_px)), 0, std::max(0, frame_width - 1));
                snapshot.valid = true;
            }
        }
        if (track.sampled_right_boundary[sample_index].valid) {
            port::ImagePoint image{};
            if (projector.ProjectVehicleToImage(track.sampled_right_boundary[sample_index].point, image)) {
                snapshot.right_visible_anchors[i].valid = true;
                snapshot.right_visible_anchors[i].row =
                    std::clamp(static_cast<int>(std::lround(image.row_px)), 0, std::max(0, frame_height - 1));
                snapshot.right_visible_anchors[i].col =
                    std::clamp(static_cast<int>(std::lround(image.col_px)), 0, std::max(0, frame_width - 1));
                snapshot.valid = true;
            }
        }
    }
    return snapshot;
}

// 根据远距离航向误差判断转向方向（左/右/直行）
int TrackTurnSign(const port::BEVTrackEstimate& track) {
    if (track.far_heading_error > 0.02F) {
        return 1;
    }
    if (track.far_heading_error < -0.02F) {
        return -1;
    }
    return 0;
}

// 计算参考路径的航向角（远近两点之间的方位角）
float ReferenceHeading(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                       std::size_t near_index,
                       std::size_t far_index) {
    if (near_index >= path.size() || far_index >= path.size() || far_index <= near_index ||
        !path[near_index].valid || !path[far_index].valid) {
        return 0.0F;
    }
    const float ds = path[far_index].point.forward_m - path[near_index].point.forward_m;
    if (std::abs(ds) < 1e-4F) {
        return 0.0F;
    }
    return std::atan2(path[far_index].point.lateral_m - path[near_index].point.lateral_m, ds);
}

// 通过三点差分计算参考路径曲率
float ReferenceCurvature(const std::array<port::BEVPathSample, port::kBevTrackSampleCount>& path,
                         std::size_t near_index,
                         std::size_t curvature_index) {
    if (near_index == 0U || near_index >= path.size() || curvature_index >= path.size() ||
        curvature_index <= near_index || !path[0].valid || !path[near_index].valid ||
        !path[curvature_index].valid) {
        return 0.0F;
    }
    const float ds1 = std::max(1e-4F, path[near_index].point.forward_m - path[0].point.forward_m);
    const float ds2 =
        std::max(1e-4F, path[curvature_index].point.forward_m - path[near_index].point.forward_m);
    const float slope1 = (path[near_index].point.lateral_m - path[0].point.lateral_m) / ds1;
    const float slope2 = (path[curvature_index].point.lateral_m - path[near_index].point.lateral_m) / ds2;
    return (slope2 - slope1) /
           std::max(1e-4F, path[curvature_index].point.forward_m - path[0].point.forward_m);
}

// 判断参考模式是否可为控制提供有效支持（非平凡模式）
bool ReferenceModeCanSupportControl(port::ReferenceMode mode) {
    return mode == port::ReferenceMode::kHoldLast ||
           mode == port::ReferenceMode::kEntryHeadingExtension ||
           mode == port::ReferenceMode::kArcFollow ||
           mode == port::ReferenceMode::kBlendToExit ||
           mode == port::ReferenceMode::kLostPrediction ||
           mode == port::ReferenceMode::kStableBoundaryOffset ||
           mode == port::ReferenceMode::kBlend;
}

// 当 BEV 跟踪无效但参考路径可用时，用参考路径构建支持性跟踪。
// 降级模式下仍可提供有限的控制信号。
port::BEVTrackEstimate BuildReferenceSupportTrack(const port::BEVTrackEstimate& observed_track,
                                                  const port::BEVReferencePath& reference,
                                                  const port::RuntimeParameters& params) {
    if (observed_track.valid || !reference.valid || !ReferenceModeCanSupportControl(reference.mode)) {
        return observed_track;
    }
    port::BEVTrackEstimate support = observed_track;
    support.valid = observed_track.calibration_valid;
    support.continuity_valid = support.valid;
    support.source = "bev_corridor_topology";
    support.fallback_mode = support.valid ? "reference_policy_support" : observed_track.fallback_mode;
    if (!support.valid) {
        return support;
    }

    float confidence_sum = 0.0F;
    int valid_count = 0;
    for (std::size_t index = 0; index < reference.sampled_path.size(); ++index) {
        const port::BEVPathSample& sample = reference.sampled_path[index];
        if (!sample.valid) {
            continue;
        }
        support.sampled_centerline[index] = sample;
        support.visible_range_m = std::max(support.visible_range_m, sample.point.forward_m);
        confidence_sum += sample.confidence;
        ++valid_count;
    }
    if (valid_count < 2) {
        support.valid = false;
        support.continuity_valid = false;
        support.fallback_mode = "reference_policy_support_short";
        return support;
    }
    const float mode_scale =
        reference.mode == port::ReferenceMode::kLostPrediction ? 0.55F : 0.75F;
    support.track_confidence =
        std::clamp((confidence_sum / static_cast<float>(valid_count)) * mode_scale, 0.0F, 1.0F);
    const std::size_t near_index =
        std::min<std::size_t>(static_cast<std::size_t>(std::max(0, params.bev_control_model.near_sample_index)),
                              port::kBevTrackSampleCount - 1U);
    const std::size_t far_index =
        std::min<std::size_t>(static_cast<std::size_t>(std::max(0, params.bev_control_model.far_sample_index)),
                              port::kBevTrackSampleCount - 1U);
    const std::size_t curvature_index =
        std::min<std::size_t>(static_cast<std::size_t>(std::max(0, params.bev_control_model.curvature_sample_index)),
                              port::kBevTrackSampleCount - 1U);
    if (support.sampled_centerline[near_index].valid) {
        support.near_lateral_error = support.sampled_centerline[near_index].point.lateral_m;
    }
    support.far_heading_error = ReferenceHeading(support.sampled_centerline, near_index, far_index);
    support.preview_curvature = ReferenceCurvature(support.sampled_centerline, near_index, curvature_index);
    return support;
}

// 构建跟踪历史快照 —— 包含中心线锚点图像坐标、车道宽度、转向方向序列
port::TrackHistorySnapshot BuildTrackHistorySnapshot(const port::BEVTrackEstimate& track,
                                                     const BEVProjector& projector,
                                                     int frame_width,
                                                     int frame_height,
                                                     const port::LegacySteeringState& prior_state) {
    port::TrackHistorySnapshot snapshot{};
    if (!track.valid) {
        snapshot = prior_state.track_history;
        if (snapshot.valid) {
            snapshot.track_confidence *= 0.85F;
        }
        return snapshot;
    }

    snapshot.valid = true;
    const std::array<std::size_t, port::kLaneGeometryAnchorCount> indices = AnchorIndices();
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const std::size_t sample_index = std::min(indices[i], port::kBevTrackSampleCount - 1);
        if (!track.sampled_centerline[sample_index].valid) {
            continue;
        }
        port::ImagePoint image{};
        if (!projector.ProjectVehicleToImage(track.sampled_centerline[sample_index].point, image)) {
            continue;
        }
        snapshot.center_anchors[i].valid = true;
        snapshot.center_anchors[i].row =
            std::clamp(static_cast<int>(std::lround(image.row_px)), 0, std::max(0, frame_height - 1));
        snapshot.center_anchors[i].col =
            std::clamp(static_cast<int>(std::lround(image.col_px)), 0, std::max(0, frame_width - 1));
    }
    snapshot.lane_width_px = track.lane_width_profile_m[0] * 100.0F;
    snapshot.heading_px_per_row = track.far_heading_error;
    snapshot.curvature_px_per_row2 = track.preview_curvature;
    const int turn_sign = TrackTurnSign(track);
    snapshot.turn_sign = turn_sign;
    snapshot.last_nonzero_turn_sign =
        turn_sign != 0 ? turn_sign : prior_state.track_history.last_nonzero_turn_sign;
    snapshot.zero_turn_sign_frames = turn_sign == 0 ? prior_state.track_history.zero_turn_sign_frames + 1 : 0;
    snapshot.track_confidence = track.track_confidence;
    return snapshot;
}

// 将 circle reference_mode 与 direction 联合以兼容历史记录格式
std::string CircleReferenceModeForCompatibility(const std::string& reference_mode,
                                                const std::string& circle_direction) {
    if (circle_direction == "none") {
        return "none";
    }
    return reference_mode;
}

}  // namespace

// 感知管线顶层入口。以一帧灰度图像为输入，串联完整的 BEV 拓扑感知流程：
// Otsu 阈值 → BEV 投影 → 稀疏采样 → 走廊间隔/图 → 场景观测组装 →
// 道路假设 → 拓扑证据 → 场景 FSM → 参考策略 → 控制误差模型 → 陀螺仪连续
// 性约束。输出 SteeringAnalysisResult 供控制循环消费。
SteeringAnalysisResult AnalyzeFrame(const port::LegacyCameraFrame& frame,
                                    const port::RuntimeParameters& params,
                                    const port::LegacySteeringState& prior_state,
                                    const port::ImuSample& imu,
                                    bool low_voltage_emergency,
                                    uint64_t frame_id,
                                    uint64_t capture_time_ms) {
    SteeringAnalysisResult analysis{};
    const int threshold = OtsuThresholdFast(frame);

    // --- 第 1 阶段：BEV 投影 + 稀疏采样 + 走廊图 ---
    BEVProjector projector{};
    (void)projector.Configure(params.bev_projector);
    const BEVTopologyPipelineResult topology =
        RunBEVTopologyPipeline(frame, threshold, params, prior_state, projector);
    const CorridorIntervalSet& intervals = topology.corridor_intervals;
    const CorridorGraph& graph = topology.corridor_graph;
    port::BEVTrackEstimate track = topology.track_estimate;
    const ObservationAssemblyResult assembled =
        AssembleObservation(frame,
                            threshold,
                            params,
                            prior_state,
                            imu,
                            low_voltage_emergency,
                            frame_id,
                            capture_time_ms,
                            track,
                            projector);
    // --- 第 2 阶段：道路假设生成 ---
    const port::RoadHypotheses hypotheses =
        GenerateRoadHypotheses(graph, intervals, prior_state, params, &topology.element_evidence);
    const port::TopologyEvidence topology_evidence =
        ScoreTopologyEvidence(hypotheses,
                              intervals,
                              assembled.vehicle,
                              params,
                              prior_state.topology_evidence_accumulator,
                              &topology.element_evidence);
    const port::TopologyEvidenceAccumulator topology_accumulator =
        UpdateTopologyEvidenceAccumulator(topology_evidence,
                                          params,
                                          prior_state.topology_evidence_accumulator);
    // --- 第 3 阶段：场景 FSM + 参考策略 ---
    const SceneFsmResult scene =
        UpdateTopologySceneFsm(topology_evidence, assembled.vehicle, params, prior_state.scene_fsm);
    const ReferencePolicyResult reference =
        ResolveReferencePolicy(hypotheses,
                               topology_evidence,
                               scene.state,
                               prior_state.reference_policy,
                               params);
    const port::BEVTrackEstimate control_track =
        BuildReferenceSupportTrack(track, reference.reference_path, params);
    // --- 第 4 阶段：控制约束 + 误差模型 ---
    port::ControlConstraintSet constraints = assembled.constraints;
    if (!track.valid && control_track.valid) {
        constraints.fail_safe_veto = false;
        constraints.steering_suppressed = false;
        constraints.primary_reason = "reference_policy_support";
        constraints.low_confidence_degraded =
            control_track.track_confidence < static_cast<float>(params.bev_control_model.low_confidence_threshold) ||
            control_track.visible_range_m < static_cast<float>(params.bev_control_model.low_visible_range_m);
        if (constraints.low_confidence_degraded) {
            const float confidence_scale =
                std::clamp(control_track.track_confidence /
                               static_cast<float>(std::max(1e-4, params.bev_control_model.low_confidence_threshold)),
                           static_cast<float>(params.bev_control_model.min_gain_scale),
                           1.0F);
            const float visible_scale =
                std::clamp(control_track.visible_range_m /
                               static_cast<float>(std::max(1e-4, params.bev_control_model.low_visible_range_m)),
                           static_cast<float>(params.bev_control_model.min_speed_limit_scale),
                           1.0F);
            constraints.steering_gain_scale = std::min(constraints.steering_gain_scale,
                                                       static_cast<double>(confidence_scale));
            constraints.speed_limit_scale = std::min(constraints.speed_limit_scale,
                                                     static_cast<double>(visible_scale));
            constraints.turn_limit_scale = std::min(constraints.turn_limit_scale,
                                                    static_cast<double>(std::min(confidence_scale, visible_scale)));
        }
    }
    if (scene.state.active_scene == port::SpecialSceneKind::kZebra) {
        constraints.low_confidence_degraded = true;
        constraints.primary_reason = "zebra_hold";
        constraints.speed_limit_scale =
            std::min(constraints.speed_limit_scale, params.bev_control_model.min_speed_limit_scale);
        constraints.turn_limit_scale =
            std::min(constraints.turn_limit_scale, params.bev_control_model.min_speed_limit_scale);
    }
    if (topology_evidence.lost_score > 0.65F) {
        constraints.low_confidence_degraded = true;
        constraints.primary_reason = "topology_lost";
        if (prior_state.lost_prediction_cycles >= params.bev_reference_policy.hold_last_max_cycles) {
            constraints.steering_suppressed = true;
        }
    }
    port::ControlErrorModelInput control_input{};
    control_input.track = control_track;
    control_input.reference_path = reference.reference_path;
    control_input.trusted_error_reference = reference.trusted_error_reference;
    control_input.trusted_error_confidence = reference.trusted_error_confidence;
    control_input.vehicle = assembled.vehicle;
    control_input.constraints = constraints;
    const port::ControlErrorModelOutput control_output =
        ComputeControlErrorModel(control_input, params);
    const GyroContinuityConstraint continuity =
        ComputeGyroContinuityConstraint(prior_state, imu, capture_time_ms);

    port::PerceptionResult perception{};
    perception.published = true;
    perception.fresh = true;
    perception.low_voltage_veto = low_voltage_emergency;
    perception.threshold = threshold;
    perception.threshold_veto = false;
    perception.geometry_veto = !control_track.valid;
    perception.emergency_veto = low_voltage_emergency || control_output.steering_suppressed;
    perception.frame_id = frame_id;
    perception.capture_time_ms = capture_time_ms;
    perception.active_module = scene.active_module;
    perception.scene_phase = scene.scene_phase;
    perception.scene_override_source = scene.scene_override_source;
    perception.reference_mode = reference.reference_mode;
    perception.circle_direction = scene.state.circle_direction;
    perception.circle_reference_mode =
        CircleReferenceModeForCompatibility(reference.reference_mode, scene.state.circle_direction);
    perception.circle_heading_delta_deg = continuity.next_state.heading_delta_deg_150ms;
    perception.circle_yaw_accum_deg = scene.state.circle_yaw_accum_deg;
    perception.circle_path_phase = reference.state.circle_reference_phase != "none"
                                       ? reference.state.circle_reference_phase
                                       : scene.state.circle_path_phase;
    perception.reference_compatibility_error_m = reference.state.compatibility_error_m;
    perception.reference_source = reference.state.reference_source;
    perception.circle_entry_signal_active = scene.state.circle_entry_signal_active;
    perception.track_confidence = control_track.track_confidence;
    perception.track_valid = control_track.valid;
    perception.sign_flip_blocked = false;
    perception.imu_grace_active = continuity.imu_grace_active;
    perception.gyro_heading_delta_deg = continuity.heading_delta_deg;
    perception.gyro_consistency_score = 1.0F;
    perception.perception_tag = "bev_first";
    perception.lateral_error = control_output.near_lateral_error;
    perception.heading_error = control_output.far_heading_error;
    perception.curvature = control_output.preview_curvature;
    perception.near_lateral_error = control_output.near_lateral_error;
    perception.far_heading_error = control_output.far_heading_error;
    perception.preview_curvature = control_output.preview_curvature;
    perception.visible_range_m = control_output.visible_range_m;
    perception.bev_track = control_track;
    perception.road_hypotheses = hypotheses;
    perception.topology_evidence = topology_evidence;
    perception.scene_observation = assembled.observation;
    perception.control_constraints = constraints;
    perception.control_model = control_output;
    perception.roadblock_active = false;
    perception.roadblock_interface_state = "supported_not_implemented";

    analysis.perception = perception;
    analysis.track_estimate = control_track;
    analysis.road_hypotheses = hypotheses;
    analysis.topology_evidence = topology_evidence;
    analysis.scene_observation = assembled.observation;
    analysis.reference_path = reference.reference_path;
    analysis.control_constraints = constraints;
    analysis.control_output = control_output;
    analysis.scene_debug_candidate = scene.state.debug_candidate;
    analysis.scene_debug_candidate_streak = scene.state.candidate_streak;
    analysis.scene_cross_candidate_score_last = topology_evidence.cross_score;
    analysis.scene_circle_left_candidate_score_last = topology_evidence.left_circle_score;
    analysis.scene_circle_right_candidate_score_last = topology_evidence.right_circle_score;
    analysis.gyro_continuity_state = continuity.next_state;
    analysis.lane_geometry_snapshot = BuildLaneHistorySnapshot(control_track, projector, frame.width, frame.height);
    analysis.track_history_snapshot =
        BuildTrackHistorySnapshot(control_track, projector, frame.width, frame.height, prior_state);

    analysis.steering_state_update = prior_state;
    analysis.steering_state_update.active_module = scene.active_module;
    analysis.steering_state_update.scene_phase = scene.scene_phase;
    analysis.steering_state_update.scene_override_source = scene.scene_override_source;
    analysis.steering_state_update.roadblock_active = false;
    analysis.steering_state_update.scene_debug_candidate = scene.state.debug_candidate;
    analysis.steering_state_update.scene_debug_candidate_streak = scene.state.candidate_streak;
    analysis.steering_state_update.scene_cross_candidate_score_last = analysis.scene_cross_candidate_score_last;
    analysis.steering_state_update.scene_circle_left_candidate_score_last =
        analysis.scene_circle_left_candidate_score_last;
    analysis.steering_state_update.scene_circle_right_candidate_score_last =
        analysis.scene_circle_right_candidate_score_last;
    analysis.steering_state_update.last_bev_track = control_track;
    analysis.steering_state_update.last_road_hypotheses = hypotheses;
    analysis.steering_state_update.last_topology_evidence = topology_evidence;
    analysis.steering_state_update.topology_evidence_accumulator = topology_accumulator;
    analysis.steering_state_update.lost_prediction_cycles =
        topology_evidence.lost_score > 0.65F ? prior_state.lost_prediction_cycles + 1 : 0;
    analysis.steering_state_update.bev_track_memory.has_previous_track = control_track.valid;
    if (control_track.valid) {
        analysis.steering_state_update.bev_track_memory.previous_track = control_track;
        analysis.steering_state_update.bev_track_memory.carry_cycles = 0;
    } else {
        ++analysis.steering_state_update.bev_track_memory.carry_cycles;
    }
    analysis.steering_state_update.scene_fsm = scene.state;
    analysis.steering_state_update.reference_policy = reference.state;
    analysis.steering_state_update.lane_geometry_previous = prior_state.lane_geometry_recent;
    analysis.steering_state_update.lane_geometry_recent = analysis.lane_geometry_snapshot;
    analysis.steering_state_update.track_history = analysis.track_history_snapshot;
    analysis.steering_state_update.gyro_continuity = continuity.next_state;
    analysis.steering_state_update_valid = true;
    return analysis;
}

}  // namespace ls2k::legacy
