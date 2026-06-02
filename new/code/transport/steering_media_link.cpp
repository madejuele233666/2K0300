#include "transport/steering_media_link.hpp"

#include <utility>

#include "platform/true_ls2k0300/steering_media_bridge.hpp"
#include "port/perf_counter.hpp"

namespace ls2k::transport {
namespace {

/**
 * 桥接转向媒体传输实现 —— 通过 true_ls2k0300 桥接层与外部媒体系统通信。
 */
class BridgeSteeringMediaTransport final : public ISteeringMediaTransport {
public:
    /**
     * 初始化桥接传输层。
     * @param config 传输配置（主机和端口）
     * @param detail 输出参数，初始化详情
     * @return true 表示初始化成功
     */
    bool Initialize(const SteeringMediaTransportConfig& config, std::string& detail) override {
        ::ls2k::platform::true_ls2k0300::SteeringMediaBridgeConfig bridge_config{};
        bridge_config.host = config.host;
        bridge_config.port = config.port;
        return ::ls2k::platform::true_ls2k0300::InitializeSteeringMediaBridge(bridge_config, detail);
    }

    /**
     * 轮询桥接传输层状态。
     * @return 传输层轮询结果，包含连接状态和变化标志
     */
    SteeringMediaTransportPollResult Poll() override {
        const ::ls2k::platform::true_ls2k0300::SteeringMediaBridgePollResult result =
            ::ls2k::platform::true_ls2k0300::PollSteeringMediaBridge();
        SteeringMediaTransportPollResult view{};
        switch (result.state) {
            case ::ls2k::platform::true_ls2k0300::SteeringMediaBridgeState::kUnconfigured:
                view.state = SteeringMediaTransportState::kUnconfigured;
                break;
            case ::ls2k::platform::true_ls2k0300::SteeringMediaBridgeState::kDisconnected:
                view.state = SteeringMediaTransportState::kDisconnected;
                break;
            case ::ls2k::platform::true_ls2k0300::SteeringMediaBridgeState::kConnecting:
                view.state = SteeringMediaTransportState::kConnecting;
                break;
            case ::ls2k::platform::true_ls2k0300::SteeringMediaBridgeState::kReady:
                view.state = SteeringMediaTransportState::kReady;
                break;
            case ::ls2k::platform::true_ls2k0300::SteeringMediaBridgeState::kBackoff:
                view.state = SteeringMediaTransportState::kBackoff;
                break;
        }
        view.state_changed = result.state_changed;
        view.detail = result.detail;
        return view;
    }

    /**
     * 查询桥接传输层是否已就绪。
     * @return true 表示传输层已连接可用
     */
    bool Ready() const override {
        return ::ls2k::platform::true_ls2k0300::SteeringMediaBridgeReady();
    }

