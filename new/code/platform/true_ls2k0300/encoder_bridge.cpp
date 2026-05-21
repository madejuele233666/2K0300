// 编码器桥接实现 —— 从字符设备读取左右轮编码器计数值。

#include "platform/true_ls2k0300/bridge.hpp"

#include <cstdint>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "platform/true_ls2k0300/vendor_paths.hpp"

namespace ls2k::platform::true_ls2k0300 {
namespace {

// 编码器单次读取结果
struct EncoderReadResult {
    bool ok = false;        // 读取是否成功
    int32_t count = 0;      // 编码器计数值
};

// 编码器设备上下文 —— 包含设备路径和已打开的文件描述符
struct EncoderDevice {
    const char* path = nullptr;   // 设备字符文件路径
    int fd = -1;                   // 已打开的文件描述符（-1 表示未打开）
};

EncoderDevice g_left_encoder{kLeftEncoderPath, -1};
EncoderDevice g_right_encoder{kRightEncoderPath, -1};
bool g_use_persistent_fd = false;

// 判断编码器读取是否成功 —— 供应商驱动仅将 read() == -1 视为失败，
// 字符设备可能返回 0 字节但仍更新缓冲区，因此 bytes >= 0 即视为成功
bool AcceptedEncoderRead(ssize_t bytes) {
    return bytes >= 0;
}

// 从已打开的文件描述符读取编码器 16 位计数值
EncoderReadResult ReadEncoderCountFromFd(int fd) {
    EncoderReadResult result{};
    if (fd < 0) {
        return result;
    }
    (void)lseek(fd, 0, SEEK_SET);
    int16_t raw = 0;
    const ssize_t bytes = read(fd, &raw, sizeof(raw));
    if (!AcceptedEncoderRead(bytes)) {
        return result;
    }

    result.ok = true;
    result.count = static_cast<int32_t>(raw);
    return result;
}

// 从编码器字符设备读取 16 位计数值
EncoderReadResult ReadEncoderCountOpenClose(const char* path) {
    EncoderReadResult result{};
    const int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return result;
    }
    int16_t raw = 0;
    const ssize_t bytes = read(fd, &raw, sizeof(raw));
    const int close_rc = close(fd);

    if (!AcceptedEncoderRead(bytes) || close_rc != 0) {
        return result;
    }

    result.ok = true;
    result.count = static_cast<int32_t>(raw);
    return result;
}

// 关闭持久打开的编码器设备文件描述符
void ClosePersistentEncoderFds() {
    if (g_left_encoder.fd >= 0) {
        (void)close(g_left_encoder.fd);
        g_left_encoder.fd = -1;
    }
    if (g_right_encoder.fd >= 0) {
        (void)close(g_right_encoder.fd);
        g_right_encoder.fd = -1;
    }
    g_use_persistent_fd = false;
}

// 探测单个编码器设备是否支持持久 fd 模式（打开、读取、lseek 回到起点、再次读取均成功）
bool ProbePersistentEncoderDevice(EncoderDevice& device) {
    device.fd = open(device.path, O_RDONLY);
    if (device.fd < 0) {
        return false;
    }
    if (!ReadEncoderCountFromFd(device.fd).ok) {
        return false;
    }
    if (lseek(device.fd, 0, SEEK_SET) < 0) {
        return false;
    }
    return ReadEncoderCountFromFd(device.fd).ok;
}

// 探测左右编码器是否都支持持久 fd 模式，均成功则启用持久 fd
bool ProbePersistentEncoderFds() {
    ClosePersistentEncoderFds();
    if (!ProbePersistentEncoderDevice(g_left_encoder) ||
        !ProbePersistentEncoderDevice(g_right_encoder)) {
        ClosePersistentEncoderFds();
        return false;
    }
    g_use_persistent_fd = true;
    return true;
}

// 读取指定编码器设备计数值 —— 优先使用持久 fd，退化时回退到 open/read/close 模式
EncoderReadResult ReadEncoderCount(EncoderDevice& device) {
    if (g_use_persistent_fd && device.fd >= 0) {
        EncoderReadResult result = ReadEncoderCountFromFd(device.fd);
        if (result.ok) {
            return result;
        }
        ClosePersistentEncoderFds();
    }
    return ReadEncoderCountOpenClose(device.path);
}

}  // namespace

// 初始化编码器 —— 探测左右轮编码器设备可访问性
BridgeStatus InitializeEncoder() {
    BridgeStatus status{};
    const bool persistent_ok = ProbePersistentEncoderFds();
    if (!ReadEncoderCountOpenClose(kLeftEncoderPath).ok) {
        status.detail = std::string("encoder resource unavailable: ") + kLeftEncoderPath;
        return status;
    }
    if (!ReadEncoderCountOpenClose(kRightEncoderPath).ok) {
        status.detail = std::string("encoder resource unavailable: ") + kRightEncoderPath;
        return status;
    }
    status.ok = true;
    status.detail = persistent_ok ? "encoder resources probed successfully with persistent fd"
                                  : "encoder resources probed successfully with open/read/close fallback";
    return status;
}

// 读取左右编码器计数值
EncoderCounts ReadEncoderCounts() {
    EncoderCounts counts{};
    const EncoderReadResult left = ReadEncoderCount(g_left_encoder);
    if (!left.ok) {
        counts.detail = std::string("encoder read failed: ") + kLeftEncoderPath;
        return counts;
    }
    const EncoderReadResult right = ReadEncoderCount(g_right_encoder);
    if (!right.ok) {
        counts.detail = std::string("encoder read failed: ") + kRightEncoderPath;
        return counts;
    }
    counts.left = static_cast<int>(left.count);
    counts.right = static_cast<int>(right.count);
    counts.valid = true;
    return counts;
}

}  // namespace ls2k::platform::true_ls2k0300
