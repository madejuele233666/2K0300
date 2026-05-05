#ifndef LS2K_PLATFORM_STEERING_MEDIA_PROTOCOL_HPP
#define LS2K_PLATFORM_STEERING_MEDIA_PROTOCOL_HPP

// 转向媒体协议定义 —— 参数快照、图像帧和编码/解码接口。
// 用于将感知/控制状态和相机帧打包传输到外部媒体系统。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "port/bev_geometry_types.hpp"

namespace ls2k::platform {

// 计算指定分辨率下图像负载的字节数
std::size_t SteeringMediaImagePayloadBytes(int width, int height);

// 参数快照视图 —— 包含 gyro yaw 参数和 BEV 配置的运行时快照
struct SteeringMediaParamSnapshotView {
    double running_speed_target = 0.0;
    double yaw_rate_pid_p = 0.0;
    double yaw_rate_pid_i = 0.0;
    double yaw_rate_pid_d = 0.0;
    int control_period_ms = 0;
    int low_voltage_sample_interval_ms = 0;
    int low_voltage_raw_threshold = 0;
    int raw_turn_output_limit = 0;
    port::BEVProjectorCalibration bev_projector{};
    port::BEVGeometryParameters bev_geometry{};
    port::BEVClassificationParameters bev_classification{};
    port::BEVControlModelParameters bev_control_model{};
};

struct SteeringMediaReferenceView {
    std::string mode = "none";
    std::string source = "none";
};

struct SteeringMediaPerceptionHealthView {
    bool projector_ok = false;
    std::string reason = "projector_invalid";
};

struct SteeringMediaEligibilityView {
    bool usable = false;
    std::uint64_t leading_usable_samples = 0;
    double leading_min_forward_m = 0.0;
    double leading_max_forward_m = 0.0;
    std::string reason = "no_reference_facts";
};

struct SteeringMediaLateralErrorView {
    bool computed = false;
    double weighted_lateral_error_m = 0.0;
    std::uint64_t weighted_sample_count = 0;
    double weight_sum = 0.0;
    std::string reason = "reference_unusable";
};

struct SteeringMediaReferenceControlView {
    bool ready = false;
    std::string reason = "reference_unusable";
};

struct SteeringMediaSafetyGateView {
    bool veto_active = true;
    std::string reason = "perception_stale";
};

struct SteeringMediaDegradedView {
    bool active = false;
    std::string reason = "none";
};

struct SteeringMediaYawControlView {
    double turn_output_target = 0.0;
};

struct SteeringMediaActuatorView {
    int raw_turn_output = 0;
    int applied_turn_output = 0;
};

struct SteeringMediaSnapshotView {
    int threshold = 0;
    SteeringMediaPerceptionHealthView perception_health{};
    SteeringMediaReferenceView reference{};
    SteeringMediaEligibilityView eligibility{};
    SteeringMediaLateralErrorView lateral_error{};
    SteeringMediaReferenceControlView reference_control{};
    SteeringMediaSafetyGateView safety_gate{};
    SteeringMediaDegradedView degraded{};
    SteeringMediaYawControlView yaw_control{};
    SteeringMediaActuatorView actuator{};
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
