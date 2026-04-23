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
#include <unistd.h>
#include <utility>
#include <netinet/tcp.h>

#include "seekfree_assistant.h"
#include "seekfree_assistant_interface.h"

namespace ls2k::platform::true_ls2k0300 {
namespace {

constexpr uint64_t kReconnectBackoffMs = 1000;
constexpr std::size_t kMaxReceiveDrainBytesPerPoll = 8192;
// Give the board TCP stack enough time to retransmit a just-sent small
// control frame before user space tears the socket down on a spurious
// receive-side close/error report.
constexpr uint64_t kPostSendDisconnectGuardMs = 1000;

enum class IoStatus {
    kOk = 0,
    kWouldBlock,
    kClosed,
    kError,
};

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
};

AssistantBridgeContext g_bridge{};

uint64_t MonotonicNowMs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

void CloseSocket() {
    if (g_bridge.socket_fd >= 0) {
        close(g_bridge.socket_fd);
        g_bridge.socket_fd = -1;
    }
}

void TransitionTo(AssistantBridgeState state, std::string detail) {
    if (g_bridge.state != state || g_bridge.detail != detail) {
        g_bridge.state_dirty = true;
    }
    g_bridge.state = state;
    g_bridge.detail = std::move(detail);
}

void EnterBackoff(const std::string& detail) {
    CloseSocket();
    g_bridge.next_retry_at_ms = MonotonicNowMs() + kReconnectBackoffMs;
    TransitionTo(AssistantBridgeState::kBackoff, detail);
}

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

bool EnableLowLatencySocket(int socket_fd, std::string& detail) {
    const int enabled = 1;
    if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) < 0) {
        detail = "setsockopt(TCP_NODELAY) failed: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

void ResetIoStatus() {
    g_bridge.last_io_status = IoStatus::kOk;
    g_bridge.last_io_detail.clear();
}

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

void RecordIoStatus(IoStatus status, const std::string& detail) {
    if (status == IoStatus::kOk) {
        return;
    }
    if (g_bridge.last_io_status == IoStatus::kOk || status == IoStatus::kClosed || status == IoStatus::kError) {
        g_bridge.last_io_status = status;
        g_bridge.last_io_detail = detail;
    }
}

void HandleSocketFailure(const std::string& detail) {
    EnterBackoff(detail);
}

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

uint32 BridgeSendData(const uint8* buff, uint32 length) {
    if (g_bridge.state != AssistantBridgeState::kReady || g_bridge.socket_fd < 0) {
        RecordIoStatus(IoStatus::kClosed, "assistant transport is not connected");
        return length;
    }

    uint32 sent_total = 0;
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
            RecordIoStatus(IoStatus::kWouldBlock, "assistant TCP socket is busy; dropped this frame");
            return length - sent_total;
        }
        RecordIoStatus(IoStatus::kError,
                       "assistant TCP send failed: " + std::string(std::strerror(errno)));
        return length - sent_total;
    }
    return 0;
}

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

AssistantBridgePollResult TakePollResult(const uint64_t now_ms) {
    AssistantBridgePollResult result{};
    result.state = g_bridge.state;
    result.state_changed = g_bridge.state_dirty;
    result.detail = g_bridge.detail;
    result.received_bytes.swap(g_bridge.received_bytes_pending);
    g_bridge.state_dirty = false;
    return result;
}

bool FinalizeTransferResult(std::string& detail) {
    if (g_bridge.last_io_status == IoStatus::kOk) {
        detail.clear();
        return true;
    }
    detail = g_bridge.last_io_detail + DescribeRecentIo();
    if (g_bridge.last_io_status == IoStatus::kWouldBlock) {
        return false;
    }
    HandleSocketFailure(detail);
    return false;
}

}  // namespace

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
                HandleSocketFailure(g_bridge.last_io_detail + DescribeRecentIo());
            }
            break;
    }

    return TakePollResult(now_ms);
}

bool AssistantBridgeReady() {
    return g_bridge.state == AssistantBridgeState::kReady;
}

bool SendAssistantBytes(const std::uint8_t* data, std::size_t length, std::string& detail) {
    if (!AssistantBridgeReady()) {
        detail = "assistant bridge not connected";
        return false;
    }
    if (data == nullptr || length == 0) {
        detail = "assistant payload is empty";
        return false;
    }

    ResetIoStatus();
    const uint32 unsent = BridgeSendData(reinterpret_cast<const uint8*>(data), static_cast<uint32>(length));
    if (unsent != 0 && g_bridge.last_io_status == IoStatus::kOk) {
        detail = "assistant payload send incomplete";
        return false;
    }
    return FinalizeTransferResult(detail);
}

bool SendAssistantOscilloscope(const std::array<float, 8>& values,
                               std::size_t channel_count,
                               std::string& detail) {
    if (!AssistantBridgeReady()) {
        detail = "assistant bridge not connected";
        return false;
    }

    seekfree_assistant_oscilloscope_struct payload{};
    const std::size_t clamped_channels =
        std::min<std::size_t>(channel_count, SEEKFREE_ASSISTANT_SET_OSCILLOSCOPE_COUNT);
    payload.channel_num = static_cast<uint8>(clamped_channels);
    for (std::size_t index = 0; index < clamped_channels; ++index) {
        payload.data[index] = values[index];
    }

    ResetIoStatus();
    seekfree_assistant_oscilloscope_send(&payload);
    return FinalizeTransferResult(detail);
}

bool SendAssistantImage(const uint8_t* image_gray, int width, int height, std::string& detail) {
    if (!AssistantBridgeReady()) {
        detail = "assistant bridge not connected";
        return false;
    }
    if (image_gray == nullptr || width <= 0 || height <= 0) {
        detail = "assistant image frame is invalid";
        return false;
    }

    ResetIoStatus();
    seekfree_assistant_camera_information_config(
        SEEKFREE_ASSISTANT_MT9V03X,
        const_cast<uint8_t*>(image_gray),
        static_cast<uint16>(width),
        static_cast<uint16>(height));
    seekfree_assistant_camera_boundary_config(NO_BOUNDARY, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    seekfree_assistant_camera_send();
    return FinalizeTransferResult(detail);
}

}  // namespace ls2k::platform::true_ls2k0300
