#ifndef LS2K_PLATFORM_TRUE_LS2K0300_STEERING_MEDIA_BRIDGE_HPP
#define LS2K_PLATFORM_TRUE_LS2K0300_STEERING_MEDIA_BRIDGE_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace ls2k::platform::true_ls2k0300 {

// 转向媒体桥 TCP 连接配置
struct SteeringMediaBridgeConfig {
    std::string host;   // 上位机主机名或 IP 地址
    int port = 0;       // TCP 端口号
};

// 转向媒体桥连接状态枚举
enum class SteeringMediaBridgeState {
    kUnconfigured = 0,   // 未配置
    kDisconnected,       // 已配置但未连接
    kConnecting,         // 正在建立 TCP 连接
    kReady,              // 连接就绪，可收发数据
    kBackoff,            // 连接失败，等待重试回退
};

// 转向媒体桥轮询结果结构体
struct SteeringMediaBridgePollResult {
    SteeringMediaBridgeState state = SteeringMediaBridgeState::kUnconfigured;  // 当前连接状态
    bool state_changed = false;  // 状态是否发生变化
    std::string detail;          // 状态详细描述
};

// 转向媒体桥发送结果枚举
enum class SteeringMediaBridgeSendResult {
    kSent = 0,               // 数据已全部发送完毕
    kAcceptedInFlight,       // 数据已加入待发送缓冲但尚未全部发送
    kBusyRejected,           // 发送通道忙，拒绝本次发送
    kDisconnected,           // 连接已断开，无法发送
    kError,                  // 发送过程中发生错误
};

// 初始化转向媒体桥 —— 配置 TCP 连接参数
// @param config 连接配置（主机和端口）
// @param[out] detail 初始化结果描述
// @return true 配置成功，false 配置无效
bool InitializeSteeringMediaBridge(const SteeringMediaBridgeConfig& config, std::string& detail);

// 轮询转向媒体桥状态机 —— 驱动连接/重连/缓冲刷新/健康检查
// @return 轮询结果（状态和变更标记）
SteeringMediaBridgePollResult PollSteeringMediaBridge();

// 检查转向媒体桥是否已就绪
// @return true 已连接就绪，false 未就绪
bool SteeringMediaBridgeReady();

// 发送转向媒体数据 —— 支持缓冲排队，忙时拒收新帧
// @param data 待发送数据缓冲区
// @param length 数据长度
// @param[out] detail 发送结果描述
// @return 发送结果枚举（kSent/kAcceptedInFlight/kBusyRejected/kDisconnected/kError）
SteeringMediaBridgeSendResult SendSteeringMediaBytes(const std::uint8_t* data,
                                                     std::size_t length,
                                                     std::string& detail);

}  // namespace ls2k::platform::true_ls2k0300

#endif  // LS2K_PLATFORM_TRUE_LS2K0300_STEERING_MEDIA_BRIDGE_HPP
