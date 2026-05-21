#include "port/platform_adapter.hpp"
#include "platform/true_ls2k0300/bridge.hpp"

#include <array>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <string>

namespace ls2k::platform {
namespace {

/// 重力加速度常数（m/s^2）
constexpr float kGravityMps2 = 9.80665F;
/// 加速度计每 LSB 对应的物理值（m/s^2）
constexpr float kAccelMetersPerSecPerCount = 0.0001220F * kGravityMps2;
/// 陀螺仪每 LSB 对应的物理值（rad/s）
constexpr float kGyroRadPerSecPerCount = 0.0010641F;
/// 加速度低通滤波器新数据权重
constexpr float kAccelFilterNewWeight = 0.9F;
/// 加速度低通滤波器旧数据权重
constexpr float kAccelFilterOldWeight = 0.1F;
/// 陀螺仪零偏校准需要的采样数量
constexpr int kImuBiasCalibrationSamples = 32;
/// 用于确认数据流连续性的有效样本数量阈值
constexpr uint32_t kImuContinuityEvidenceSamples = 32;

/// @brief 从环境变量读取正整数（用于故障注入间隔配置）
/// @param key 环境变量名
/// @param diagnostics 诊断输出接口
/// @param now_ms 当前时间戳
/// @return 解析得到的正整数，无效或未设置时返回 0
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

/// @brief 根据 IMU 类型码返回可读的名称字符串
/// @param imu_type IMU 类型标识字节
/// @return IMU 型号名称（如 "imu660ra"）
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

/// @brief IMU 适配器类
///
/// 实现 port::IImuAdapter 接口，封装 true_ls2k0300 桥接层的
/// IMU 初始化、样本读取和关闭操作。支持 direct-match 和 adaptation-hook 两种模式。
/// 内部包含加速度低通滤波和陀螺仪零偏校准逻辑。
class ImuAdapter final : public port::IImuAdapter {
public:
    /// @brief 初始化 IMU 适配器
    /// @param profile 硬件描述文件（检查 IMU 子系统是否启用及其模式）
    /// @param diagnostics 诊断输出接口
    /// @return 初始化成功返回 true
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

    /// @brief 读取一帧 IMU 样本数据
    /// @param diagnostics 诊断输出接口
    /// @return 归一化后的 IMU 样本（含加速度和角速度）
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

    /// @brief 关闭 IMU 适配器
    /// @param diagnostics 诊断输出接口
    void Shutdown(port::DiagnosticSink& diagnostics) override {
        ready_ = false;
        ResetCalibrationState();
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "imu.shutdown",
                          "imu adapter shutdown complete",
                          port::NowMs()});
    }

    /// @brief 检查 IMU 是否已就绪
    /// @return true 表示 IMU 可用
    bool Ready() const override { return ready_; }

private:
    /// @brief 重置所有校准和连续性状态
    void ResetCalibrationState() {
        gyro_bias_raw_ = {};
        filtered_acc_ = {};
        have_filtered_acc_ = false;
        valid_streak_ = 0;
        invalid_streak_ = 0;
        continuity_reported_ = false;
    }

    /// @brief 执行初始陀螺仪零偏校准
    ///
    /// 在适配器初始化时采集若干静态样本来估算陀螺仪的零偏，
    /// 后续读取时将减去该零偏值以获得更准确的角速度。
    /// @param diagnostics 诊断输出接口
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

    /// IMU 子系统是否启用
    bool enabled_ = false;
    /// IMU 是否已就绪
    bool ready_ = false;
    /// 是否使用适配钩子模式
    bool adaptation_hook_ = false;
    /// 适配钩子名称
    std::string hook_name_ = "direct-match";
    /// 陀螺仪 X/Y/Z 轴零偏原始计数值
    std::array<float, 3> gyro_bias_raw_{};
    /// 加速度低通滤波后的 X/Y/Z 值（m/s^2）
    std::array<float, 3> filtered_acc_{};
    /// 是否已获得初始加速度滤波值
    bool have_filtered_acc_ = false;
    /// 连续有效读取计数
    uint32_t valid_streak_ = 0;
    /// 连续无效读取计数
    uint32_t invalid_streak_ = 0;
    /// 是否已上报连续性就绪诊断
    bool continuity_reported_ = false;
    /// 读取计数（用于故障注入周期性）
    uint64_t read_count_ = 0;
};

}  // namespace

/// @brief 创建 IMU 适配器实例
/// @return 新创建的 ImuAdapter 智能指针
std::unique_ptr<port::IImuAdapter> MakeImuAdapter() {
    return std::make_unique<ImuAdapter>();
}

}  // namespace ls2k::platform
