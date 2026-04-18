#ifndef LS2K_PORT_CONTROL_TYPES_HPP
#define LS2K_PORT_CONTROL_TYPES_HPP

#include <array>
#include <cstdint>
#include <string>

namespace ls2k::port {

constexpr int kLegacyFrameWidth = 160;
constexpr int kLegacyFrameHeight = 128;
constexpr int kPhase1UvcWidth = 160;
constexpr int kPhase1UvcHeight = 120;

enum class CameraGeometryMarker {
    kPhase1Adapted,
    kNonPhase1Geometry,
    kEmptyFrame,
    kAdapterNotReady,
    kAdaptationHookRouted
};

struct LegacyCameraFrame {
    std::array<uint8_t, kLegacyFrameWidth * kLegacyFrameHeight> gray{};
};

struct CameraCapture {
    bool has_frame = false;
    LegacyCameraFrame frame{};
    CameraGeometryMarker marker = CameraGeometryMarker::kAdapterNotReady;
    int source_width = 0;
    int source_height = 0;
    uint64_t frame_id = 0;
    uint64_t capture_time_ms = 0;
};

struct PerceptionResult {
    bool published = false;
    bool fresh = false;
    bool emergency_veto = true;
    bool low_voltage_veto = false;
    bool threshold_veto = false;
    bool geometry_veto = false;
    float lateral_error = 0.0F;
    int threshold = 0;
    int highest_line = 0;
    uint64_t frame_id = 0;
    uint64_t capture_time_ms = 0;
    std::string perception_tag = "none";
};

struct ImuSample {
    bool valid = false;
    float acc_x = 0.0F;
    float acc_y = 0.0F;
    float acc_z = 0.0F;
    float gyro_x = 0.0F;
    float gyro_y = 0.0F;
    float gyro_z = 0.0F;
    uint64_t capture_time_ms = 0;
};

struct EncoderDelta {
    bool valid = false;
    int left = 0;
    int right = 0;
    uint64_t capture_time_ms = 0;
};

struct LowVoltageSample {
    bool valid = false;
    bool emergency = true;
    int raw_value = -1;
    int threshold = 0;
    uint64_t capture_time_ms = 0;
    std::string source = "unavailable";
};

struct ActuatorCommand {
    int left_pwm = 0;
    int right_pwm = 0;
    bool emergency_stop = true;
};

struct RuntimeParameters {
    double Speed_base = 77.0;
    double JWJC = 1.0;
    double circle_k = 1.10;
    double circle_b = 35.0;
    double road_k = 1.15;
    double road_b = 55.0;
    double see_max = 35.0;
    double pid_turn_camera_d = 5.0;
    double pid_turn_gyro_camera_d = 9.0;
    double Straight_permit = 0.0;
    double island_point = 25.0;
    double island_delay = 500.0;
    double circle_k_err = 0.0;
    int P_Mode = 0;
    int exp_light = 65;

    // Additional phase-1 runtime policy values.
    int emergency_threshold = 40;
    int control_period_ms = 5;
    int perception_stale_ms = 40;
    int pwm_limit = 9000;
    bool startup_critical_applied = false;
    bool loaded_from_defaults = false;
    bool parse_failure = false;
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_CONTROL_TYPES_HPP
