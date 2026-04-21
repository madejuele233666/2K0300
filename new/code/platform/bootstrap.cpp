#include "platform/bootstrap.hpp"
#include "platform/true_ls2k0300/bridge.hpp"

#include <atomic>
#include <functional>

namespace ls2k::platform {

namespace {

class TimerAdapter final : public port::ITimerAdapter {
public:
    bool Start(const port::SubsystemProfile& profile,
               uint32_t period_ms,
               std::function<void()> callback,
               std::function<void()> on_failure,
               port::DiagnosticSink& diagnostics) override {
        Stop(diagnostics);

        if (!port::IsEnabled(profile)) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "timer.disabled",
                              "timer subsystem disabled by hardware profile",
                              port::NowMs()});
            return false;
        }

        port::DiagnosticSink* diagnostics_sink = &diagnostics;
        auto timer_failure = [this, on_failure = std::move(on_failure), diagnostics_sink]() mutable {
            const bool was_running = running_.exchange(false);
            if (!was_running) {
                return;
            }

            diagnostics_sink->Emit({port::DiagnosticLevel::kFailSafe,
                                    "timer.runtime.failure",
                                    "timer backend exited unexpectedly; escalating to control fail-safe handling",
                                    port::NowMs()});
            if (on_failure) {
                on_failure();
            }
        };

        if (profile.mode == port::SubsystemMode::kAdaptationHook) {
            const bool started = bridge_.Start(period_ms, std::move(callback), std::move(timer_failure));
            running_.store(started);
            diagnostics.Emit({started ? port::DiagnosticLevel::kWarning : port::DiagnosticLevel::kFailSafe,
                              started ? "timer.start.hook" : "timer.start.hook.failed",
                              started ? "timer routed through adaptation hook: " + profile.hook
                                      : "timer adaptation hook failed to start: " + profile.hook,
                              port::NowMs()});
            return started;
        }

        const bool started = bridge_.Start(period_ms, std::move(callback), std::move(timer_failure));
        running_.store(started);
        diagnostics.Emit({started ? port::DiagnosticLevel::kInfo : port::DiagnosticLevel::kFailSafe,
                          started ? "timer.start.pit" : "timer.start.pit.failed",
                          started ? "timer started with true_ls2k0300 timerfd bridge"
                                  : "timer direct-match timerfd bridge failed to start",
                          port::NowMs()});
        return started;
    }

    void Stop(port::DiagnosticSink& diagnostics) override {
        const bool was_running = running_.exchange(false);
        bridge_.Stop();
        if (was_running) {
            diagnostics.Emit({port::DiagnosticLevel::kInfo,
                              "timer.stop",
                              "timer bridge stopped",
                              port::NowMs()});
        }
    }

    bool Running() const override { return running_.load() && bridge_.Running(); }

private:
    std::atomic<bool> running_{false};
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
