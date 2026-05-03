// 转向媒体桥接实现 —— 基于 TCP 的媒体流通信桥。
// 管理与上位机的媒体数据传输、连接重连和待发送缓冲刷新。
// 与辅助桥类似但专用于转向媒体通道，支持发送缓冲排队。

#include "platform/true_ls2k0300/steering_media_bridge.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ls2k::platform::true_ls2k0300 {
namespace {

constexpr std::uint64_t kReconnectBackoffMs = 1000;
[[maybe_unused]] constexpr std::uint64_t kReliableSendTimeoutMs = 75;
constexpr int kSteeringMediaSocketBufferBytes = 256 * 1024;

struct SteeringMediaBridgeContext {
    SteeringMediaBridgeConfig config{};
    SteeringMediaBridgeState state = SteeringMediaBridgeState::kUnconfigured;
    int socket_fd = -1;
    std::uint64_t next_retry_at_ms = 0;
    bool state_dirty = false;
    std::string detail = "steering media bridge not configured";
    std::vector<std::uint8_t> pending_send{};
    std::size_t pending_send_offset = 0;
};

SteeringMediaBridgeContext g_bridge{};

// 获取单调时钟当前毫秒数
std::uint64_t MonotonicNowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

// 清空待发送缓冲
void ClearPendingSend() {
    g_bridge.pending_send.clear();
    g_bridge.pending_send_offset = 0;
}

// 关闭并清理套接字（同时清空待发送缓冲）
void CloseSocket() {
    if (g_bridge.socket_fd >= 0) {
        close(g_bridge.socket_fd);
        g_bridge.socket_fd = -1;
    }
    ClearPendingSend();
}

// 状态迁移 —— 标记 state_dirty 供外部轮询时捕获
void TransitionTo(SteeringMediaBridgeState state, std::string detail) {
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
    TransitionTo(SteeringMediaBridgeState::kBackoff, detail);
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

// 尝试扩大套接字发送/接收缓冲区（best-effort，忽略失败）
void TryExpandSocketBuffers(int socket_fd) {
    const int buffer_bytes = kSteeringMediaSocketBufferBytes;
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_SNDBUF, &buffer_bytes, sizeof(buffer_bytes));
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &buffer_bytes, sizeof(buffer_bytes));
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
        EnterBackoff("steering media TCP resolve failed for " + g_bridge.config.host + ":" +
                     port_text + " (" + gai_strerror(resolve_rc) + ")");
        if (resolved != nullptr) {
            freeaddrinfo(resolved);
        }
        return false;
    }

    const int socket_fd = socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
    if (socket_fd < 0) {
        const std::string detail =
            "steering media TCP socket() failed: " + std::string(std::strerror(errno));
        freeaddrinfo(resolved);
        EnterBackoff(detail);
        return false;
    }

    std::string detail;
    if (!EnsureNonBlocking(socket_fd, detail) || !EnableLowLatencySocket(socket_fd, detail)) {
        freeaddrinfo(resolved);
        close(socket_fd);
        EnterBackoff(detail);
        return false;
    }
    TryExpandSocketBuffers(socket_fd);

    g_bridge.socket_fd = socket_fd;
    const int connect_rc = connect(socket_fd, resolved->ai_addr, resolved->ai_addrlen);
    freeaddrinfo(resolved);

    if (connect_rc == 0) {
        TransitionTo(SteeringMediaBridgeState::kReady,
                     "steering media TCP connected to " + g_bridge.config.host + ":" + port_text);
        return true;
    }
    if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
        TransitionTo(SteeringMediaBridgeState::kConnecting,
                     "steering media TCP connecting to " + g_bridge.config.host + ":" + port_text);
        return false;
    }

    EnterBackoff("steering media TCP connect failed: " + std::string(std::strerror(errno)));
    return false;
}

// 完成挂起的非阻塞连接 —— 通过 poll + getsockopt(SO_ERROR) 检查连接是否成功
void FinishPendingConnect() {
    if (g_bridge.socket_fd < 0) {
        EnterBackoff("steering media TCP socket disappeared during connect");
        return;
    }

    pollfd descriptor{};
    descriptor.fd = g_bridge.socket_fd;
    descriptor.events = POLLOUT | POLLERR | POLLHUP;

    const int poll_rc = poll(&descriptor, 1, 0);
    if (poll_rc <= 0) {
        if (poll_rc < 0 && errno != EINTR) {
            EnterBackoff("steering media TCP poll failed: " + std::string(std::strerror(errno)));
        }
        return;
    }

    int socket_error = 0;
    socklen_t socket_error_len = sizeof(socket_error);
    if (getsockopt(g_bridge.socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_len) < 0) {
        EnterBackoff("steering media TCP getsockopt(SO_ERROR) failed: " +
                     std::string(std::strerror(errno)));
        return;
    }
    if (socket_error != 0) {
        EnterBackoff("steering media TCP connect failed: " + std::string(std::strerror(socket_error)));
        return;
    }

    TransitionTo(SteeringMediaBridgeState::kReady,
                 "steering media TCP connected to " + g_bridge.config.host + ":" +
                     std::to_string(g_bridge.config.port));
}

