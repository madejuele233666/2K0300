#ifndef LS2K_PLATFORM_STEERING_MEDIA_PROTOCOL_HPP
#define LS2K_PLATFORM_STEERING_MEDIA_PROTOCOL_HPP

// 转向媒体协议定义 —— 参数快照、图像帧和编码/解码接口。
// 用于将感知/控制状态和相机帧打包传输到外部媒体系统。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "port/control_types.hpp"

namespace ls2k::platform {

// 计算指定分辨率下图像负载的字节数
std::size_t SteeringMediaImagePayloadBytes(int width, int height);

// 参数快照视图 —— 包含 PID 参数和 BEV 配置的运行时快照
struct SteeringMediaParamSnapshotView {
    double pid_turn_camera_p = 0.0;
    double pid_turn_camera_p_scale = 0.0;
    double pid_turn_camera_d = 0.0;
    bool pid_turn_camera_use_fuzzy = false;
    double pid_turn_gyro_camera_p = 0.0;
    double pid_turn_gyro_camera_i = 0.0;
    double pid_turn_gyro_camera_d = 0.0;
    int p_mode = 0;
    double speed_base = 0.0;
    int control_period_ms = 0;
    int raw_turn_output_limit = 0;
    port::BEVProjectorCalibration bev_projector{};
    port::BEVGeometryParameters bev_geometry{};
    port::BEVSceneFsmParameters bev_scene_fsm{};
    port::BEVControlModelParameters bev_control_model{};
    port::BEVTopologySamplerParameters bev_topology_sampler{};
    port::BEVCorridorGraphParameters bev_corridor_graph{};
    port::BEVTopologyEvidenceParameters bev_topology_evidence{};
    port::BEVReferencePolicyParameters bev_reference_policy{};
    port::BEVPathPolicyParameters bev_path_policy{};
};

struct SteeringMediaSnapshotView {
    double near_lateral_error = 0.0;
    double far_heading_error = 0.0;
    double preview_curvature = 0.0;
    double lookahead_distance_m = 0.0;
    double lookahead_lateral_error = 0.0;
    double lookahead_heading_error = 0.0;
    double reference_curvature = 0.0;
    double curvature_command = 0.0;
    double yaw_rate_target = 0.0;
    double visible_range_m = 0.0;
    double scene_width_expand_ratio = 1.0;
    double scene_cross_bilateral_open_score_m = 0.0;
    bool scene_cross_bilateral_open = false;
    bool scene_cross_candidate = false;
    bool scene_zebra_candidate = false;
    bool scene_circle_left_candidate = false;
    bool scene_circle_right_candidate = false;
    double scene_left_open_score = 0.0;
    double scene_right_open_score = 0.0;
    double scene_left_contract_score = 0.0;
    double scene_right_contract_score = 0.0;
    double scene_left_boundary_heading_abs_rad = 0.0;
    double scene_right_boundary_heading_abs_rad = 0.0;
    bool scene_circle_left_opposite_straight = false;
    bool scene_circle_right_opposite_straight = false;
    double lateral_error = 0.0;
    double heading_error = 0.0;
    double curvature = 0.0;
    double track_confidence = 0.0;
    bool track_valid = false;
    bool sign_flip_blocked = false;
    bool imu_grace_active = false;
    double gyro_heading_delta_deg = 0.0;
    double gyro_consistency_score = 1.0;
    int threshold = 0;
    bool threshold_veto = false;
    double resolved_fuzzy_p = 0.0;
    double camera_p_term = 0.0;
    double camera_d_term = 0.0;
    double w_target = 0.0;
    double gyro_z = 0.0;
    double gyro_error = 0.0;
    double gyro_p_term = 0.0;
    double gyro_d_term = 0.0;
    int raw_turn_output = 0;
    int applied_turn_output = 0;
    bool roadblock_active = false;
    std::string active_module = "straight";
    std::string scene_phase = "idle";
    std::string scene_override_source = "none";
    std::string reference_mode = "centerline";
    std::string roadblock_interface_state = "supported_not_implemented";
    std::string circle_direction = "none";
    std::string circle_reference_mode = "none";
    double circle_heading_delta_deg = 0.0;
    double circle_yaw_accum_deg = 0.0;
    std::string circle_path_phase = "none";
    double reference_compatibility_error_m = 0.0;
    std::string reference_source = "none";
    bool circle_entry_signal_active = false;
};

struct SteeringMediaConfigSnapshot {
    std::uint64_t publish_time_ms = 0;
    int media_publish_interval_ms = 0;
    SteeringMediaParamSnapshotView param_snapshot{};
};

struct SteeringMediaImageFrame {
    std::uint64_t frame_id = 0;
    std::uint64_t capture_time_ms = 0;
    std::uint64_t publish_time_ms = 0;
    int width = 0;
    int height = 0;
    const char* motion_phase = "DISARMED";
    SteeringMediaSnapshotView steering_snapshot{};
    const std::uint8_t* pixel_data = nullptr;
    std::size_t pixel_size = 0;
};

bool EncodeSteeringMediaConfigSnapshot(const SteeringMediaConfigSnapshot& snapshot,
                                       std::vector<std::uint8_t>& encoded,
                                       std::string& error);
bool EncodeSteeringMediaImageFrame(const SteeringMediaImageFrame& frame,
                                   std::vector<std::uint8_t>& encoded,
                                   std::string& error);
bool DecodeSteeringMediaEnvelope(const std::uint8_t* data,
                                 std::size_t size,
                                 std::string& header_json,
                                 std::vector<std::uint8_t>& payload,
                                 std::string& error);
bool ValidateSteeringMediaImagePayload(int width,
                                       int height,
                                       std::size_t payload_size,
                                       std::string& error);

}  // namespace ls2k::platform

#endif  // LS2K_PLATFORM_STEERING_MEDIA_PROTOCOL_HPP
