#ifndef LS2K_PORT_RUNTIME_PARAMETER_TYPES_HPP
#define LS2K_PORT_RUNTIME_PARAMETER_TYPES_HPP

#include <string>

#include "port/bev_geometry_types.hpp"
#include "port/visual_element_evidence_types.hpp"

namespace ls2k::port {

struct WheelPidParameters {
    double p = 84.0;
    double i = 2.4;
    double d = 0.75;
    double integral_limit = 5000.0;
    double measurement_filter_alpha = 0.4;
};

struct AssistantTcpParameters {
    std::string host = "10.100.170.115";
    int port = 8888;
};

struct RuntimeParameters {
    double running_speed_target = 300.0;
    double yaw_rate_pid_p = 12.0;
    double yaw_rate_pid_i = 0.0;
    double yaw_rate_pid_d = 0.0;
    int exp_light = 65;

    int low_voltage_raw_threshold = 400;
    int control_period_ms = 5;
    int perception_stale_ms = 120;
    int pwm_limit = 5000;
    int raw_turn_output_limit = 3000;
    int pwm_floor = 0;
    bool prohibit_reverse_pwm = false;
    int prohibit_reverse_pwm_step_limit = 1000;
    int motion_unveto_confirm_cycles = 3;
    int motion_spinup_ms = 800;
    double motion_turn_limit_spinup = 1.0;
    int motion_pwm_step_limit = 3000;
    int motion_stop_ms = 300;
    int motion_stop_encoder_threshold = 8;
    int motion_fault_rearm_hold_ms = 600;
    WheelPidParameters left_wheel_pid{};
    WheelPidParameters right_wheel_pid{96.0, 2.2, 0.2, 5000.0, 0.4};

    int control_snapshot_emit_interval_ms = 100;
    bool assistant_enabled = true;
    AssistantTcpParameters assistant_tcp{};
    bool steering_media_enabled = true;
    int steering_media_port = 8890;
    int steering_media_publish_interval_ms = 120;
    int low_voltage_sample_interval_ms = 1000;

    int camera_frame_width = 320;
    int camera_frame_height = 240;
    BEVProjectorCalibration bev_projector{};
    BEVGeometryParameters bev_geometry{};
    BEVClassificationParameters bev_classification{};
    BEVControlModelParameters bev_control_model{};
    BEVElementParameters bev_element{};
    bool startup_critical_applied = false;
    bool loaded_from_defaults = false;
    bool parse_failure = false;
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_RUNTIME_PARAMETER_TYPES_HPP