// 健康检查 —— poll 探测就绪套接字是否出现错误或对端关闭
void CheckReadySocketHealth() {
    if (g_bridge.socket_fd < 0) {
        EnterBackoff("steering media TCP socket disappeared");
        return;
    }

    pollfd descriptor{};
    descriptor.fd = g_bridge.socket_fd;
#ifdef POLLRDHUP
    descriptor.events = POLLOUT | POLLERR | POLLHUP | POLLRDHUP;
#else
    descriptor.events = POLLOUT | POLLERR | POLLHUP;
#endif

    const int poll_rc = poll(&descriptor, 1, 0);
    if (poll_rc < 0) {
        if (errno != EINTR) {
            EnterBackoff("steering media TCP poll failed: " + std::string(std::strerror(errno)));
        }
        return;
    }
    if (poll_rc == 0) {
        return;
    }

    if ((descriptor.revents & POLLERR) != 0 || (descriptor.revents & POLLHUP) != 0
#ifdef POLLRDHUP
        || (descriptor.revents & POLLRDHUP) != 0
#endif
    ) {
        int socket_error = 0;
        socklen_t socket_error_len = sizeof(socket_error);
        if (getsockopt(g_bridge.socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_len) == 0 &&
            socket_error != 0) {
            EnterBackoff("steering media TCP socket failed: " + std::string(std::strerror(socket_error)));
        } else {
            EnterBackoff("steering media TCP socket closed by peer");
        }
    }
}

// 等待套接字可写（带超时），用于可靠发送的重试
[[maybe_unused]] bool WaitForSocketWritable(std::uint64_t started_at_ms, std::uint64_t timeout_ms) {
    if (g_bridge.socket_fd < 0) {
        return false;
    }

    while (true) {
        const std::uint64_t now_ms = MonotonicNowMs();
        if (now_ms < started_at_ms || now_ms - started_at_ms >= timeout_ms) {
            return false;
        }

        pollfd descriptor{};
        descriptor.fd = g_bridge.socket_fd;
        descriptor.events = POLLOUT | POLLERR | POLLHUP;
        const int remaining_ms =
            static_cast<int>(std::min<std::uint64_t>(timeout_ms - (now_ms - started_at_ms), 5));
        const int poll_rc = poll(&descriptor, 1, remaining_ms);
        if (poll_rc > 0) {
            if ((descriptor.revents & POLLOUT) != 0) {
                return true;
            }
            return false;
        }
        if (poll_rc == 0) {
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        return false;
    }
}

// 刷新待发送缓冲 —— 循环发送直到耗尽或遇阻塞；wait_for_writable 决定是否等待可写
SteeringMediaBridgeSendResult FlushPendingSend(bool wait_for_writable, std::string& detail) {
    if (g_bridge.pending_send.empty()) {
        detail.clear();
        return SteeringMediaBridgeSendResult::kSent;
    }
    if (g_bridge.state != SteeringMediaBridgeState::kReady || g_bridge.socket_fd < 0) {
        detail = "steering media bridge not connected";
        return SteeringMediaBridgeSendResult::kDisconnected;
    }

    const std::uint64_t started_at_ms = MonotonicNowMs();
    while (g_bridge.pending_send_offset < g_bridge.pending_send.size()) {
#ifdef MSG_NOSIGNAL
        const int send_flags = MSG_NOSIGNAL;
#else
        const int send_flags = 0;
#endif
        const std::size_t remaining = g_bridge.pending_send.size() - g_bridge.pending_send_offset;
        const ssize_t sent = send(g_bridge.socket_fd,
                                  g_bridge.pending_send.data() + g_bridge.pending_send_offset,
                                  remaining,
                                  send_flags);
        if (sent > 0) {
            g_bridge.pending_send_offset += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent == 0) {
            EnterBackoff("steering media TCP socket closed while sending");
            detail = g_bridge.detail;
            return SteeringMediaBridgeSendResult::kDisconnected;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (wait_for_writable && WaitForSocketWritable(started_at_ms, kReliableSendTimeoutMs)) {
                continue;
            }
            detail = "steering media TCP socket is busy; keeping current frame in-flight";
            return SteeringMediaBridgeSendResult::kBusyRejected;
        }
        EnterBackoff("steering media TCP send failed: " + std::string(std::strerror(errno)));
        detail = g_bridge.detail;
        return SteeringMediaBridgeSendResult::kError;
    }

    ClearPendingSend();
    detail.clear();
    return SteeringMediaBridgeSendResult::kSent;
}

}  // namespace

