/**
 * @file runtime_parameter_types.hpp
 * @brief 全部运行时参数类型定义
 *
 * 集中定义所有可在运行时动态调整的参数。
 * 包含车轮PID、助理TCP、运动控制、感知、BEV投影、转向媒体、低电压采样等全部子系统参数。
 * 参数通过JSON配置文件加载，支持启动时批量下发。
 */

#ifndef LS2K_PORT_RUNTIME_PARAMETER_TYPES_HPP
#define LS2K_PORT_RUNTIME_PARAMETER_TYPES_HPP

#include <string>

#include "port/bev_element_raster_types.hpp"
#include "port/bev_geometry_types.hpp"
#include "port/visual_element_evidence_types.hpp"

namespace ls2k::port {

/**
 * @struct WheelPidParameters
 * @brief 车轮PID控制器参数
 *
 * 左右轮可独立配置PID参数和测量滤波系数。
 */
struct WheelPidParameters {
    double p = 84.0;             ///< 比例增益
    double i = 2.4;             ///< 积分增益
    double d = 0.75;            ///< 微分增益
    double integral_limit = 5000.0;  ///< 积分项限幅
    double measurement_filter_alpha = 0.4;  ///< 测量值低通滤波系数（0~1，越小越平滑）
};

/**
 * @struct AssistantTcpParameters
 * @brief 助理连接的TCP参数
 *
 * 定义与地面站/上位机通信的TCP连接配置。
 */
struct AssistantTcpParameters {
    std::string host = "192.168.137.1";   ///< 上位机IP地址
    int port = 48011;                     ///< TCP端口号
};

/// Reference time alignment 参数
struct ReferenceTimeAlignmentParameters {
    bool enabled = false;                  ///< 是否启用控制侧 reference 时间对齐
    int max_age_ms = 120;                  ///< 最大可对齐 reference 年龄
    int max_integration_gap_ms = 30;       ///< motion history 最大允许采样空洞
    double max_delta_yaw_rad = 0.80;       ///< 单次对齐最大 yaw 积分
    int min_aligned_samples = 3;           ///< 对齐后最少前方样本数
};

/// Camera source 参数，独立于 BEV/circle/cross 语义
struct CameraSourceParameters {
    std::string backend = "v4l2_yuyv";     ///< 主相机后端
    std::string device = "/dev/video0";    ///< V4L2 设备路径
    int width = 320;                       ///< 采集宽度
    int height = 240;                      ///< 采集高度
    int fps = 60;                          ///< 目标帧率
    int buffer_count = 3;                  ///< V4L2 mmap buffer 数
    int poll_timeout_ms = 50;              ///< capture thread 内 poll 超时
    bool drain_ready_buffers = true;       ///< 一次 wait 中 drain ready buffers
    std::string fallback_backend = "vendor_uvc";  ///< primary startup 失败时 fallback 后端
};

/**
 * @struct RuntimeParameters
 * @brief 完整的运行时参数集合
 *
 * 涵盖所有子系统的运行参数，从JSON配置文件加载后供各模块查询使用。
 * 包含运动控制（PID增益、PWM限幅、运动超时等）、感知（相机尺寸、分类阈值）、
 * 通信（助理TCP、转向媒体服务）、电源监控等全部可调参数。
 */
struct RuntimeParameters {
    // 运动控制参数
    double running_speed_target = 300.0;  ///< 目标行驶速度（PWM，0~5000）
    double yaw_rate_pid_p = 3.0;          ///< 偏航角速率PID比例增益
    double yaw_rate_pid_i = 0.0;          ///< 偏航角速率PID积分增益
    double yaw_rate_pid_d = 0.0;          ///< 偏航角速率PID微分增益
    int exp_light = 65;                   ///< 相机曝光值

    // 安全与低电压
    int low_voltage_raw_threshold = 200; ///< 低电压原始阈值

    // 控制周期与超时
    int control_period_ms = 5;            ///< 控制周期（毫秒）
    int perception_stale_ms = 120;        ///< 感知数据过期阈值（毫秒）

    // 电机PWM限制
    int pwm_limit = 5000;                 ///< PWM最大绝对值
    int raw_turn_output_limit = 20000;    ///< 原始转向输出限幅
    int pwm_floor = 0;                    ///< PWM最低有效值（低于此值电机不转）
    bool prohibit_reverse_pwm = false;    ///< 是否禁止反转PWM
    int prohibit_reverse_pwm_step_limit = 1000;  ///< 禁止反转时的阶梯限制

    // 运动状态机参数
    int motion_unveto_confirm_cycles = 3;   ///< 解除封锁需要的确认周期数
    int motion_spinup_ms = 800;             ///< 电机启动加速时间（毫秒）
    double motion_turn_limit_spinup = 1.0;  ///< 启动阶段的转向限制
    int motion_pwm_step_limit = 3000;       ///< PWM每步最大变化量
    int motion_stop_ms = 300;               ///< 停止超时时间（毫秒）
    int motion_stop_encoder_threshold = 8;  ///< 停止判定编码器阈值
    int motion_fault_rearm_hold_ms = 600;   ///< 故障后重新就绪等待时间（毫秒）

    // 左右轮独立PID
    WheelPidParameters left_wheel_pid{};    ///< 左轮PID参数
    WheelPidParameters right_wheel_pid{96.0, 2.2, 0.2, 5000.0, 0.4};  ///< 右轮PID参数

    // 调试与通信
    int control_snapshot_emit_interval_ms = 100;  ///< 控制快照输出间隔（毫秒）
    bool assistant_enabled = true;                ///< 是否启用助理连接
    AssistantTcpParameters assistant_tcp{};        ///< 助理TCP参数
    bool steering_media_enabled = true;            ///< 是否启用转向媒体服务
    int steering_media_port = 48012;               ///< 媒体服务端口
    int steering_media_publish_interval_ms = 20;   ///< 媒体发布间隔（毫秒）
    int steering_media_downsample = 1;             ///< 媒体图像下采样因子
    bool steering_media_publish_latest_frame = false;  ///< 诊断开关：优先发布最新相机帧而非严格快照匹配帧
    int steering_media_gray_bits = 2;               ///< 媒体图像灰度位深，支持 1/2/4/8
    bool steering_media_publish_disarmed = true;   ///< 媒体发布是否处于未就绪状态
    int low_voltage_sample_interval_ms = 1000;     ///< 低电压采样间隔（毫秒）

    // 相机参数
    int camera_frame_width = 320;          ///< 相机帧宽度
    int camera_frame_height = 240;         ///< 相机帧高度

    // BEV参数
    BEVProjectorCalibration bev_projector{};             ///< BEV投影器标定参数
    BEVGeometryParameters bev_geometry{};                 ///< BEV几何参数
    BEVClassificationParameters bev_classification{};     ///< BEV分类参数
    BEVControlModelParameters bev_control_model{};        ///< BEV控制模型参数
    BEVElementParameters bev_element{};                   ///< BEV元素检测参数
    BEVElementRasterParameters bev_element_raster{};      ///< BEV元素栅格参数
    ReferenceTimeAlignmentParameters reference_time_alignment{};  ///< 参考时间对齐参数
    CameraSourceParameters camera_source{};                       ///< 相机源参数

    // 状态标记
    bool startup_critical_applied = false;  ///< 启动阶段关键参数是否已应用
    bool loaded_from_defaults = false;      ///< 是否从默认值加载（非JSON文件）
    bool parse_failure = false;             ///< JSON解析是否失败
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_RUNTIME_PARAMETER_TYPES_HPP
