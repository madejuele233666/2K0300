#ifndef LS2K_PLATFORM_TRUE_LS2K0300_ASSISTANT_BRIDGE_HPP
#define LS2K_PLATFORM_TRUE_LS2K0300_ASSISTANT_BRIDGE_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace ls2k::platform::true_ls2k0300 {

// 辅助桥 TCP 连接配置
struct AssistantBridgeConfig {
    std::string host;   // 上位机主机名或 IP 地址
    int port = 0;       // TCP 端口号
};

// 辅助桥连接状态枚举
enum class AssistantBridgeState {
    kUnconfigured = 0,   // 未配置
    kDisconnected,       // 已配置但未连接
    kConnecting,         // 正在建立 TCP 连接
    kReady,              // 连接就绪，可收发数据
    kBackoff,            // 连接失败，等待重试回退
};

// 辅助桥轮询结果结构体
struct AssistantBridgePollResult {
    AssistantBridgeState state = AssistantBridgeState::kUnconfigured;  // 当前连接状态
    bool state_changed = false;  // 状态是否发生变化（供外部检测）
    std::string detail;          // 状态详细描述
    std::string received_bytes;  // 本次轮询期间接收到的待处理数据
};

// 初始化辅助桥 —— 配置 TCP 连接参数并绑定底层传输接口
// @param config 连接配置（主机和端口）
// @param[out] detail 初始化结果描述
// @return true 配置成功，false 配置无效
bool InitializeAssistantBridge(const AssistantBridgeConfig& config, std::string& detail);

// 轮询辅助桥状态机 —— 驱动连接/重连/数据接收流程
// @return 轮询结果（状态、变更标记、接收数据）
AssistantBridgePollResult PollAssistantBridge();

// 检查辅助桥是否已就绪
// @return true 已连接就绪，false 未就绪
bool AssistantBridgeReady();

// 通过辅助桥发送数据字节
// @param data 待发送数据缓冲区
// @param length 数据长度
// @param reliable 是否可靠发送（true=等待可写重试，false=忙时丢弃）
// @param[out] detail 发送结果描述
// @return true 发送成功，false 发送失败
bool SendAssistantBytes(const std::uint8_t* data, std::size_t length, bool reliable, std::string& detail);
}  // namespace ls2k::platform::true_ls2k0300

#endif  // LS2K_PLATFORM_TRUE_LS2K0300_ASSISTANT_BRIDGE_HPP