// 初始化转向媒体桥 —— 配置主机/端口，重置全局状态至 kDisconnected
bool InitializeSteeringMediaBridge(const SteeringMediaBridgeConfig& config, std::string& detail) {
    if (config.host.empty() || config.port <= 0) {
        CloseSocket();
        detail = "steering media TCP host/port is invalid";
        TransitionTo(SteeringMediaBridgeState::kUnconfigured, detail);
        return false;
    }

    CloseSocket();
    g_bridge.config = config;
    g_bridge.next_retry_at_ms = 0;
    TransitionTo(SteeringMediaBridgeState::kDisconnected,
                 "steering media TCP configured for " + config.host + ":" + std::to_string(config.port));
    detail = g_bridge.detail;
    return true;
}

// 轮询转向媒体桥状态机 —— 根据当前状态执行连接/刷新/健康检查
SteeringMediaBridgePollResult PollSteeringMediaBridge() {
    const std::uint64_t now_ms = MonotonicNowMs();
    switch (g_bridge.state) {
        case SteeringMediaBridgeState::kUnconfigured:
            break;
        case SteeringMediaBridgeState::kDisconnected:
            if (now_ms >= g_bridge.next_retry_at_ms) {
                BeginConnectAttempt();
            }
            break;
        case SteeringMediaBridgeState::kConnecting:
            FinishPendingConnect();
            break;
        case SteeringMediaBridgeState::kReady:
            {
                std::string ignored_detail;
                (void)FlushPendingSend(false, ignored_detail);
            }
            CheckReadySocketHealth();
            break;
        case SteeringMediaBridgeState::kBackoff:
            if (now_ms >= g_bridge.next_retry_at_ms) {
                TransitionTo(SteeringMediaBridgeState::kDisconnected,
                             "steering media TCP reconnecting after backoff");
                BeginConnectAttempt();
            }
            break;
    }

    SteeringMediaBridgePollResult result{};
    result.state = g_bridge.state;
    result.state_changed = g_bridge.state_dirty;
    result.detail = g_bridge.detail;
    g_bridge.state_dirty = false;
    return result;
}

// 检查转向媒体桥是否处于就绪状态
bool SteeringMediaBridgeReady() {
    return g_bridge.state == SteeringMediaBridgeState::kReady && g_bridge.socket_fd >= 0;
}

// 发送转向媒体数据 —— bridge 拥有当前 in-flight bytes；
// 如果已有 in-flight 还没刷完，则拒收新帧，交由上层只保留最新 replacement。
SteeringMediaBridgeSendResult SendSteeringMediaBytes(const std::uint8_t* data,
                                                     std::size_t length,
                                                     std::string& detail) {
    if (!SteeringMediaBridgeReady()) {
        detail = "steering media bridge not connected";
        return SteeringMediaBridgeSendResult::kDisconnected;
    }
    if (data == nullptr || length == 0) {
        detail = "steering media payload is empty";
        return SteeringMediaBridgeSendResult::kError;
    }

    if (!g_bridge.pending_send.empty()) {
        const SteeringMediaBridgeSendResult flush_result = FlushPendingSend(false, detail);
        if (flush_result != SteeringMediaBridgeSendResult::kSent) {
            if (SteeringMediaBridgeReady() && !g_bridge.pending_send.empty()) {
                detail = "steering media TCP socket is busy; finishing previous frame";
                return SteeringMediaBridgeSendResult::kBusyRejected;
            }
            return flush_result;
        }
    }

    g_bridge.pending_send.assign(data, data + length);
    g_bridge.pending_send_offset = 0;
    const SteeringMediaBridgeSendResult flush_result = FlushPendingSend(false, detail);
    if (flush_result == SteeringMediaBridgeSendResult::kSent) {
        return SteeringMediaBridgeSendResult::kSent;
    }
    if (SteeringMediaBridgeReady() && !g_bridge.pending_send.empty()) {
        detail.clear();
        return SteeringMediaBridgeSendResult::kAcceptedInFlight;
    }
    return flush_result;
}

}  // namespace ls2k::platform::true_ls2k0300
