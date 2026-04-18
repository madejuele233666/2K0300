#include "platform/bootstrap.hpp"
#include "platform/true_ls2k0300/bridge.hpp"

#include <functional>

namespace ls2k::platform {

namespace {

class TimerAdapter final : public port::ITimerAdapter {
public:
    bool Start(const port::SubsystemProfile& profile,
               uint32_t period_ms,
               std::function<void()> callback,
               port::DiagnosticSink& diagnostics) override {
        Stop(diagnostics);

        if (!port::IsEnabled(profile)) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "timer.disabled",
                              "timer subsystem disabled by hardware profile",
                              port::NowMs()});
            return false;
        }

        period_ms_ = period_ms;
        callback_ = std::move(callback);

        if (profile.mode == port::SubsystemMode::kAdaptationHook) {
            const bool started = bridge_.Start(period_ms_, callback_, false);
            running_ = started;
            diagnostics.Emit({started ? port::DiagnosticLevel::kWarning : port::DiagnosticLevel::kFailSafe,
                              started ? "timer.start.hook" : "timer.start.hook.failed",
                              started ? "timer routed through adaptation hook: " + profile.hook
                                      : "timer adaptation hook failed to start: " + profile.hook,
                              port::NowMs()});
            return started;
        }

        const bool started = bridge_.Start(period_ms_, callback_, true);
        running_ = started;
        diagnostics.Emit({started ? port::DiagnosticLevel::kInfo : port::DiagnosticLevel::kFailSafe,
                          started ? "timer.start.pit" : "timer.start.pit.failed",
                          started ? "timer started with true_ls2k0300 pit bridge"
                                  : "timer direct-match pit bridge failed to start",
                          port::NowMs()});
        return started;
    }

    void Stop(port::DiagnosticSink& diagnostics) override {
        if (!running_) {
            return;
        }
        bridge_.Stop();
        running_ = false;
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "timer.stop",
                          "timer bridge stopped",
                          port::NowMs()});
    }

    bool Running() const override { return running_; }

private:
    bool running_ = false;
    uint32_t period_ms_ = 0;
    std::function<void()> callback_{};
    true_ls2k0300::TimerBridge bridge_{};
};

}  // namespace

port::PlatformBundle CreatePlatformBundle(const port::HardwareProfile&, port::DiagnosticSink&) {
    port::PlatformBundle bundle{};
    bundle.camera = MakeCameraAdapter();
    bundle.imu = MakeImuAdapter();
    bundle.encoder = MakeEncoderAdapter();
    bundle.motor = MakeMotorAdapter();
    bundle.power = MakePowerMonitorAdapter();
    bundle.params = MakeParamStore();
    bundle.timer = std::make_unique<TimerAdapter>();
    return bundle;
}

}  // namespace ls2k::platform
