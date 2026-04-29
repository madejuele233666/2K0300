// 编码器桥接实现 —— 从字符设备读取左右轮编码器计数值。

#include "platform/true_ls2k0300/bridge.hpp"

#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

#include "platform/true_ls2k0300/vendor_paths.hpp"

namespace ls2k::platform::true_ls2k0300 {
namespace {

struct EncoderReadResult {
    bool ok = false;
    int32_t count = 0;
};

// 从编码器字符设备读取 16 位计数值
EncoderReadResult ReadEncoderCount(const char* path) {
    EncoderReadResult result{};
    const int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return result;
    }
    int16_t raw = 0;
    const ssize_t bytes = read(fd, &raw, sizeof(raw));
    const int close_rc = close(fd);

    // The vendor encoder helper only treats `read() == -1` as failure and
    // reads a 16-bit count. The board-side char device may report a zero-byte
    // read while still updating the caller-provided buffer, so match that
    // device contract at the owning boundary instead of requiring an exact
    // byte count.
    if (bytes < 0 || close_rc != 0) {
        return result;
    }

    result.ok = true;
    result.count = static_cast<int32_t>(raw);
    return result;
}

}  // namespace

// 初始化编码器 —— 探测左右轮编码器设备可访问性
BridgeStatus InitializeEncoder() {
    BridgeStatus status{};
    if (!ReadEncoderCount(kLeftEncoderPath).ok) {
        status.detail = std::string("encoder resource unavailable: ") + kLeftEncoderPath;
        return status;
    }
    if (!ReadEncoderCount(kRightEncoderPath).ok) {
        status.detail = std::string("encoder resource unavailable: ") + kRightEncoderPath;
        return status;
    }
    status.ok = true;
    status.detail = "encoder resources probed successfully";
    return status;
}

// 读取左右编码器计数值
EncoderCounts ReadEncoderCounts() {
    EncoderCounts counts{};
    const EncoderReadResult left = ReadEncoderCount(kLeftEncoderPath);
    if (!left.ok) {
        counts.detail = std::string("encoder read failed: ") + kLeftEncoderPath;
        return counts;
    }
    const EncoderReadResult right = ReadEncoderCount(kRightEncoderPath);
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
