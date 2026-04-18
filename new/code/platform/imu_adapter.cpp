#include "port/platform_adapter.hpp"
#include "platform/true_ls2k0300/bridge.hpp"

#include <array>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <string>

namespace ls2k::platform {
namespace {

constexpr float kGravityMps2 = 9.80665F;
constexpr float kAccelMetersPerSecPerCount = 0.0001220F * kGravityMps2;
constexpr float kGyroRadPerSecPerCount = 0.0010641F;
constexpr float kAccelFilterNewWeight = 0.9F;
constexpr float kAccelFilterOldWeight = 0.1F;
constexpr int kImuBiasCalibrationSamples = 32;
constexpr uint32_t kImuContinuityEvidenceSamples = 32;

int ReadPositiveIntervalEnv(const char* key, port::DiagnosticSink& diagnostics, uint64_t now_ms) {
    const char* value = std::getenv(key);
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    try {
        const int parsed = std::stoi(value);
        if (parsed > 0) {
            return parsed;
        }
    } catch (...) {
    }
    port::EmitRateLimited(diagnostics,
                          {port::DiagnosticLevel::kWarning,
                           "imu.inject.invalid_env",
                           std::string("ignoring invalid fault-injection interval for ") + key + "=" + value,
                           now_ms},
                          1000);
    return 0;
}

const char* ImuTypeName(uint8_t imu_type) {
    switch (imu_type) {
        case 0x10:
            return "imu660ra";
        case 0x11:
            return "imu660rb";
        case 0x12:
            return "imu963ra";
        default:
            return "unknown";
    }
}

class ImuAdapter final : public port::IImuAdapter {
public:
    bool Initialize(const port::HardwareProfile& profile, port::DiagnosticSink& diagnostics) override {
        if (!port::IsEnabled(profile.imu)) {
            diagnostics.Emit({port::DiagnosticLevel::kInfo,
                              "imu.disabled",
                              "imu subsystem disabled by hardware profile",
                              port::NowMs()});
            enabled_ = false;
            ready_ = false;
            return true;
        }

        enabled_ = true;
        adaptation_hook_ = profile.imu.mode == port::SubsystemMode::kAdaptationHook;
        hook_name_ = profile.imu.hook;

        if (adaptation_hook_) {
            ready_ = true;
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "imu.init.hook",
                              "imu direct path bypassed; adaptation hook selected: " + hook_name_,
                              port::NowMs()});
            return true;
        }

        const true_ls2k0300::ImuInitResult init = true_ls2k0300::InitializeImu();
        ready_ = init.ready;
        ResetCalibrationState();
        diagnostics.Emit({ready_ ? port::DiagnosticLevel::kInfo : port::DiagnosticLevel::kFailSafe,
                          "imu.init",
                          ready_ ? "imu initialized through true_ls2k0300 bridge: " + init.detail
                                 : "imu unavailable: " + init.detail,
                          port::NowMs()});
        diagnostics.Emit({ready_ ? port::DiagnosticLevel::kInfo : port::DiagnosticLevel::kWarning,
                          "imu.detect",
                          std::string("imu detection path selected: ") + ImuTypeName(init.imu_type) +
                              " source=" + (init.source.empty() ? "unresolved" : init.source),
                          port::NowMs()});
        if (ready_) {
            PrimeBiasCalibration(diagnostics);
        }
        return ready_;
    }