    /**
     * 通过桥接层发送二进制数据。
     * @param data 数据缓冲区指针
     * @param length 数据长度（字节）
     * @param detail 输出参数，发送结果详情
     * @return 发送结果枚举
     */
    SteeringMediaTransportSendResult SendBytes(const std::uint8_t* data,
                                               std::size_t length,
                                               std::string& detail) override {
        switch (::ls2k::platform::true_ls2k0300::SendSteeringMediaBytes(data, length, detail)) {
            case ::ls2k::platform::true_ls2k0300::SteeringMediaBridgeSendResult::kSent:
                return SteeringMediaTransportSendResult::kSent;
            case ::ls2k::platform::true_ls2k0300::SteeringMediaBridgeSendResult::kAcceptedInFlight:
                return SteeringMediaTransportSendResult::kAcceptedInFlight;
            case ::ls2k::platform::true_ls2k0300::SteeringMediaBridgeSendResult::kBusyRejected:
                return SteeringMediaTransportSendResult::kBusyRejected;
            case ::ls2k::platform::true_ls2k0300::SteeringMediaBridgeSendResult::kDisconnected:
                return SteeringMediaTransportSendResult::kDisconnected;
            case ::ls2k::platform::true_ls2k0300::SteeringMediaBridgeSendResult::kError:
                return SteeringMediaTransportSendResult::kError;
        }
        return SteeringMediaTransportSendResult::kError;
    }
};

/**
 * 将转向媒体传输状态转换为诊断事件代码字符串。
 * @param state 传输状态
 * @return 对应的诊断事件代码
 */
const char* ToStateMarker(SteeringMediaTransportState state) {
    switch (state) {
        case SteeringMediaTransportState::kUnconfigured:
            return "steering_media.unconfigured";
        case SteeringMediaTransportState::kDisconnected:
            return "steering_media.disconnected";
        case SteeringMediaTransportState::kConnecting:
            return "steering_media.connecting";
        case SteeringMediaTransportState::kReady:
            return "steering_media.connected";
        case SteeringMediaTransportState::kBackoff:
            return "steering_media.backoff";
    }
    return "steering_media.unknown";
}

/**
 * 将转向媒体传输状态映射为诊断级别。
 * @param state 传输状态
 * @return 对应的诊断级别（kInfo 或 kWarning）
 */
port::DiagnosticLevel ToStateLevel(SteeringMediaTransportState state) {
    switch (state) {
        case SteeringMediaTransportState::kReady:
        case SteeringMediaTransportState::kConnecting:
            return port::DiagnosticLevel::kInfo;
        case SteeringMediaTransportState::kUnconfigured:
        case SteeringMediaTransportState::kDisconnected:
        case SteeringMediaTransportState::kBackoff:
            return port::DiagnosticLevel::kWarning;
    }
    return port::DiagnosticLevel::kWarning;
}

}  // namespace

/**
 * 构造 SteeringMediaLink —— 使用默认的 BridgeSteeringMediaTransport 作为传输层。
 */
SteeringMediaLink::SteeringMediaLink()
    : SteeringMediaLink(std::make_unique<BridgeSteeringMediaTransport>()) {}

/**
 * 构造 SteeringMediaLink —— 使用指定的传输层实现。
 * @param transport 传输层实例的唯一指针
 */
SteeringMediaLink::SteeringMediaLink(std::unique_ptr<ISteeringMediaTransport> transport)
    : transport_(std::move(transport)) {}

/**
 * 初始化转向媒体链路。
 * 根据运行时参数中的 steering_media_enabled 决定是否启用。
 * 使用 assistant_tcp.host 和 steering_media_port 配置 TCP 端点。
 * @param params 运行时参数
 * @param diagnostics 诊断输出接收器
 * @return true 表示初始化成功，false 表示媒体被禁用或初始化失败
 */
bool SteeringMediaLink::Initialize(const port::RuntimeParameters& params, port::DiagnosticSink& diagnostics) {
    configured_ = true;
    ready_ = false;
    last_state_code_ = static_cast<int>(SteeringMediaTransportState::kUnconfigured);
    pending_image_.clear();
    if (!params.steering_media_enabled) {
        return false;
    }

    SteeringMediaTransportConfig config{};
    config.host = params.assistant_tcp.host;
    config.port = params.steering_media_port;

    std::string detail;
    const bool ok = transport_ != nullptr && transport_->Initialize(config, detail);
    diagnostics.Emit({ok ? port::DiagnosticLevel::kInfo : port::DiagnosticLevel::kWarning,
                      ok ? "steering_media.configured" : "steering_media.config.failed",
                      ok ? "steering media configured for TCP endpoint " + config.host + ":" +
                               std::to_string(config.port)
                         : "steering media unavailable: " + detail,
                      port::NowMs()});
    return ok;
}

/**
 * 轮询转向媒体链路状态。
 * 更新就绪标志，检测连接状态变化和连接丢失事件。
 * 连接丢失时自动清除待发送的排队图像。
 * @param diagnostics 诊断输出接收器
 * @return 链路轮询结果
 */
SteeringMediaLinkPollResult SteeringMediaLink::Poll(port::DiagnosticSink& diagnostics) {
    SteeringMediaLinkPollResult poll_result{};
    if (!configured_ || transport_ == nullptr) {
        return poll_result;
    }
    const bool was_ready = ready_;
    const SteeringMediaTransportPollResult result = transport_->Poll();
    ready_ = result.state == SteeringMediaTransportState::kReady;
    poll_result.ready = ready_;
    poll_result.became_ready = !was_ready && ready_;
    poll_result.connection_lost = was_ready && !ready_;
    if (poll_result.connection_lost) {
        pending_image_.clear();
    }
    if (result.state_changed || last_state_code_ != static_cast<int>(result.state)) {
        last_state_code_ = static_cast<int>(result.state);
        diagnostics.Emit({ToStateLevel(result.state),
                          ToStateMarker(result.state),
                          result.detail,
                          port::NowMs()});
    }
    return poll_result;
}

/**
 * 发送已编码的数据负载（内部方法）。
 * 在性能探测范围内执行发送操作。
 * 发送结果为断开或错误时更新就绪状态并发出诊断事件。
 * @param encoded 已编码的完整媒体信封数据
 * @param diagnostic_code 诊断事件代码（用于发送失败时的诊断）
 * @param diagnostics 诊断输出接收器
 * @return 传输发送结果
 */
SteeringMediaTransportSendResult SteeringMediaLink::PublishEncoded(
    const std::vector<std::uint8_t>& encoded,
    const char* diagnostic_code,
    port::DiagnosticSink& diagnostics) {
    if (!ready_ || transport_ == nullptr) {
        return SteeringMediaTransportSendResult::kDisconnected;
    }
    std::string detail;
    SteeringMediaTransportSendResult send_result = SteeringMediaTransportSendResult::kError;
    {
        LS2K_PERF_SCOPE(port::PerfStage::kMediaSend);
        send_result = transport_->SendBytes(encoded.data(), encoded.size(), detail);
    }
    if (send_result != SteeringMediaTransportSendResult::kSent &&
        send_result != SteeringMediaTransportSendResult::kAcceptedInFlight &&
        send_result != SteeringMediaTransportSendResult::kBusyRejected) {
        ready_ = transport_->Ready();
    }
    if (send_result == SteeringMediaTransportSendResult::kDisconnected ||
        send_result == SteeringMediaTransportSendResult::kError) {
        diagnostics.Emit({port::DiagnosticLevel::kWarning,
                          diagnostic_code,
                          detail,
                          port::NowMs()});
    }
    return send_result;
}

/**
 * 发布参数配置快照。
 * 先编码快照为媒体信封格式，再通过传输层发送。
 * @param snapshot 参数配置快照数据
 * @param diagnostics 诊断输出接收器
 * @return true 表示快照已成功发送或已被接收在途
 */
bool SteeringMediaLink::PublishConfigSnapshot(const SteeringMediaConfigSnapshot& snapshot,
                                              port::DiagnosticSink& diagnostics) {
    std::vector<std::uint8_t> encoded;
    std::string error;
    {
        LS2K_PERF_SCOPE(port::PerfStage::kMediaEncode);
        if (!EncodeSteeringMediaConfigSnapshot(snapshot, encoded, error)) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "steering_media.config_snapshot.invalid",
                              error,
                              port::NowMs()});
            return false;
        }
    }
    const SteeringMediaTransportSendResult send_result =
        PublishEncoded(encoded, "steering_media.config_snapshot.failed", diagnostics);
    return send_result == SteeringMediaTransportSendResult::kSent ||
           send_result == SteeringMediaTransportSendResult::kAcceptedInFlight;
}

