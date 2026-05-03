// 辅助桥接实现 —— 基于 TCP 的远程辅助通信桥。
// 管理与外部上位机（如调参控制台）的连接、重连、数据收发。
// 封装 seekfree_assistant 协议，支持波形发送和图像传输。

#include "platform/true_ls2k0300/assistant_bridge.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>
#include <netinet/tcp.h>

#include "seekfree_assistant.h"
#include "seekfree_assistant_interface.h"

namespace ls2k::platform::true_ls2k0300 {
namespace {

constexpr uint64_t kReconnectBackoffMs = 1000;
constexpr uint64_t kReliableSendTimeoutMs = 75;
constexpr std::size_t kMaxReceiveDrainBytesPerPoll = 8192;
// Give the board TCP stack enough time to retransmit a just-sent small
// control frame before user space tears the socket down on a spurious
// receive-side close/error report.
constexpr uint64_t kPostSendDisconnectGuardMs = 1000;

// I/O 状态枚举
enum class IoStatus {
    kOk = 0,
    kWouldBlock,
    kClosed,
    kError,
};

// 发送策略 —— 丢弃忙帧或可靠重试
enum class SendPolicy {
    kDropIfBusy = 0,
    kReliable,
};

// 辅助桥全局上下文 —— 连接状态、套接字、I/O 统计和待接收数据
struct AssistantBridgeContext {
    AssistantBridgeConfig config{};
    AssistantBridgeState state = AssistantBridgeState::kUnconfigured;
    bool interface_bound = false;
    int socket_fd = -1;
    uint64_t next_retry_at_ms = 0;
    bool state_dirty = false;
    std::string detail = "assistant bridge not configured";
    IoStatus last_io_status = IoStatus::kOk;
    std::string last_io_detail;
    std::string received_bytes_pending;
    std::size_t total_sent_bytes = 0;
    std::size_t total_received_bytes = 0;
    std::size_t last_sent_bytes = 0;
    std::size_t last_received_bytes = 0;
    uint64_t last_send_at_ms = 0;
    uint64_t last_recv_at_ms = 0;
    SendPolicy active_send_policy = SendPolicy::kDropIfBusy;
};

AssistantBridgeContext g_bridge{};

// 获取单调时钟当前毫秒数
uint64_t MonotonicNowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

// 关闭并清理套接字
void CloseSocket() {
    if (g_bridge.socket_fd >= 0) {
        close(g_bridge.socket_fd);
        g_bridge.socket_fd = -1;
    }
}

// 状态迁移 —— 标记 state_dirty 供外部轮询时捕获
void TransitionTo(AssistantBridgeState state, std::string detail) {
    if (g_bridge.state != state || g_bridge.detail != detail) {
        g_bridge.state_dirty = true;
    }
    g_bridge.state = state;
    g_bridge.detail = std::move(detail);
}

// 进入回退状态 —— 关闭套接字并设置下次重试时间
void EnterBackoff(const std::string& detail) {
    CloseSocket();
    g_bridge.next_retry_at_ms = MonotonicNowMs() + kReconnectBackoffMs;
    TransitionTo(AssistantBridgeState::kBackoff, detail);
}

// 设置套接字为非阻塞模式
bool EnsureNonBlocking(int socket_fd, std::string& detail) {
    const int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0) {
        detail = "fcntl(F_GETFL) failed: " + std::string(std::strerror(errno));
        return false;
    }
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        detail = "fcntl(F_SETFL) failed: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

// 启用 TCP_NODELAY 禁用 Nagle 算法降低延迟
bool EnableLowLatencySocket(int socket_fd, std::string& detail) {
    const int enabled = 1;
    if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) < 0) {
        detail = "setsockopt(TCP_NODELAY) failed: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

// 重置 I/O 状态记录
void ResetIoStatus() {
    g_bridge.last_io_status = IoStatus::kOk;
    g_bridge.last_io_detail.clear();
}

// 描述最近 I/O 统计信息（发送/接收字节数和距上次时间）
std::string DescribeRecentIo() {
    const uint64_t now_ms = MonotonicNowMs();
    const uint64_t send_age_ms =
        g_bridge.last_send_at_ms == 0 || now_ms < g_bridge.last_send_at_ms ? 0 : now_ms - g_bridge.last_send_at_ms;
    const uint64_t recv_age_ms =
        g_bridge.last_recv_at_ms == 0 || now_ms < g_bridge.last_recv_at_ms ? 0 : now_ms - g_bridge.last_recv_at_ms;
    return " last_send_bytes=" + std::to_string(g_bridge.last_sent_bytes) +
           " last_send_age_ms=" + std::to_string(send_age_ms) +
           " total_sent_bytes=" + std::to_string(g_bridge.total_sent_bytes) +
           " last_recv_bytes=" + std::to_string(g_bridge.last_received_bytes) +
           " last_recv_age_ms=" + std::to_string(recv_age_ms) +
           " total_recv_bytes=" + std::to_string(g_bridge.total_received_bytes);
}

// 转换 TCP 状态编号为可读名称
const char* ToTcpStateName(uint8_t state) {
    switch (state) {
        case 1:
            return "ESTABLISHED";
        case 2:
            return "SYN_SENT";
        case 3:
            return "SYN_RECV";
        case 4:
            return "FIN_WAIT1";
        case 5:
            return "FIN_WAIT2";
        case 6:
            return "TIME_WAIT";
        case 7:
            return "CLOSE";
        case 8:
            return "CLOSE_WAIT";
        case 9:
            return "LAST_ACK";
        case 10:
            return "LISTEN";
        case 11:
            return "CLOSING";
        default:
            return "UNKNOWN";
    }
}

// 描述套接字本地和远端地址端口信息
std::string DescribeSocketEndpoints() {
    if (g_bridge.socket_fd < 0) {
        return " fd=closed";
    }

    sockaddr_in local_addr{};
    socklen_t local_len = sizeof(local_addr);
    sockaddr_in peer_addr{};
    socklen_t peer_len = sizeof(peer_addr);
    char local_ip[INET_ADDRSTRLEN] = {};
    char peer_ip[INET_ADDRSTRLEN] = {};
    std::string detail;
    if (getsockname(g_bridge.socket_fd, reinterpret_cast<sockaddr*>(&local_addr), &local_len) == 0 &&
        inet_ntop(AF_INET, &local_addr.sin_addr, local_ip, sizeof(local_ip)) != nullptr) {
        detail += " local=" + std::string(local_ip) + ":" + std::to_string(ntohs(local_addr.sin_port));
    }
    if (getpeername(g_bridge.socket_fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_len) == 0 &&
        inet_ntop(AF_INET, &peer_addr.sin_addr, peer_ip, sizeof(peer_ip)) != nullptr) {
        detail += " peer=" + std::string(peer_ip) + ":" + std::to_string(ntohs(peer_addr.sin_port));
    }
    return detail;
}

// 读取 TCP_INFO 和发送队列深度描述连接健康状态
std::string DescribeTcpInfo() {
    if (g_bridge.socket_fd < 0) {
        return {};
    }

    std::string detail;
#ifdef TCP_INFO
    tcp_info info{};
    socklen_t info_len = sizeof(info);
    if (getsockopt(g_bridge.socket_fd, IPPROTO_TCP, TCP_INFO, &info, &info_len) == 0) {
        detail += " tcp_state=" + std::string(ToTcpStateName(info.tcpi_state));
        detail += " unacked=" + std::to_string(info.tcpi_unacked);
        detail += " retrans=" + std::to_string(info.tcpi_retransmits);
        detail += " probes=" + std::to_string(info.tcpi_probes);
        detail += " backoff=" + std::to_string(info.tcpi_backoff);
        detail += " rto_us=" + std::to_string(info.tcpi_rto);
        detail += " ato_us=" + std::to_string(info.tcpi_ato);
        detail += " snd_mss=" + std::to_string(info.tcpi_snd_mss);
        detail += " rcv_mss=" + std::to_string(info.tcpi_rcv_mss);
        detail += " last_data_sent_ms=" + std::to_string(info.tcpi_last_data_sent);
        detail += " last_data_recv_ms=" + std::to_string(info.tcpi_last_data_recv);
    }
#endif

#ifdef TIOCOUTQ
    int outq = 0;
    if (ioctl(g_bridge.socket_fd, TIOCOUTQ, &outq) == 0) {
        detail += " outq_bytes=" + std::to_string(outq);
    }
#endif

    return detail;
}

// 组合完整的套接字状态描述（I/O 统计 + 端点 + TCP 信息）
std::string DescribeFullSocketState() {
    return DescribeRecentIo() + DescribeSocketEndpoints() + DescribeTcpInfo();
}

// 记录 I/O 状态 —— 仅记录非 OK 状态，避免冲掉已有错误
void RecordIoStatus(IoStatus status, const std::string& detail) {
    if (status == IoStatus::kOk) {
        return;
    }
    if (g_bridge.last_io_status == IoStatus::kOk || status == IoStatus::kClosed || status == IoStatus::kError) {
        g_bridge.last_io_status = status;
        g_bridge.last_io_detail = detail;
    }
}

// 套接字故障处理 —— 直接进入回退状态
void HandleSocketFailure(const std::string& detail) {
    EnterBackoff(detail);
}

// 延迟断开判决 —— 发送后短时内收到的关闭/错误可能为 WLAN 伪报，延后处理
bool ShouldDeferDisconnect(IoStatus status) {
    if ((status != IoStatus::kClosed && status != IoStatus::kError) || g_bridge.last_send_at_ms == 0) {
        return false;
    }

    const uint64_t now_ms = MonotonicNowMs();
    if (now_ms < g_bridge.last_send_at_ms) {
        return false;
    }

    // The board-side TCP stack occasionally reports a false receive-side
    // close/error a few hundred milliseconds after we successfully queue a
    // small JSON ACK/control frame. Tearing the socket down inside that window
    // aborts kernel retransmission and turns a recoverable WLAN blip into a
    // deterministic assistant command failure.
    return now_ms - g_bridge.last_send_at_ms <= kPostSendDisconnectGuardMs;
}

// 检查套接字是否有可读事件（poll 零超时探测）
bool SocketHasReadableEvent() {
    if (g_bridge.socket_fd < 0) {
        return false;
    }

    pollfd descriptor{};
    descriptor.fd = g_bridge.socket_fd;
#ifdef POLLRDHUP
    descriptor.events = POLLIN | POLLERR | POLLHUP | POLLRDHUP;
#else
    descriptor.events = POLLIN | POLLERR | POLLHUP;
#endif

    while (true) {
        const int poll_rc = poll(&descriptor, 1, 0);
        if (poll_rc > 0) {
            return descriptor.revents != 0;
        }
        if (poll_rc == 0) {
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        RecordIoStatus(IoStatus::kError,
                       "assistant TCP poll failed: " + std::string(std::strerror(errno)));
        return true;
    }
}

// 等待套接字可写（带超时），用于可靠发送的重试
bool WaitForSocketWritable(uint64_t started_at_ms, uint64_t timeout_ms) {
    if (g_bridge.socket_fd < 0) {
        return false;
    }

    while (true) {
        const uint64_t now_ms = MonotonicNowMs();
        if (now_ms < started_at_ms || now_ms - started_at_ms >= timeout_ms) {
            return false;
        }

        pollfd descriptor{};
        descriptor.fd = g_bridge.socket_fd;
        descriptor.events = POLLOUT | POLLERR | POLLHUP;
        const int remaining_ms = static_cast<int>(std::min<uint64_t>(timeout_ms - (now_ms - started_at_ms), 5));
        const int poll_rc = poll(&descriptor, 1, remaining_ms);
        if (poll_rc > 0) {
            if ((descriptor.revents & POLLOUT) != 0) {
                return true;
            }
            RecordIoStatus(IoStatus::kError,
                           "assistant TCP socket reported an error while waiting to send");
            return false;
        }
        if (poll_rc == 0) {
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        RecordIoStatus(IoStatus::kError,
                       "assistant TCP poll for writable failed: " + std::string(std::strerror(errno)));
        return false;
    }
}

// 带发送策略的数据发送 —— kDropIfBusy 丢弃整帧，kReliable 等待可写重试
uint32 BridgeSendDataWithPolicy(const uint8* buff, uint32 length, SendPolicy policy) {
    if (g_bridge.state != AssistantBridgeState::kReady || g_bridge.socket_fd < 0) {
        RecordIoStatus(IoStatus::kClosed, "assistant transport is not connected");
        return length;
    }

    uint32 sent_total = 0;
    const uint64_t started_at_ms = MonotonicNowMs();
    while (sent_total < length) {
        const uint32 remaining = length - sent_total;
#ifdef MSG_NOSIGNAL
        const int send_flags = MSG_NOSIGNAL;
#else
        const int send_flags = 0;
#endif
        const ssize_t sent = send(
            g_bridge.socket_fd,
            buff + sent_total,
            static_cast<size_t>(remaining),
            send_flags);
        if (sent > 0) {
            g_bridge.last_sent_bytes = static_cast<std::size_t>(sent);
            g_bridge.total_sent_bytes += static_cast<std::size_t>(sent);
            g_bridge.last_send_at_ms = MonotonicNowMs();
            sent_total += static_cast<uint32>(sent);
            continue;
        }
        if (sent == 0) {
            RecordIoStatus(IoStatus::kClosed, "assistant TCP socket closed while sending");
            return length - sent_total;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (policy == SendPolicy::kDropIfBusy && sent_total == 0) {
                RecordIoStatus(IoStatus::kWouldBlock, "assistant TCP socket is busy; dropped this frame");
                return length;
            }
            if (WaitForSocketWritable(started_at_ms, kReliableSendTimeoutMs)) {
                continue;
            }
            if (g_bridge.last_io_status == IoStatus::kOk) {
                RecordIoStatus(sent_total == 0 ? IoStatus::kWouldBlock : IoStatus::kError,
                               sent_total == 0
                                   ? "assistant TCP socket stayed busy before sending this frame"
                                   : "assistant TCP socket stalled after a partial frame send");
            }
            return length - sent_total;
        }
        RecordIoStatus(IoStatus::kError,
                       "assistant TCP send failed: " + std::string(std::strerror(errno)));
        return length - sent_total;
    }
    return 0;
}

// 使用当前活跃发送策略发送数据
uint32 BridgeSendData(const uint8* buff, uint32 length) {
    return BridgeSendDataWithPolicy(buff, length, g_bridge.active_send_policy);
}

// 从套接字接收数据 —— 返回实际收到的字节数，0 表示无数据或连接关闭
uint32 BridgeReadData(uint8* buff, uint32 length) {
    if (g_bridge.state != AssistantBridgeState::kReady || g_bridge.socket_fd < 0 || buff == nullptr || length == 0) {
        return 0;
    }

    while (true) {
        const ssize_t received = recv(
            g_bridge.socket_fd,
            buff,
            static_cast<size_t>(length),
#ifdef MSG_DONTWAIT
            MSG_DONTWAIT
#else
            0
#endif
        );
        if (received > 0) {
            g_bridge.last_received_bytes = static_cast<std::size_t>(received);
            g_bridge.total_received_bytes += static_cast<std::size_t>(received);
            g_bridge.last_recv_at_ms = MonotonicNowMs();
            return static_cast<uint32>(received);
        }
        if (received == 0) {
            RecordIoStatus(IoStatus::kClosed, "assistant TCP peer closed the connection");
            return 0;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        RecordIoStatus(IoStatus::kError,
                       "assistant TCP receive failed: " + std::string(std::strerror(errno)));
        return 0;
    }
}

// 排空接收缓冲区 —— 将可读数据追加到待处理字节串，单次上限 kMaxReceiveDrainBytesPerPoll
void DrainReceiveBuffer() {
    uint8_t buffer[256];
    std::size_t drained_bytes = 0;
    while (true) {
        if (!SocketHasReadableEvent()) {
            return;
        }
        const uint32 received = BridgeReadData(buffer, sizeof(buffer));
        if (received == 0 || g_bridge.last_io_status == IoStatus::kClosed || g_bridge.last_io_status == IoStatus::kError) {
            return;
        }
        g_bridge.received_bytes_pending.append(reinterpret_cast<const char*>(buffer),
                                              static_cast<std::size_t>(received));
        drained_bytes += static_cast<std::size_t>(received);
        if (drained_bytes >= kMaxReceiveDrainBytesPerPoll) {
            return;
        }
    }
}

// 发起 TCP 连接 —— DNS 解析 → 创建非阻塞套接字 → connect（非阻塞）
bool BeginConnectAttempt() {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* resolved = nullptr;
    const std::string port_text = std::to_string(g_bridge.config.port);
    const int resolve_rc = getaddrinfo(g_bridge.config.host.c_str(), port_text.c_str(), &hints, &resolved);
    if (resolve_rc != 0 || resolved == nullptr) {
        EnterBackoff("assistant TCP resolve failed for " + g_bridge.config.host + ":" + port_text +
                     " (" + gai_strerror(resolve_rc) + ")");
        if (resolved != nullptr) {
            freeaddrinfo(resolved);
        }
        return false;
    }

    const int socket_fd = socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
    if (socket_fd < 0) {
        const std::string detail = "assistant TCP socket() failed: " + std::string(std::strerror(errno));
        freeaddrinfo(resolved);
        EnterBackoff(detail);
        return false;
    }

    std::string detail;
    if (!EnsureNonBlocking(socket_fd, detail)) {
        freeaddrinfo(resolved);
        close(socket_fd);
        EnterBackoff(detail);
        return false;
    }
    if (!EnableLowLatencySocket(socket_fd, detail)) {
        freeaddrinfo(resolved);
        close(socket_fd);
        EnterBackoff(detail);
        return false;
    }

    g_bridge.socket_fd = socket_fd;
    const int connect_rc = connect(socket_fd, resolved->ai_addr, resolved->ai_addrlen);
    freeaddrinfo(resolved);

    if (connect_rc == 0) {
        TransitionTo(AssistantBridgeState::kReady,
                     "assistant TCP connected to " + g_bridge.config.host + ":" + port_text);
        return true;
    }
    if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
        TransitionTo(AssistantBridgeState::kConnecting,
                     "assistant TCP connecting to " + g_bridge.config.host + ":" + port_text);
        return false;
    }

    EnterBackoff("assistant TCP connect failed: " + std::string(std::strerror(errno)));
    return false;
}

// 完成挂起的非阻塞连接 —— 通过 poll + getsockopt(SO_ERROR) 检查连接是否成功
void FinishPendingConnect() {
    if (g_bridge.socket_fd < 0) {
        EnterBackoff("assistant TCP socket disappeared during connect");
        return;
    }

    pollfd descriptor{};
    descriptor.fd = g_bridge.socket_fd;
    descriptor.events = POLLOUT | POLLERR | POLLHUP;

    const int poll_rc = poll(&descriptor, 1, 0);
    if (poll_rc <= 0) {
        if (poll_rc < 0 && errno != EINTR) {
            EnterBackoff("assistant TCP poll failed: " + std::string(std::strerror(errno)));
        }
        return;
    }

    int socket_error = 0;
    socklen_t socket_error_len = sizeof(socket_error);
    if (getsockopt(g_bridge.socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_len) < 0) {
        EnterBackoff("assistant TCP getsockopt(SO_ERROR) failed: " + std::string(std::strerror(errno)));
        return;
    }
    if (socket_error != 0) {
        EnterBackoff("assistant TCP connect failed: " + std::string(std::strerror(socket_error)));
        return;
    }

    TransitionTo(AssistantBridgeState::kReady,
                 "assistant TCP connected to " + g_bridge.config.host + ":" + std::to_string(g_bridge.config.port));
}

// 获取轮询结果 —— 提取状态、变更标记、待处理数据，清理 dirty 标志
AssistantBridgePollResult TakePollResult(const uint64_t now_ms) {
    AssistantBridgePollResult result{};
    result.state = g_bridge.state;
    result.state_changed = g_bridge.state_dirty;
    result.detail = g_bridge.detail;
    result.received_bytes.swap(g_bridge.received_bytes_pending);
    g_bridge.state_dirty = false;
    return result;
}

// 最终化传输结果 —— 根据 last_io_status 判断成功/失败，出错时聚合诊断信息
bool FinalizeTransferResult(std::string& detail) {
    if (g_bridge.last_io_status == IoStatus::kOk) {
        detail.clear();
        return true;
    }
    detail = g_bridge.last_io_detail + DescribeRecentIo() + DescribeSocketEndpoints() + DescribeTcpInfo();
    if (g_bridge.last_io_status == IoStatus::kWouldBlock) {
        return false;
    }
    HandleSocketFailure(detail);
    return false;
}

}  // namespace

// 初始化辅助桥 —— 配置主机/端口，绑定协议接口，重置全局状态
bool InitializeAssistantBridge(const AssistantBridgeConfig& config, std::string& detail) {
    if (config.host.empty() || config.port <= 0) {
        CloseSocket();
        detail = "assistant TCP host/port is invalid";
        TransitionTo(AssistantBridgeState::kUnconfigured, detail);
        return false;
    }

    if (!g_bridge.interface_bound) {
        seekfree_assistant_interface_init(BridgeSendData, BridgeReadData);
        g_bridge.interface_bound = true;
    }

    g_bridge.config = config;
    g_bridge.next_retry_at_ms = 0;
    ResetIoStatus();
    CloseSocket();
    g_bridge.total_sent_bytes = 0;
    g_bridge.total_received_bytes = 0;
    g_bridge.last_sent_bytes = 0;
    g_bridge.last_received_bytes = 0;
    g_bridge.last_send_at_ms = 0;
    g_bridge.last_recv_at_ms = 0;

    TransitionTo(AssistantBridgeState::kDisconnected,
                 "assistant TCP configured for " + config.host + ":" + std::to_string(config.port));
    detail.clear();
    return true;
}

// 轮询辅助桥状态机 —— 根据当前状态执行连接/重连/排空/故障处理
AssistantBridgePollResult PollAssistantBridge() {
    const uint64_t now_ms = MonotonicNowMs();
    ResetIoStatus();

    switch (g_bridge.state) {
        case AssistantBridgeState::kUnconfigured:
            break;
        case AssistantBridgeState::kDisconnected:
        case AssistantBridgeState::kBackoff:
            if (now_ms >= g_bridge.next_retry_at_ms) {
                (void)BeginConnectAttempt();
            }
            break;
        case AssistantBridgeState::kConnecting:
            FinishPendingConnect();
            break;
        case AssistantBridgeState::kReady:
            DrainReceiveBuffer();
            if (g_bridge.last_io_status == IoStatus::kClosed || g_bridge.last_io_status == IoStatus::kError) {
                if (ShouldDeferDisconnect(g_bridge.last_io_status)) {
                    ResetIoStatus();
                    break;
                }
                HandleSocketFailure(g_bridge.last_io_detail + DescribeFullSocketState());
            }
            break;
    }

    return TakePollResult(now_ms);
}

// 检查辅助桥是否处于就绪状态
bool AssistantBridgeReady() {
    return g_bridge.state == AssistantBridgeState::kReady;
}

// 发送辅助数据字节 —— 支持可靠/丢弃策略，返回 FinalizeTransferResult
bool SendAssistantBytes(const std::uint8_t* data, std::size_t length, bool reliable, std::string& detail) {
    if (!AssistantBridgeReady()) {
        detail = "assistant bridge not connected";
        return false;
    }
    if (data == nullptr || length == 0) {
        detail = "assistant payload is empty";
        return false;
    }

    ResetIoStatus();
    const uint32 unsent = BridgeSendDataWithPolicy(reinterpret_cast<const uint8*>(data),
                                                   static_cast<uint32>(length),
                                                   reliable ? SendPolicy::kReliable : SendPolicy::kDropIfBusy);
    if (unsent != 0 && g_bridge.last_io_status == IoStatus::kOk) {
        detail = "assistant payload send incomplete";
        return false;
    }
    return FinalizeTransferResult(detail);
}

}  // namespace ls2k::platform::true_ls2k0300
