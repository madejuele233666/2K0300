#include <cstdlib>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "runtime/startup.hpp"

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class NullDiagnostics final : public ls2k::port::DiagnosticSink {
public:
    void Emit(const ls2k::port::DiagnosticEvent&) override {}
};

class FakeCameraAdapter final : public ls2k::port::ICameraAdapter {
public:
    bool Initialize(const ls2k::port::HardwareProfile&,
                    const ls2k::port::RuntimeParameters&,
                    ls2k::port::DiagnosticSink&) override {
        ready_ = true;
        return true;
    }
    ls2k::port::CameraCapture Capture(ls2k::port::DiagnosticSink&) override { return {}; }
    void Shutdown(ls2k::port::DiagnosticSink&) override {}
    bool Ready() const override { return ready_; }

private:
    bool ready_ = false;
};

class FakeImuAdapter final : public ls2k::port::IImuAdapter {
public:
    bool Initialize(const ls2k::port::HardwareProfile&, ls2k::port::DiagnosticSink&) override {
        ready_ = true;
        return true;
    }
    ls2k::port::ImuSample Read(ls2k::port::DiagnosticSink&) override { return {}; }
    void Shutdown(ls2k::port::DiagnosticSink&) override {}
    bool Ready() const override { return ready_; }

private:
    bool ready_ = false;
};

class FakeEncoderAdapter final : public ls2k::port::IEncoderAdapter {
public:
    bool Initialize(const ls2k::port::HardwareProfile&, ls2k::port::DiagnosticSink&) override {
        ready_ = true;
        return true;
    }
    ls2k::port::EncoderDelta ReadDelta(ls2k::port::DiagnosticSink&) override { return {}; }
    void Shutdown(ls2k::port::DiagnosticSink&) override {}
    bool Ready() const override { return ready_; }

private:
    bool ready_ = false;
};

class FakeMotorAdapter final : public ls2k::port::IMotorAdapter {
public:
    bool Initialize(const ls2k::port::HardwareProfile&, ls2k::port::DiagnosticSink&) override {
        ready_ = true;
        return true;
    }
    bool Apply(const ls2k::port::ActuatorCommand&, ls2k::port::DiagnosticSink&) override { return true; }
    void Disable(ls2k::port::DiagnosticSink&) override {}
    void Shutdown(ls2k::port::DiagnosticSink&) override {}
    bool Ready() const override { return ready_; }

private:
    bool ready_ = false;
};

class FakeTimerAdapter final : public ls2k::port::ITimerAdapter {
public:
    bool Start(const ls2k::port::SubsystemProfile&,
               std::uint32_t,
               std::function<void()>,
               std::function<void()>,
               ls2k::port::DiagnosticSink&) override {
        running_ = true;
        return true;
    }
    void Stop(ls2k::port::DiagnosticSink&) override { running_ = false; }
    bool Running() const override { return running_; }

private:
    bool running_ = false;
};

class FakePowerMonitorAdapter final : public ls2k::port::IPowerMonitorAdapter {
public:
    bool Initialize(ls2k::port::DiagnosticSink&) override {
        initialized = true;
        return true;
    }
    void ConfigureLowVoltageThreshold(int raw_threshold, ls2k::port::DiagnosticSink&) override {
        configured = true;
        configured_threshold = raw_threshold;
        ++call_index;
        configure_call_index = call_index;
    }
    ls2k::port::LowVoltageSample SampleLowVoltage(ls2k::port::DiagnosticSink&) override {
        ++call_index;
        sample_call_index = call_index;
        sampled_before_configure = !configured;
        ls2k::port::LowVoltageSample sample{};
        sample.valid = true;
        sample.emergency = false;
        sample.raw_value = 999;
        sample.threshold = configured_threshold;
        sample.source = "fake-power";
        return sample;
    }
    bool Ready() const override { return initialized; }

    bool initialized = false;
    bool configured = false;
    bool sampled_before_configure = false;
    int configured_threshold = 0;
    int call_index = 0;
    int configure_call_index = 0;
    int sample_call_index = 0;
};

class FakeParamStore final : public ls2k::port::IParamStore {
public:
    bool LoadRuntimeParameters(const std::string&,
                               ls2k::port::RuntimeParameters&,
                               ls2k::port::DiagnosticSink&) override {
        return false;
    }
    bool LoadHardwareProfile(const std::string&,
                             ls2k::port::HardwareProfile&,
                             ls2k::port::DiagnosticSink&) override {
        return false;
    }
    void ApplyStartupCritical(ls2k::port::RuntimeParameters& params,
                              ls2k::port::DiagnosticSink&) override {
        params.startup_critical_applied = true;
    }
};

ls2k::port::HardwareProfile DirectProfile() {
    ls2k::port::HardwareProfile profile{};
    profile.camera = {ls2k::port::SubsystemMode::kDirectMatch, "fake-camera"};
    profile.imu = {ls2k::port::SubsystemMode::kDirectMatch, "fake-imu"};
    profile.encoder = {ls2k::port::SubsystemMode::kDirectMatch, "fake-encoder"};
    profile.motor = {ls2k::port::SubsystemMode::kDirectMatch, "fake-motor"};
    profile.timer = {ls2k::port::SubsystemMode::kDirectMatch, "fake-timer"};
    profile.persistence = {ls2k::port::SubsystemMode::kDirectMatch, "fake-persistence"};
    return profile;
}

}  // namespace

int main() {
    try {
        unsetenv("LS2K_ALLOW_DEGRADED_STARTUP");
        auto power = std::make_unique<FakePowerMonitorAdapter>();
        FakePowerMonitorAdapter* power_ptr = power.get();

        ls2k::port::PlatformBundle platform{};
        platform.camera = std::make_unique<FakeCameraAdapter>();
        platform.imu = std::make_unique<FakeImuAdapter>();
        platform.encoder = std::make_unique<FakeEncoderAdapter>();
        platform.motor = std::make_unique<FakeMotorAdapter>();
        platform.timer = std::make_unique<FakeTimerAdapter>();
        platform.power = std::move(power);
        platform.params = std::make_unique<FakeParamStore>();

        ls2k::port::RuntimeParameters params{};
        params.low_voltage_raw_threshold = 512;
        ls2k::runtime::RuntimeState state{};
        NullDiagnostics diagnostics{};

        Expect(ls2k::runtime::RunStartup(DirectProfile(), params, platform, state, diagnostics),
               "startup must pass with fake direct-match adapters");
        Expect(power_ptr->configured, "startup must configure low-voltage threshold");
        Expect(power_ptr->configured_threshold == 512,
               "startup must pass runtime low-voltage threshold to power adapter");
        Expect(!power_ptr->sampled_before_configure,
               "startup must configure threshold before first low-voltage sample");
        Expect(power_ptr->configure_call_index > 0 && power_ptr->sample_call_index > 0 &&
                   power_ptr->configure_call_index < power_ptr->sample_call_index,
               "configure call must precede first sample call");
        Expect(state.startup_complete, "startup state must be complete");
    } catch (const std::exception& error) {
        std::cerr << "startup_low_voltage_order_test failed: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
    std::cout << "startup_low_voltage_order_test passed\n";
    return EXIT_SUCCESS;
}
