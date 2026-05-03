// 电机桥接实现 —— 通过供应商 PWM/GPIO 字符设备控制差速驱动电机。
// 管理方向切换（GPIO 电平）和占空比（PWM 写入）的时序协调。

#include "platform/true_ls2k0300/bridge.hpp"

#include <algorithm>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "platform/true_ls2k0300/vendor_paths.hpp"

namespace ls2k::platform::true_ls2k0300 {

namespace {

struct MotorChannel {
    const char* pwm_path;
    const char* gpio_path;
};

struct MotorApplyState {
    bool initialized = false;
    int last_direction_sign = 1;
    bool last_pwm_zero = true;
};

struct WritableDevice {
    const char* path = nullptr;
    int fd = -1;
};

// 逻辑映射：右路 PWM/GPIO 控制左电机，左路 PWM/GPIO 控制右电机（硬件接脚交叉）
constexpr MotorChannel kLogicalLeftMotor = {kRightMotorPwmPath, kRightMotorGpioPath};
constexpr MotorChannel kLogicalRightMotor = {kLeftMotorPwmPath, kLeftMotorGpioPath};
MotorApplyState g_left_motor_state{};
MotorApplyState g_right_motor_state{};
WritableDevice g_left_pwm{kLeftMotorPwmPath, -1};
WritableDevice g_right_pwm{kRightMotorPwmPath, -1};
WritableDevice g_left_gpio{kLeftMotorGpioPath, -1};
WritableDevice g_right_gpio{kRightMotorGpioPath, -1};
bool g_use_persistent_fd = false;

// 供应商方向约定与逻辑方向相反，取负
int NormalizeLogicalDutyToVendorDirection(int logical_duty) {
    return -logical_duty;
}

// 测试路径是否可写打开
bool OpenWritable(const char* path) {
    const int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return false;
    }
    return close(fd) == 0;
}

WritableDevice* FindDevice(const char* path) {
    if (path == kLeftMotorPwmPath) {
        return &g_left_pwm;
    }
    if (path == kRightMotorPwmPath) {
        return &g_right_pwm;
    }
    if (path == kLeftMotorGpioPath) {
        return &g_left_gpio;
    }
    if (path == kRightMotorGpioPath) {
        return &g_right_gpio;
    }
    return nullptr;
}

template <typename T>
bool WriteFd(int fd, const T& value) {
    if (fd < 0) {
        return false;
    }
    const ssize_t bytes = write(fd, &value, sizeof(value));
    const bool accepted = bytes == 0 || bytes == static_cast<ssize_t>(sizeof(value));
    return accepted;
}

void ClosePersistentMotorFds() {
    for (WritableDevice* device : {&g_left_pwm, &g_right_pwm, &g_left_gpio, &g_right_gpio}) {
        if (device->fd >= 0) {
            (void)close(device->fd);
            device->fd = -1;
        }
    }
    g_use_persistent_fd = false;
}

bool OpenPersistentMotorFds() {
    ClosePersistentMotorFds();
    for (WritableDevice* device : {&g_left_pwm, &g_right_pwm, &g_left_gpio, &g_right_gpio}) {
        device->fd = open(device->path, O_WRONLY);
        if (device->fd < 0) {
            ClosePersistentMotorFds();
            return false;
        }
    }
    return true;
}

bool ProbePersistentMotorFds() {
    if (!OpenPersistentMotorFds()) {
        return false;
    }
    const uint16_t zero = 0;
    const uint8_t gpio_low = static_cast<uint8_t>('0');
    const uint8_t gpio_high = static_cast<uint8_t>('1');
    const bool ok =
        WriteFd(g_left_pwm.fd, zero) &&
        WriteFd(g_left_pwm.fd, zero) &&
        WriteFd(g_right_pwm.fd, zero) &&
        WriteFd(g_right_pwm.fd, zero) &&
        WriteFd(g_left_gpio.fd, gpio_low) &&
        WriteFd(g_left_gpio.fd, gpio_high) &&
        WriteFd(g_right_gpio.fd, gpio_low) &&
        WriteFd(g_right_gpio.fd, gpio_high);
    if (!ok) {
        ClosePersistentMotorFds();
        return false;
    }
    g_use_persistent_fd = true;
    return true;
}

// 以二进制写入字符设备（PWM 占空比或 GPIO 电平）
template <typename T>
bool WriteBinary(const char* path, const T& value) {
    if (g_use_persistent_fd) {
        WritableDevice* device = FindDevice(path);
        if (device != nullptr && WriteFd(device->fd, value)) {
            return true;
        }
        ClosePersistentMotorFds();
    }
    const int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return false;
    }
    const ssize_t bytes = write(fd, &value, sizeof(value));
    const int close_rc = close(fd);
    // Vendor PWM char devices may report success with a zero-length write
    // even though the command was accepted, while GPIO nodes return the
    // payload length. Treat both forms as success at the owning boundary.
    const bool accepted = bytes == 0 || bytes == static_cast<ssize_t>(sizeof(value));
    return accepted && close_rc == 0;
}

