#ifndef LS2K_PLATFORM_STEERING_MEDIA_PROTOCOL_HPP
#define LS2K_PLATFORM_STEERING_MEDIA_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "port/control_types.hpp"

namespace ls2k::platform {

std::size_t SteeringMediaImagePayloadBytes(int width, int height);

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
};

struct SteeringMediaSnapshotView {
    double lateral_error = 0.0;
    int highest_line = 0;
    int farthest_line = 0;
    int steering_reference_col = 160;
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
    std::string roadblock_interface_state = "supported_not_implemented";
    std::string last_special_scene_correction = "none";
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