    port::ImuSample Read(port::DiagnosticSink& diagnostics) override {
        port::ImuSample out{};
        out.capture_time_ms = port::NowMs();
        if (!enabled_ || !ready_) {
            return out;
        }

        if (adaptation_hook_) {
            out.valid = false;
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kWarning,
                                   "imu.hook.read",
                                   "imu adaptation hook selected with no concrete phase-1 implementation: " +
                                       hook_name_,
                                   out.capture_time_ms},
                                  1000);
            return out;
        }

        ++read_count_;
        const int inject_invalid_every_n =
            ReadPositiveIntervalEnv("LS2K_FAULT_INJECT_IMU_INVALID_EVERY_N", diagnostics, out.capture_time_ms);
        if (inject_invalid_every_n > 0 && read_count_ % static_cast<uint64_t>(inject_invalid_every_n) == 0) {
            if (valid_streak_ > 0) {
                continuity_reported_ = false;
            }
            valid_streak_ = 0;
            ++invalid_streak_;
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kWarning,
                                   "imu.inject.invalid",
                                   "injecting bounded Phase B invalid-IMU fault on the accepted runtime entrypoint",
                                   out.capture_time_ms},
                                  1000);
            return out;
        }

        const true_ls2k0300::ImuBridgeSample sample = true_ls2k0300::ReadImuSample();
        if (!sample.valid) {
            if (valid_streak_ > 0) {
                continuity_reported_ = false;
            }
            valid_streak_ = 0;
            ++invalid_streak_;
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kWarning,
                                   "imu.read.invalid",
                                   sample.detail.empty() ? "imu sample unavailable" : sample.detail,
                                   out.capture_time_ms},
                                  1000);
            return out;
        }

        const std::array<float, 3> acc_mps2 = {static_cast<float>(sample.acc_x) * kAccelMetersPerSecPerCount,
                                               static_cast<float>(sample.acc_y) * kAccelMetersPerSecPerCount,
                                               static_cast<float>(sample.acc_z) * kAccelMetersPerSecPerCount};
        if (!have_filtered_acc_) {
            filtered_acc_ = acc_mps2;
            have_filtered_acc_ = true;
        } else {
            for (std::size_t i = 0; i < filtered_acc_.size(); ++i) {
                filtered_acc_[i] =
                    acc_mps2[i] * kAccelFilterNewWeight + filtered_acc_[i] * kAccelFilterOldWeight;
            }
        }

        if (invalid_streak_ > 0) {
            diagnostics.Emit({port::DiagnosticLevel::kInfo,
                              "imu.read.recovered",
                              "imu sample stream recovered after " + std::to_string(invalid_streak_) +
                                  " invalid read(s)",
                              out.capture_time_ms});
            invalid_streak_ = 0;
        }

        ++valid_streak_;
        if (!continuity_reported_ && valid_streak_ >= kImuContinuityEvidenceSamples) {
            diagnostics.Emit({port::DiagnosticLevel::kInfo,
                              "imu.continuity.ready",
                              "imu sample stream stayed valid for " +
                                  std::to_string(kImuContinuityEvidenceSamples) +
                                  " consecutive reads after bridge normalization",
                              out.capture_time_ms});
            continuity_reported_ = true;
        }

        out.valid = true;
        out.acc_x = filtered_acc_[0];
        out.acc_y = filtered_acc_[1];
        out.acc_z = filtered_acc_[2];
        out.gyro_x =
            (static_cast<float>(sample.gyro_x) - gyro_bias_raw_[0]) * kGyroRadPerSecPerCount;
        out.gyro_y =
            (static_cast<float>(sample.gyro_y) - gyro_bias_raw_[1]) * kGyroRadPerSecPerCount;
        out.gyro_z =
            (static_cast<float>(sample.gyro_z) - gyro_bias_raw_[2]) * kGyroRadPerSecPerCount;
        port::EmitRateLimited(diagnostics,
                              {port::DiagnosticLevel::kInfo,
                               "imu.sample.summary",
                               "imu normalized sample acc_z=" + std::to_string(out.acc_z) +
                                   "mps2 gyro_z=" + std::to_string(out.gyro_z) +
                                   "radps valid_streak=" + std::to_string(valid_streak_),
                               out.capture_time_ms},
                              1000);
        return out;
    }

    void Shutdown(port::DiagnosticSink& diagnostics) override {
        ready_ = false;
        ResetCalibrationState();
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "imu.shutdown",
                          "imu adapter shutdown complete",
                          port::NowMs()});
    }

    bool Ready() const override { return ready_; }

private:
    void ResetCalibrationState() {
        gyro_bias_raw_ = {};
        filtered_acc_ = {};
        have_filtered_acc_ = false;
        valid_streak_ = 0;
        invalid_streak_ = 0;
        continuity_reported_ = false;
    }

    void PrimeBiasCalibration(port::DiagnosticSink& diagnostics) {
        std::array<double, 3> gyro_sum{};
        int collected = 0;
        for (int i = 0; i < kImuBiasCalibrationSamples; ++i) {
            const true_ls2k0300::ImuBridgeSample sample = true_ls2k0300::ReadImuSample();
            if (!sample.valid) {
                continue;
            }
            if (sample.gyro_x == 0 && sample.gyro_y == 0 && sample.gyro_z == 0) {
                continue;
            }
            gyro_sum[0] += sample.gyro_x;
            gyro_sum[1] += sample.gyro_y;
            gyro_sum[2] += sample.gyro_z;
            ++collected;
        }

        if (collected == 0) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "imu.calibration.partial",
                              "imu gyro zero-bias calibration could not collect valid startup samples; using raw origin",
                              port::NowMs()});
            return;
        }

        for (std::size_t i = 0; i < gyro_bias_raw_.size(); ++i) {
            gyro_bias_raw_[i] = static_cast<float>(gyro_sum[i] / collected);
        }

        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "imu.calibration.ready",
                          "imu gyro zero-bias calibrated from " + std::to_string(collected) +
                              " startup sample(s)",
                          port::NowMs()});
    }

    bool enabled_ = false;
    bool ready_ = false;
    bool adaptation_hook_ = false;
    std::string hook_name_ = "direct-match";
    std::array<float, 3> gyro_bias_raw_{};
    std::array<float, 3> filtered_acc_{};
    bool have_filtered_acc_ = false;
    uint32_t valid_streak_ = 0;
    uint32_t invalid_streak_ = 0;
    bool continuity_reported_ = false;
    uint64_t read_count_ = 0;
};

}  // namespace

std::unique_ptr<port::IImuAdapter> MakeImuAdapter() {
    return std::make_unique<ImuAdapter>();
}

}  // namespace ls2k::platform
