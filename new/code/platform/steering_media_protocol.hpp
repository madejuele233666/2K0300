#ifndef LS2K_PLATFORM_STEERING_MEDIA_PROTOCOL_HPP
#define LS2K_PLATFORM_STEERING_MEDIA_PROTOCOL_HPP

/**
 * @file steering_media_protocol.hpp
 * 转向媒体协议定义 —— 参数快照、图像帧和编码/解码接口。
 * 用于将感知/控制状态和相机帧打包传输到外部媒体系统。
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "port/bev_geometry_types.hpp"
#include "port/bev_element_raster_types.hpp"
#include "port/camera_frame_types.hpp"
#include "port/perception_result.hpp"
#include "port/visual_element_evidence_types.hpp"

namespace ls2k::platform {

/**
 * 计算指定分辨率下灰度图像负载的字节数（宽 * 高）。
 * @param width 图像宽度（像素）
 * @param height 图像高度（像素）
 * @return 负载字节数，若宽或高 <= 0 则返回 0
 */
std::size_t SteeringMediaImagePayloadBytes(int width, int height);

/**
 * 参数快照视图 —— 包含速度目标、陀螺仪偏航 PID 和 BEV 配置的运行时参数快照。
 */
struct SteeringMediaParamSnapshotView {
    /** 运行速度目标值 */
    double running_speed_target = 0.0;
    /** 偏航角速度 PID 比例系数 */
    double yaw_rate_pid_p = 0.0;
    /** 偏航角速度 PID 积分系数 */
    double yaw_rate_pid_i = 0.0;
    /** 偏航角速度 PID 微分系数 */
    double yaw_rate_pid_d = 0.0;
    /** 控制周期（毫秒） */
    int control_period_ms = 0;
    /** 低电压采样间隔（毫秒） */
    int low_voltage_sample_interval_ms = 0;
    /** 低电压原始 ADC 阈值 */
    int low_voltage_raw_threshold = 0;
    /** 原始转向输出限幅值 */
    int raw_turn_output_limit = 0;
    /** BEV 投影校准参数 */
    port::BEVProjectorCalibration bev_projector{};
    /** BEV 几何配置参数 */
    port::BEVGeometryParameters bev_geometry{};
    /** BEV 分类配置参数 */
    port::BEVClassificationParameters bev_classification{};
    /** BEV 控制模型参数 */
    port::BEVControlModelParameters bev_control_model{};
    /** BEV 元素检测参数（含圆形/路口退出） */
    port::BEVElementParameters bev_element{};
    /** BEV 元素栅格化参数 */
    port::BEVElementRasterParameters bev_element_raster{};
};

/**
 * 转向参考视图 —— 描述当前转向参考的模式和数据来源。
 */
struct SteeringMediaReferenceView {
    /** 参考模式（如 "none" / "visual" / "gps" 等） */
    std::string mode = "none";
    /** 参考数据的来源描述 */
    std::string source = "none";
};

/**
 * 转向视觉参考视图 —— 描述当前是否有可用的视觉参考候选。
 */
struct SteeringMediaVisualReferenceView {
    /** 是否存在有效的视觉参考 */
    bool present = false;
    /** 视觉参考的来源描述 */
    std::string source = "none";
    /** 视觉参考不可用或不存在的原因 */
    std::string reason = "no_visual_reference_candidate";
    /** 候选参考的数量 */
    std::uint64_t candidate_count = 0;
    /** 被拒绝的候选参考原因 */
    std::string rejected_candidate_reason = "none";
    /** 本帧构建的候选路径事实集合 */
    port::VisualReferenceCandidatePathSet candidate_paths{};
};

/**
 * 感知健康视图 —— 描述投影仪等感知硬件的健康状态。
 */
struct SteeringMediaPerceptionHealthView {
    /** 投影仪是否工作正常 */
    bool projector_ok = false;
    /** 状态描述或异常原因 */
    std::string reason = "projector_invalid";
};

/**
 * 转向资格视图 —— 描述当前参考是否可用于控制。
 */
struct SteeringMediaEligibilityView {
    /** 参考是否可用于转向控制 */
    bool usable = false;
    /** 前方可用的参考样本数量 */
    std::uint64_t leading_usable_samples = 0;
    /** 前方参考样本的最小前向距离（米） */
    double leading_min_forward_m = 0.0;
    /** 前方参考样本的最大前向距离（米） */
    double leading_max_forward_m = 0.0;
    /** 不可用的原因描述 */
    std::string reason = "no_reference_facts";
};