// 施加单电机命令 —— 方向变化时先清零 PWM → 写 GPIO → 再写 PWM
BridgeStatus ApplyOne(const MotorChannel& channel, MotorApplyState& state, int signed_duty) {
    BridgeStatus status{};
    const int vendor_signed_duty = NormalizeLogicalDutyToVendorDirection(signed_duty);
    const int clamped = std::clamp(vendor_signed_duty, -9000, 9000);
    const int direction_sign = clamped < 0 ? -1 : 1;
    const uint8_t gpio_level = static_cast<uint8_t>((direction_sign > 0 ? 1 : 0) + 0x30);
    const uint16_t pwm_duty = static_cast<uint16_t>(std::abs(clamped));
    const uint16_t zero = 0;
    const bool direction_changed = !state.initialized || direction_sign != state.last_direction_sign;
    const bool need_preclear = direction_changed && !state.last_pwm_zero;

    if (need_preclear && !WriteBinary(channel.pwm_path, zero)) {
        status.detail = std::string("motor PWM write failed: ") + channel.pwm_path;
        return status;
    }
    if (direction_changed && !WriteBinary(channel.gpio_path, gpio_level)) {
        status.detail = std::string("motor GPIO write failed: ") + channel.gpio_path;
        return status;
    }
    if (!WriteBinary(channel.pwm_path, pwm_duty)) {
        status.detail = std::string("motor PWM write failed: ") + channel.pwm_path;
        return status;
    }

    state.initialized = true;
    state.last_direction_sign = direction_sign;
    state.last_pwm_zero = pwm_duty == 0;
    status.ok = true;
    status.detail = "motor command applied";
    return status;
}

// 探测电机设备路径的可访问性
BridgeStatus ProbeMotorPath(const char* path) {
    BridgeStatus status{};
    if (!OpenWritable(path)) {
        status.detail = std::string("motor resource unavailable: ") + path;
        return status;
    }
    status.ok = true;
    status.detail = path;
    return status;
}

}  // namespace

// 初始化电机 —— 探测 PWM/GPIO 路径可写性
BridgeStatus InitializeMotor() {
    const bool persistent_ok = ProbePersistentMotorFds();
    for (const char* path : {kLeftMotorPwmPath, kRightMotorPwmPath, kLeftMotorGpioPath, kRightMotorGpioPath}) {
        const BridgeStatus probe = ProbeMotorPath(path);
        if (!probe.ok) {
            return probe;
        }
    }
    g_left_motor_state = {};
    g_right_motor_state = {};
    return {true,
            persistent_ok ? "motor PWM/GPIO resources probed successfully with persistent fd"
                          : "motor PWM/GPIO resources probed successfully with open/write/close fallback"};
}

// 施加电机命令（左右 PWM）。任一路失败时回滚禁用全部输出。
BridgeStatus ApplyMotorCommand(int left_pwm, int right_pwm) {
    const BridgeStatus left = ApplyOne(kLogicalLeftMotor, g_left_motor_state, left_pwm);
    if (!left.ok) {
        // Best-effort rollback keeps hardware fail-safe if one side write path failed mid-apply.
        const BridgeStatus rollback = DisableMotorOutput();
        if (!rollback.ok) {
            return {false, left.detail + "; rollback failed: " + rollback.detail};
        }
        return left;
    }
    const BridgeStatus right = ApplyOne(kLogicalRightMotor, g_right_motor_state, right_pwm);
    if (!right.ok) {
        const BridgeStatus rollback = DisableMotorOutput();
        if (!rollback.ok) {
            return {false, right.detail + "; rollback failed: " + rollback.detail};
        }
        return right;
    }
    return right;
}

// 禁用电机输出 —— 将左右 PWM 清零
BridgeStatus DisableMotorOutput() {
    const uint16_t zero = 0;
    if (!WriteBinary(kLeftMotorPwmPath, zero)) {
        return {false, std::string("motor PWM write failed: ") + kLeftMotorPwmPath};
    }
    if (!WriteBinary(kRightMotorPwmPath, zero)) {
        return {false, std::string("motor PWM write failed: ") + kRightMotorPwmPath};
    }
    g_left_motor_state.initialized = true;
    g_right_motor_state.initialized = true;
    g_left_motor_state.last_pwm_zero = true;
    g_right_motor_state.last_pwm_zero = true;
    return {true, "motor outputs disabled"};
}

}  // namespace ls2k::platform::true_ls2k0300
