#ifndef LS2K_PLATFORM_STEERING_MEDIA_LINK_HPP
#define LS2K_PLATFORM_STEERING_MEDIA_LINK_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "platform/steering_media_protocol.hpp"
#include "port/diagnostics.hpp"
#include "port/runtime_parameter_types.hpp"

namespace ls2k::platform {

/**
 * 转向媒体传输配置 —— 定义 TCP 连接的目标地址和端口。
 */
struct SteeringMediaTransportConfig {
    /** 目标主机名或 IP 地址 */
    std::string host;
    /** 目标 TCP 端口号 */
    int port = 0;
};

/**
 * 转向媒体传输状态枚举。
 */
enum class SteeringMediaTransportState {
    /** 尚未配置连接参数 */
    kUnconfigured = 0,
    /** 已配置但未连接 */
    kDisconnected,
    /** 正在尝试建立连接 */
    kConnecting,
    /** 连接已建立，可以发送数据 */
    kReady,
    /** 连接失败，处于退避重试状态 */
    kBackoff,
};

/**
 * 转向媒体传输轮询结果 —— 包含当前传输状态和变化标志。
 */
struct SteeringMediaTransportPollResult {
    /** 当前传输状态 */
    SteeringMediaTransportState state = SteeringMediaTransportState::kUnconfigured;
    /** 状态是否在上次轮询后发生了改变 */
    bool state_changed = false;
    /** 与当前状态相关的详细信息（如错误描述） */
    std::string detail;
};

/**
 * 转向媒体数据发送结果枚举。
 */
enum class SteeringMediaTransportSendResult {
    /** 数据已成功发送 */
    kSent = 0,
    /** 已接收但尚在飞行中（未完全确认） */
    kAcceptedInFlight,
    /** 发送方忙，请求被拒绝 */
    kBusyRejected,
    /** 连接已断开，无法发送 */
    kDisconnected,
    /** 发送过程中发生错误 */
    kError,
};

/**
 * 转向媒体传输接口 —— 抽象底层传输层（TCP/桥接等）。
 */
class ISteeringMediaTransport {
public:
    virtual ~ISteeringMediaTransport() = default;

    /**
     * 初始化传输层连接。
     * @param config 传输配置（主机和端口）
     * @param detail 输出参数，初始化结果详情
     * @return true 表示初始化成功
     */
    virtual bool Initialize(const SteeringMediaTransportConfig& config, std::string& detail) = 0;

    /**
     * 轮询传输状态。
     * @return 当前传输轮询结果（含状态和变化信息）
     */
    virtual SteeringMediaTransportPollResult Poll() = 0;

    /**
     * 查询传输层是否已就绪。
     * @return true 表示传输层已连接并可用于发送
     */
    virtual bool Ready() const = 0;

    /**
     * 发送二进制数据。
     * @param data 数据缓冲区指针
     * @param length 数据长度（字节）
     * @param detail 输出参数，发送结果详情
     * @return 发送结果枚举
     */
    virtual SteeringMediaTransportSendResult SendBytes(const std::uint8_t* data,
                                                       std::size_t length,
                                                       std::string& detail) = 0;
};

/**
 * 转向媒体链路的轮询结果 —— 包含连接状态变化信息。
 */
struct SteeringMediaLinkPollResult {
    /** 当前链路是否就绪 */
    bool ready = false;
    /** 本次轮询是否刚变为就绪状态 */
    bool became_ready = false;
    /** 本次轮询是否检测到连接丢失 */
    bool connection_lost = false;
};

/**
 * 转向媒体发布结果枚举。
 */
enum class SteeringMediaPublishResult {
    /** 链路不可用，无法发布 */
    kUnavailable,
    /** 数据已成功发送 */
    kSent,
    /** 数据已排队等待发送 */
    kQueued,
};

/**
 * 转向媒体链路 —— 管理转向媒体数据的传输生命周期。
 * 负责初始化传输层、轮询连接状态、编码和发布参数快照与图像帧。
 */
class SteeringMediaLink {
public:
    /** 使用默认的 BridgeSteeringMediaTransport 创建链路 */
    SteeringMediaLink();

    /**
     * 使用自定义传输层实现创建链路（用于测试和注入）。
     * @param transport 传输层实例的唯一指针
     */
    explicit SteeringMediaLink(std::unique_ptr<ISteeringMediaTransport> transport);

    /**
     * 初始化转向媒体链路。
     * 根据运行时参数中的 steering_media_enabled 配置决定是否初始化。
     * @param params 运行时参数（含主机、端口、启用标志）
     * @param diagnostics 诊断输出接收器
     * @return true 表示初始化成功
     */
    bool Initialize(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics);

    /**
     * 轮询传输层状态并更新就绪标志。
     * 连接丢失时自动清除待发送的待处理图像。
     * @param diagnostics 诊断输出接收器
     * @return 链路轮询结果（含就绪状态和变化信息）
     */
    SteeringMediaLinkPollResult Poll(port::DiagnosticSink& diagnostics);

    /**
     * 发布参数配置快照。
     * 先编码为媒体信封格式，再通过传输层发送。
     * @param snapshot 参数快照数据
     * @param diagnostics 诊断输出接收器
     * @return true 表示快照已发送或已接收在途
     */
    bool PublishConfigSnapshot(const SteeringMediaConfigSnapshot& snapshot,
                               port::DiagnosticSink& diagnostics);

    /**
     * 发布图像帧。
     * 如果当前链路忙碌，图像将被排队等待稍后发送。
     * @param frame 图像帧数据（含像素数据和元信息）
     * @param diagnostics 诊断输出接收器
     * @return 发布结果（已发送/已排队/不可用）
     */
    SteeringMediaPublishResult PublishImageFrame(const SteeringMediaImageFrame& frame,
                                                 port::DiagnosticSink& diagnostics);

    /**
     * 刷新待发送的排队图像。
     * 尝试发送之前排队失败的图像帧。
     * @param diagnostics 诊断输出接收器
     * @return true 表示排队图像已成功发送或接收在途
     */
    bool FlushPendingImage(port::DiagnosticSink& diagnostics);

    /** @return true 表示链路已就绪可用 */
    bool Ready() const;

private:
    /**
     * 发送已编码的数据负载。
     * @param encoded 已编码的完整媒体信封（含头部和负载）
     * @param diagnostic_code 诊断事件代码
     * @param diagnostics 诊断输出接收器
     * @return 传输发送结果
     */
    SteeringMediaTransportSendResult PublishEncoded(const std::vector<std::uint8_t>& encoded,
                                                    const char* diagnostic_code,
                                                    port::DiagnosticSink& diagnostics);

    /** 是否已完成配置 */
    bool configured_ = false;
    /** 传输层是否已就绪 */
    bool ready_ = false;
    /** 上一次传输状态码，用于检测状态变化 */
    int last_state_code_ = -1;
    /** 待发送的排队图像数据（当链路忙时暂存） */
    std::vector<std::uint8_t> pending_image_{};
    /** 底层传输层实现 */
    std::unique_ptr<ISteeringMediaTransport> transport_{};
};

}  // namespace ls2k::platform

#endif  // LS2K_PLATFORM_STEERING_MEDIA_LINK_HPP