/**
 * 横向误差视图 —— 描述从参考计算得到的加权横向误差。
 */
struct SteeringMediaLateralErrorView {
    /** 是否已成功计算横向误差 */
    bool computed = false;
    /** 加权横向误差（米） */
    double weighted_lateral_error_m = 0.0;
    /** 参与加权的样本数量 */
    std::uint64_t weighted_sample_count = 0;
    /** 权重总和 */
    double weight_sum = 0.0;
    /** 计算失败的原因描述 */
    std::string reason = "reference_unusable";
};

/**
 * 参考控制视图 —— 描述参考控制系统是否就绪。
 */
struct SteeringMediaReferenceControlView {
    /** 参考控制是否就绪可用 */
    bool ready = false;
    /** 未就绪的原因描述 */
    std::string reason = "reference_unusable";
};

/**
 * 安全门视图 —— 描述安全门禁（veto）是否处于激活状态。
 */
struct SteeringMediaSafetyGateView {
    /** 安全否决是否激活（激活时将阻止控制输出） */
    bool veto_active = true;
    /** 否决激活的原因描述 */
    std::string reason = "perception_stale";
};

/**
 * 降级视图 —— 描述系统是否处于降级运行模式。
 */
struct SteeringMediaDegradedView {
    /** 是否处于降级模式 */
    bool active = false;
    /** 降级原因描述 */
    std::string reason = "none";
};

/**
 * 偏航控制视图 —— 描述偏航控制的目标输出。
 */
struct SteeringMediaYawControlView {
    /** 转向输出目标值 */
    double turn_output_target = 0.0;
};

/**
 * 执行器视图 —— 描述原始和应用后的转向输出值。
 */
struct SteeringMediaActuatorView {
    /** 原始（未限幅的）转向输出值 */
    int raw_turn_output = 0;
    /** 经过限幅和安全处理后的实际应用转向输出值 */
    int applied_turn_output = 0;
};

/**
 * 转向媒体元素证据视图 —— 别名 port::VisualElementEvidenceFrame。
 * 包含路口退出和圆形检测等视觉元素的证据帧数据。
 */
using SteeringMediaElementEvidenceView = port::VisualElementEvidenceFrame;

/**
 * 转向媒体快照视图 —— 包含感知、参考、控制、安全和执行器状态的完整快照。
 */
struct SteeringMediaSnapshotView {
    /** 当前阈值等级（用于调试和可视化） */
    int threshold = 0;
    /** 感知健康状态 */
    SteeringMediaPerceptionHealthView perception_health{};
    /** 视觉元素证据（路口退出/圆形检测） */
    SteeringMediaElementEvidenceView element_evidence{};
    /** CircleV2 场景状态 */
    port::CircleV2TelemetrySnapshot circle_v2{};
    /** 视觉参考候选状态 */
    SteeringMediaVisualReferenceView visual_reference{};
    /** 参考模式与来源 */
    SteeringMediaReferenceView reference{};
    /** 参考可用性资格 */
    SteeringMediaEligibilityView eligibility{};
    /** 加权横向误差 */
    SteeringMediaLateralErrorView lateral_error{};
    /** 参考控制就绪状态 */
    SteeringMediaReferenceControlView reference_control{};
    /** 安全门禁状态 */
    SteeringMediaSafetyGateView safety_gate{};
    /** 降级运行状态 */
    SteeringMediaDegradedView degraded{};
    /** 偏航控制输出 */
    SteeringMediaYawControlView yaw_control{};
    /** 执行器输出 */
    SteeringMediaActuatorView actuator{};
};

/**
 * 转向媒体配置快照 —— 包含发布时间和完整的参数快照。
 */
struct SteeringMediaConfigSnapshot {
    /** 快照发布时间戳（毫秒） */
    std::uint64_t publish_time_ms = 0;
    /** 媒体发布间隔（毫秒） */
    int media_publish_interval_ms = 0;
    /** 运行时参数快照 */
    SteeringMediaParamSnapshotView param_snapshot{};
};

/**
 * 转向媒体图像帧 —— 包含灰度像素数据和关联的转向快照。
 */
