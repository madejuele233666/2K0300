#include "port/platform_adapter.hpp"
#include "platform/true_ls2k0300/bridge.hpp"
#include "platform/true_ls2k0300/vendor_paths.hpp"

#include <string>

namespace ls2k::platform {
namespace {

constexpr int kLeftEncoderDirectionSign = 1;
constexpr int kRightEncoderDirectionSign = -1;

class EncoderAdapter final : public port::IEncoderAdapter {
public:
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

    void Shutdown(port::DiagnosticSink& diagnostics) override {
        ready_ = false;
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "encoder.shutdown",
                          "encoder adapter shutdown complete",
                          port::NowMs()});
    }

    bool Ready() const override { return ready_; }

private:
    bool enabled_ = false;
    bool ready_ = false;
    bool adaptation_hook_ = false;
    std::string hook_name_ = "direct-match";
};

}  // namespace

std::unique_ptr<port::IEncoderAdapter> MakeEncoderAdapter() {
    return std::make_unique<EncoderAdapter>();
}

}  // namespace ls2k::platform
