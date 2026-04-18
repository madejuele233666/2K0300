#include "platform/true_ls2k0300/bridge.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

#include "zf_driver_pit.h"

namespace ls2k::platform::true_ls2k0300 {

struct TimerBridge::Impl {
    std::atomic<bool> running{false};
    std::thread worker{};
    std::unique_ptr<Pit_timer> vendor_timer{};
    bool using_vendor_timer = false;
};

TimerBridge::TimerBridge() : impl_(std::make_unique<Impl>()) {}

TimerBridge::~TimerBridge() {
    Stop();
}

bool TimerBridge::Start(uint32_t period_ms, std::function<void()> callback, bool use_vendor_timer) {
    Stop();
    if (!callback) {
        return false;
    }

    impl_->running = true;
    impl_->using_vendor_timer = use_vendor_timer;

    if (use_vendor_timer) {
        impl_->vendor_timer = std::make_unique<Pit_timer>(std::chrono::milliseconds(period_ms), std::move(callback));
        return true;
    }

    impl_->worker = std::thread([impl = impl_.get(), period_ms, callback = std::move(callback)]() mutable {
        const auto period = std::chrono::milliseconds(period_ms);
        while (impl->running) {
            const auto start = std::chrono::steady_clock::now();
            callback();
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            if (elapsed < period) {
                std::this_thread::sleep_for(period - elapsed);
            }
        }
    });
    return true;
}

void TimerBridge::Stop() {
    impl_->running = false;
    impl_->vendor_timer.reset();
    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
    impl_->using_vendor_timer = false;
}

bool TimerBridge::Running() const {
    return impl_->running;
}

}  // namespace ls2k::platform::true_ls2k0300