struct SteeringMediaImageFrame {
    /** 帧序列 ID */
    std::uint64_t frame_id = 0;
    /** 图像采集时间戳（毫秒） */
    std::uint64_t capture_time_ms = 0;
    /** 图像发布时间戳（毫秒） */
    std::uint64_t publish_time_ms = 0;
    /** 相机采集/提交元数据 */
    port::CameraRawFrameMetadata camera_metadata{};
    /** 相机帧存储健康统计 */
    port::CameraFrameStoreHealth camera_store_health{};
    /** 降采样后的图像宽度（像素） */
    int width = 0;
    /** 降采样后的图像高度（像素） */
    int height = 0;
    /** 原始源图像宽度（像素），0 时与 width 相同 */
    int source_width = 0;
    /** 原始源图像高度（像素），0 时与 height 相同 */
    int source_height = 0;
    /** 原始源图像行跨度（字节），0 时与 source_width 相同 */
    int source_stride = 0;
    /** 降采样系数（1 表示无降采样） */
    int downsample = 1;
    /** 负载像素格式，支持 gray8/gray4/gray2/gray1 */
    const char* pixel_format = "gray8";
    /** 图像源模式，snapshot_aligned 或 latest_camera_frame */
    const char* frame_source = "snapshot_aligned";
    /** steering snapshot 是否与图像帧精确对齐 */
    bool steering_snapshot_aligned = true;
    /** 关联 steering snapshot 的帧 ID */
    std::uint64_t steering_snapshot_frame_id = 0;
    /** 关联 steering snapshot 的采集时间 */
    std::uint64_t steering_snapshot_capture_time_ms = 0;
    /** 当前运动阶段（如 "DISARMED" / "DRIVING"） */
    const char* motion_phase = "DISARMED";
    /** 关联的转向快照数据 */
    SteeringMediaSnapshotView steering_snapshot{};
    /** 灰度像素数据缓冲区指针 */
    const std::uint8_t* pixel_data = nullptr;
    /** 像素数据大小（字节），由 pixel_format 决定 */
    std::size_t pixel_size = 0;
};

/**
 * 编码参数配置快照为媒体信封格式。
 * @param snapshot 参数配置快照
 * @param encoded 输出参数，编码后的完整媒体信封数据
 * @param error 输出参数，编码失败时的错误描述
 * @return true 表示编码成功
 */
bool EncodeSteeringMediaConfigSnapshot(const SteeringMediaConfigSnapshot& snapshot,
                                       std::vector<std::uint8_t>& encoded,
                                       std::string& error);

/**
 * 编码图像帧为媒体信封格式（JSON 头部 + 灰度像素负载）。
 * @param frame 图像帧数据（含像素数据和元信息）
 * @param encoded 输出参数，编码后的完整媒体信封数据
 * @param error 输出参数，编码失败时的错误描述
 * @return true 表示编码成功
 */
bool EncodeSteeringMediaImageFrame(const SteeringMediaImageFrame& frame,
                                   std::vector<std::uint8_t>& encoded,
                                   std::string& error);

/**
 * 解码媒体信封 —— 解析 8 字节长度前缀 + JSON 头部 + 二进制负载。
 * @param data 原始媒体信封数据缓冲区
 * @param size 数据总大小（字节）
 * @param header_json 输出参数，解析得到的 JSON 头部字符串
 * @param payload 输出参数，解析得到的二进制负载数据
 * @param error 输出参数，解码失败时的错误描述
 * @return true 表示解码成功
 */
bool DecodeSteeringMediaEnvelope(const std::uint8_t* data,
                                 std::size_t size,
                                 std::string& header_json,
                                 std::vector<std::uint8_t>& payload,
                                 std::string& error);

/**
 * 校验图像负载尺寸是否与声明的分辨率一致。
 * @param width 声明的图像宽度
 * @param height 声明的图像高度
 * @param payload_size 实际负载大小（字节）
 * @param error 输出参数，校验失败时的错误描述
 * @return true 表示校验通过
 */
bool ValidateSteeringMediaImagePayload(int width,
                                       int height,
                                       std::size_t payload_size,
                                       std::string& error);

bool ValidateSteeringMediaImagePayload(int width,
                                       int height,
                                       const char* pixel_format,
                                       std::size_t payload_size,
                                       std::string& error);

}  // namespace ls2k::platform

#endif  // LS2K_PLATFORM_STEERING_MEDIA_PROTOCOL_HPP