/**
 * 发布图像帧。
 * 先编码图像帧为媒体信封格式，然后尝试发送。
 * 如果传输层忙碌（kBusyRejected），则暂存图像数据等待稍后刷新。
 * 如果已有待发送的图像排队，则直接替换为最新帧。
 * @param frame 图像帧数据
 * @param diagnostics 诊断输出接收器
 * @return 发布结果（已发送/已排队/不可用）
 */
SteeringMediaPublishResult SteeringMediaLink::PublishImageFrame(const SteeringMediaImageFrame& frame,
                                                                port::DiagnosticSink& diagnostics) {
    std::vector<std::uint8_t> encoded;
    std::string error;
    {
        LS2K_PERF_SCOPE(port::PerfStage::kMediaEncode);
        if (!EncodeSteeringMediaImageFrame(frame, encoded, error)) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "steering_media.image_frame.invalid",
                              error,
                              port::NowMs()});
            return SteeringMediaPublishResult::kUnavailable;
        }
    }
    if (!ready_) {
        return SteeringMediaPublishResult::kUnavailable;
    }
    if (!pending_image_.empty()) {
        pending_image_ = std::move(encoded);
        return SteeringMediaPublishResult::kQueued;
    }
    const SteeringMediaTransportSendResult send_result =
        PublishEncoded(encoded, "steering_media.image_frame.failed", diagnostics);
    if (send_result == SteeringMediaTransportSendResult::kSent) {
        return SteeringMediaPublishResult::kSent;
    }
    if (send_result == SteeringMediaTransportSendResult::kAcceptedInFlight) {
        return SteeringMediaPublishResult::kQueued;
    }
    if (send_result == SteeringMediaTransportSendResult::kBusyRejected) {
        pending_image_ = std::move(encoded);
        return SteeringMediaPublishResult::kQueued;
    }
    return SteeringMediaPublishResult::kUnavailable;
}

/**
 * 刷新待发送的排队图像。
 * 尝试发送之前因链路忙碌而暂存的图像数据。
 * 发送成功或连接已断开时清除排队数据。
 * @param diagnostics 诊断输出接收器
 * @return true 表示排队图像已成功发送或接收在途
 */
bool SteeringMediaLink::FlushPendingImage(port::DiagnosticSink& diagnostics) {
    if (pending_image_.empty()) {
        return false;
    }
    const SteeringMediaTransportSendResult send_result =
        PublishEncoded(pending_image_, "steering_media.image_frame.failed", diagnostics);
    if (send_result == SteeringMediaTransportSendResult::kSent ||
        send_result == SteeringMediaTransportSendResult::kAcceptedInFlight) {
        pending_image_.clear();
        return true;
    }
    if (send_result != SteeringMediaTransportSendResult::kBusyRejected) {
        if (!ready_) {
            pending_image_.clear();
        }
        return false;
    }
    return false;
}

/**
 * 查询转向媒体链路是否已就绪。
 * @return true 表示链路已连接并可用于发送数据
 */
bool SteeringMediaLink::Ready() const {
    return ready_;
}

}  // namespace ls2k::transport
