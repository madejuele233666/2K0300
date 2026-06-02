#include "port/platform_adapter.hpp"
#include "platform/true_ls2k0300/bridge.hpp"
#include "platform/true_ls2k0300/vendor_paths.hpp"

#include <cstdlib>
#include <string>

namespace ls2k::platform {
namespace {

/// 左编码器方向符号（正向保持原始符号）
constexpr int kLeftEncoderDirectionSign = 1;
/// 右编码器方向符号（取反以匹配逻辑坐标系）
constexpr int kRightEncoderDirectionSign = -1;

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
                           "encoder.inject.invalid_env",
                           std::string("ignoring invalid fault-injection interval for ") + key + "=" + value,
                           now_ms},
                          1000);
    return 0;
}

/// @brief 编码器适配器类
///
/// 实现 port::IEncoderAdapter 接口，封装 true_ls2k0300 桥接层的
/// 编码器初始化、增量读取和关闭操作。支持 direct-match 和 adaptation-hook 两种模式。
/// 对左右编码器原始计数值应用方向符号归一化。
class EncoderAdapter final : public port::IEncoderAdapter {
public:
    /// @brief 初始化编码器适配器
    /// @param profile 硬件描述文件（检查编码器子系统是否启用及其模式）
    /// @param diagnostics 诊断输出接口
    /// @return 初始化成功返回 true
    bool Initialize(const port::HardwareProfile& profile, port::DiagnosticSink& diagnostics) override {
        if (!port::IsEnabled(profile.encoder)) {
            diagnostics.Emit({port::DiagnosticLevel::kInfo,
                              "encoder.disabled",
                              "encoder subsystem disabled by hardware profile",
                              port::NowMs()});
            enabled_ = false;
            ready_ = false;
            return true;
        }

        enabled_ = true;
        adaptation_hook_ = profile.encoder.mode == port::SubsystemMode::kAdaptationHook;
        hook_name_ = profile.encoder.hook;

        if (adaptation_hook_) {
            ready_ = true;
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "encoder.init.hook",
                              "encoder direct path bypassed; adaptation hook selected: " + hook_name_,
                              port::NowMs()});
            return true;
        }

        const true_ls2k0300::BridgeStatus init = true_ls2k0300::InitializeEncoder();
        ready_ = init.ok;
        diagnostics.Emit({ready_ ? port::DiagnosticLevel::kInfo : port::DiagnosticLevel::kFailSafe,
                          "encoder.init",
                          ready_ ? "encoder initialized through true_ls2k0300 bridge: left=" +
                                       std::string(true_ls2k0300::kLeftEncoderPath) + ", right=" +
                                       std::string(true_ls2k0300::kRightEncoderPath)
                                 : "encoder backend unavailable: " + init.detail,
                          port::NowMs()});
        if (ready_) {
            diagnostics.Emit({port::DiagnosticLevel::kInfo,
                              "encoder.normalization",
                              "encoder direct-match normalization keeps logical left=raw_left and logical right=-raw_right for direct speed samples",
                              port::NowMs()});
        }
        return ready_;
    }

    /// @brief 读取编码器增量数据
    /// @param diagnostics 诊断输出接口
    /// @return 编码器增量值（含左右计数和时间戳）
    port::EncoderDelta ReadDelta(port::DiagnosticSink& diagnostics) override {
        port::EncoderDelta out{};
        out.capture_time_ms = port::NowMs();
        if (!enabled_ || !ready_) {
            return out;
        }

        if (adaptation_hook_) {
            out.valid = false;
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kWarning,
                                   "encoder.hook.read",
                                   "encoder adaptation hook selected with no concrete phase-1 implementation: " +
                                       hook_name_,
                                   out.capture_time_ms},
                                  1000);
            return out;
        }

        ++read_count_;
        const int inject_invalid_every_n = ReadPositiveIntervalEnv(
            "LS2K_FAULT_INJECT_ENCODER_INVALID_EVERY_N", diagnostics, out.capture_time_ms);
        if (inject_invalid_every_n > 0 && read_count_ % static_cast<uint64_t>(inject_invalid_every_n) == 0) {
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kWarning,
                                   "encoder.inject.invalid",
                                   "injecting bounded Phase B invalid-encoder fault on the accepted runtime entrypoint",
                                   out.capture_time_ms},
                                  1000);
            return out;
        }

        const true_ls2k0300::EncoderCounts counts = true_ls2k0300::ReadEncoderCounts();
        if (!counts.valid) {
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kWarning,
                                   "encoder.read.invalid",
                                   counts.detail.empty() ? "encoder sample unavailable" : counts.detail,
                                   out.capture_time_ms},
                                  1000);
            return out;
        }

        out.left = counts.left * kLeftEncoderDirectionSign;
        out.right = counts.right * kRightEncoderDirectionSign;
        out.valid = true;
        port::EmitRateLimited(diagnostics,
                              {port::DiagnosticLevel::kInfo,
                               "encoder.delta.summary",
                               "logical encoder sample left=" + std::to_string(out.left) +
                                   " right=" + std::to_string(out.right) +
                                   " mean=" + std::to_string((out.left + out.right) / 2) +
                                   " diff=" + std::to_string(out.right - out.left),
                               out.capture_time_ms},
                              1000);
        return out;
    }

    /// @brief 关闭编码器适配器
    /// @param diagnostics 诊断输出接口
    void Shutdown(port::DiagnosticSink& diagnostics) override {
        ready_ = false;
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "encoder.shutdown",
                          "encoder adapter shutdown complete",
                          port::NowMs()});
    }

    /// @brief 检查编码器是否已就绪
    /// @return true 表示编码器可用
    bool Ready() const override { return ready_; }

private:
    /// 编码器子系统是否启用
    bool enabled_ = false;
    /// 编码器是否已就绪
    bool ready_ = false;
    /// 是否使用适配钩子模式
    bool adaptation_hook_ = false;
    /// 适配钩子名称
    std::string hook_name_ = "direct-match";
    /// 读取计数（用于故障注入周期性）
    uint64_t read_count_ = 0;
};

}  // namespace

/// @brief 创建编码器适配器实例
/// @return 新创建的 EncoderAdapter 智能指针
std::unique_ptr<port::IEncoderAdapter> MakeEncoderAdapter() {
    return std::make_unique<EncoderAdapter>();
}

}  // namespace ls2k::platform
