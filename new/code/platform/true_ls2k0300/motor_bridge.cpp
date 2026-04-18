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

constexpr MotorChannel kLogicalLeftMotor = {kRightMotorPwmPath, kRightMotorGpioPath};
constexpr MotorChannel kLogicalRightMotor = {kLeftMotorPwmPath, kLeftMotorGpioPath};

int NormalizeLogicalDutyToVendorDirection(int logical_duty) {
    return -logical_duty;
}

bool OpenWritable(const char* path) {
    const int fd = open(path, O_WRONLY);
    if (fd < 0) {
        return false;
    }
    return close(fd) == 0;
}

template <typename T>
bool WriteBinary(const char* path, const T& value) {
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

BridgeStatus ApplyOne(const char* pwm_path, const char* gpio_path, int signed_duty) {
    BridgeStatus status{};
    const int vendor_signed_duty =
        NormalizeLogicalDutyToVendorDirection(signed_duty);
    const int clamped = std::clamp(vendor_signed_duty, -9000, 9000);
    const uint8_t gpio_level = static_cast<uint8_t>((clamped >= 0 ? 1 : 0) + 0x30);
    const uint16_t pwm_duty = static_cast<uint16_t>(std::abs(clamped));
    const uint16_t zero = 0;

    // Clear duty before any direction update so sign flips cannot momentarily
    // energize the previous non-zero PWM under the new GPIO direction.
    if (!WriteBinary(pwm_path, zero)) {
        status.detail = std::string("motor PWM write failed: ") + pwm_path;
        return status;
    }
    if (!WriteBinary(gpio_path, gpio_level)) {
        status.detail = std::string("motor GPIO write failed: ") + gpio_path;
        return status;
    }
    if (!WriteBinary(pwm_path, pwm_duty)) {
        status.detail = std::string("motor PWM write failed: ") + pwm_path;
        return status;
    }

    status.ok = true;
    status.detail = "motor command applied";
    return status;
}

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

BridgeStatus InitializeMotor() {
    for (const char* path : {kLeftMotorPwmPath, kRightMotorPwmPath, kLeftMotorGpioPath, kRightMotorGpioPath}) {
        const BridgeStatus probe = ProbeMotorPath(path);
        if (!probe.ok) {
            return probe;
        }
    }
    return {true, "motor PWM/GPIO resources probed successfully"};
}

BridgeStatus ApplyMotorCommand(int left_pwm, int right_pwm) {
    const BridgeStatus left = ApplyOne(kLogicalLeftMotor.pwm_path, kLogicalLeftMotor.gpio_path, left_pwm);
    if (!left.ok) {
        // Best-effort rollback keeps hardware fail-safe if one side write path failed mid-apply.
        const BridgeStatus rollback = DisableMotorOutput();
        if (!rollback.ok) {
            return {false, left.detail + "; rollback failed: " + rollback.detail};
        }
        return left;
    }
    const BridgeStatus right =
        ApplyOne(kLogicalRightMotor.pwm_path, kLogicalRightMotor.gpio_path, right_pwm);
    if (!right.ok) {
        const BridgeStatus rollback = DisableMotorOutput();
        if (!rollback.ok) {
            return {false, right.detail + "; rollback failed: " + rollback.detail};
        }
        return right;
    }
    return right;
}

BridgeStatus DisableMotorOutput() {
    const uint16_t zero = 0;
    if (!WriteBinary(kLeftMotorPwmPath, zero)) {
        return {false, std::string("motor PWM write failed: ") + kLeftMotorPwmPath};
    }
    if (!WriteBinary(kRightMotorPwmPath, zero)) {
        return {false, std::string("motor PWM write failed: ") + kRightMotorPwmPath};
    }
    return {true, "motor outputs disabled"};
}

}  // namespace ls2k::platform::true_ls2k0300
