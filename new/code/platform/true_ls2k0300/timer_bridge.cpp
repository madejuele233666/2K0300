#include "platform/true_ls2k0300/bridge.hpp"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <memory>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <thread>
#include <utility>
#include <unistd.h>

namespace ls2k::platform::true_ls2k0300 {

namespace {

timespec ToTimespec(uint32_t period_ms) {
    timespec spec{};
    spec.tv_sec = static_cast<time_t>(period_ms / 1000U);
    spec.tv_nsec = static_cast<long>((period_ms % 1000U) * 1000000UL);
    return spec;
}

class ScopedFd {
public:
    ScopedFd() = default;
    explicit ScopedFd(int fd) : fd_(fd) {}
    ~ScopedFd() { Reset(); }

    ScopedFd(ScopedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

    ScopedFd& operator=(ScopedFd&& other) noexcept {
        if (this != &other) {
            Reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    int Get() const { return fd_; }
    bool Valid() const { return fd_ >= 0; }

    void Reset(int fd = -1) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_ = -1;
};

class PeriodicTimerBackend {
public:
    virtual ~PeriodicTimerBackend() = default;
    virtual bool Start(uint32_t period_ms, std::function<void()> callback, std::function<void()> on_failure) = 0;
    virtual void Stop() = 0;
    virtual bool Running() const = 0;
};

class TimerfdBackend final : public PeriodicTimerBackend {
public:
    ~TimerfdBackend() override { Stop(); }

    bool Start(uint32_t period_ms, std::function<void()> callback, std::function<void()> on_failure) override {
        Stop();
        if (!callback || period_ms == 0U) {
            return false;
        }

        ScopedFd timer_fd(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC));
        if (!timer_fd.Valid() || !ArmTimerFd(timer_fd.Get(), period_ms)) {
            return false;
        }

        ScopedFd stop_fd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
        if (!stop_fd.Valid()) {
            return false;
        }

        timer_fd_ = std::move(timer_fd);
        stop_fd_ = std::move(stop_fd);
        running_.store(true);

        try {
            worker_ = std::thread([this, callback = std::move(callback), on_failure = std::move(on_failure)]() mutable {
                Run(std::move(callback), std::move(on_failure));
            });
        } catch (...) {
            running_.store(false);
            timer_fd_.Reset();
            stop_fd_.Reset();
            return false;
        }

        return true;
    }

    void Stop() override {
        if (!running_.load() && !worker_.joinable()) {
            timer_fd_.Reset();
            stop_fd_.Reset();
            return;
        }

        running_.store(false);
        SignalStop();
        if (worker_.joinable()) {
            if (worker_.get_id() == std::this_thread::get_id()) {
                return;
            }
            worker_.join();
        }
        timer_fd_.Reset();
        stop_fd_.Reset();
    }

    bool Running() const override { return running_.load(); }

private:
    static bool ArmTimerFd(int timer_fd, uint32_t period_ms) {
        itimerspec schedule{};
        schedule.it_value = ToTimespec(period_ms);
        schedule.it_interval = schedule.it_value;
        return timerfd_settime(timer_fd, 0, &schedule, nullptr) == 0;
    }

    void SignalStop() {
        if (!stop_fd_.Valid()) {
            return;
        }

        const uint64_t signal = 1;
        while (true) {
            ssize_t rc = write(stop_fd_.Get(), &signal, sizeof(signal));
            if (rc == static_cast<ssize_t>(sizeof(signal)) || (rc < 0 && errno == EAGAIN)) {
                return;
            }
            if (rc < 0 && errno == EINTR) {
                continue;
            }
            return;
        }
    }

    static void DrainStopSignal(int stop_fd) {
        uint64_t signal = 0;
        while (read(stop_fd, &signal, sizeof(signal)) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
    }

    void Run(std::function<void()> callback, std::function<void()> on_failure) {
        pollfd descriptors[2]{};
        descriptors[0].fd = timer_fd_.Get();
        descriptors[0].events = POLLIN;
        descriptors[1].fd = stop_fd_.Get();
        descriptors[1].events = POLLIN;
        bool unexpected_exit = false;

        while (running_.load()) {
            int poll_rc = -1;
            do {
                poll_rc = poll(descriptors, 2, -1);
            } while (poll_rc < 0 && errno == EINTR && running_.load());

            if (poll_rc < 0) {
                unexpected_exit = running_.load();
                break;
            }
            if (poll_rc == 0) {
                continue;
            }

            if ((descriptors[1].revents & POLLIN) != 0) {
                DrainStopSignal(stop_fd_.Get());
                break;
            }

            if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                unexpected_exit = running_.load();
                break;
            }

            if ((descriptors[0].revents & POLLIN) != 0) {
                uint64_t expirations = 0;
                ssize_t read_rc = -1;
                do {
                    read_rc = read(timer_fd_.Get(), &expirations, sizeof(expirations));
                } while (read_rc < 0 && errno == EINTR);

                if (read_rc != static_cast<ssize_t>(sizeof(expirations))) {
                    unexpected_exit = running_.load();
                    break;
                }

                if (expirations > 0 && running_.load()) {
                    try {
                        callback();
                    } catch (...) {
                        unexpected_exit = true;
                        break;
                    }
                }
            }
        }

        running_.store(false);
        if (unexpected_exit && on_failure) {
            try {
                on_failure();
            } catch (...) {
            }
        }
    }

    std::atomic<bool> running_{false};
    ScopedFd timer_fd_{};
    ScopedFd stop_fd_{};
    std::thread worker_{};
};

}  // namespace

struct TimerBridge::Impl {
    std::unique_ptr<PeriodicTimerBackend> backend = std::make_unique<TimerfdBackend>();
};

TimerBridge::TimerBridge() : impl_(std::make_unique<Impl>()) {}

TimerBridge::~TimerBridge() {
    Stop();
}

bool TimerBridge::Start(uint32_t period_ms, std::function<void()> callback, std::function<void()> on_failure) {
    return impl_->backend->Start(period_ms, std::move(callback), std::move(on_failure));
}

void TimerBridge::Stop() {
    impl_->backend->Stop();
}

bool TimerBridge::Running() const {
    return impl_->backend->Running();
}

}  // namespace ls2k::platform::true_ls2k0300
