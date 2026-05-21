// 定时器桥接实现 —— 基于 timerfd + eventfd + poll 的周期定时器。
// 支持启动/停止、超时回调、故障回调和线程安全的生命周期管理。

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

// 毫秒转 timespec 结构
timespec ToTimespec(uint32_t period_ms) {
    timespec spec{};
    spec.tv_sec = static_cast<time_t>(period_ms / 1000U);
    spec.tv_nsec = static_cast<long>((period_ms % 1000U) * 1000000UL);
    return spec;
}

// RAII 文件描述符封装 —— 析构时自动关闭，仅支持移动语义（禁止拷贝）
class ScopedFd {
public:
    // 默认构造，fd_ 初始为 -1
    ScopedFd() = default;
    // 从已有文件描述符构造，取得所有权
    explicit ScopedFd(int fd) : fd_(fd) {}
    // 析构时自动关闭有效 fd
    ~ScopedFd() { Reset(); }

    // 移动构造 —— 转移所有权，源对象 fd_ 置 -1
    ScopedFd(ScopedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

    // 移动赋值 —— 先释放当前 fd，再转移所有权
    ScopedFd& operator=(ScopedFd&& other) noexcept {
        if (this != &other) {
            Reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    // 禁止拷贝构造
    ScopedFd(const ScopedFd&) = delete;
    // 禁止拷贝赋值
    ScopedFd& operator=(const ScopedFd&) = delete;

    // 获取底层文件描述符
    int Get() const { return fd_; }
    // 判断文件描述符是否有效（>= 0）
    bool Valid() const { return fd_ >= 0; }

    // 重置文件描述符 —— 关闭当前 fd 并替换为新 fd
    void Reset(int fd = -1) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_ = -1;       // 底层文件描述符，-1 表示无效
};

// 周期定时器抽象接口 —— 定义 Start/Stop/Running 纯虚方法
class PeriodicTimerBackend {
public:
    virtual ~PeriodicTimerBackend() = default;
    // 启动周期定时器
    // @param period_ms 周期（毫秒）
    // @param callback 到期回调
    // @param on_failure 故障回调
    virtual bool Start(uint32_t period_ms, std::function<void()> callback, std::function<void()> on_failure) = 0;
    // 停止定时器
    virtual void Stop() = 0;
    // 检查定时器是否正在运行
    virtual bool Running() const = 0;
};

// 基于 timerfd + eventfd 的周期定时器实现。
// 工作线程执行 poll() 监听定时器到期和停止信号。
class TimerfdBackend final : public PeriodicTimerBackend {
public:
    ~TimerfdBackend() override { Stop(); }

    // 启动定时器 —— 创建 timerfd/eventfd → 设置周期性 arm → 创建工作线程
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

    // 停止定时器 —— 信号通知工作线程退出并等待 join
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

    // 判断定时器是否在运行
    bool Running() const override { return running_.load(); }

private:
    // arm timerfd 定时器（一次性 + 周期）
    static bool ArmTimerFd(int timer_fd, uint32_t period_ms) {
        itimerspec schedule{};
        schedule.it_value = ToTimespec(period_ms);
        schedule.it_interval = schedule.it_value;
        return timerfd_settime(timer_fd, 0, &schedule, nullptr) == 0;
    }

    // 向 eventfd 写入停止信号
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

    // 排空 eventfd 停止信号（在读端消耗）
    static void DrainStopSignal(int stop_fd) {
        uint64_t signal = 0;
        while (read(stop_fd, &signal, sizeof(signal)) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
    }

    // 工作线程主循环 —— poll timerfd/stop_fd，到期执行回调
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

    std::atomic<bool> running_{false};   // 原子运行标志，线程安全的状态指示
    ScopedFd timer_fd_{};                // 定时器文件描述符（timerfd）
    ScopedFd stop_fd_{};                 // 停止信号文件描述符（eventfd）
    std::thread worker_{};               // 工作线程，执行 poll 等待和回调调用
};

}  // namespace

// TimerBridge PIMPL 实现 —— 持有定时器后端实例（默认 TimerfdBackend）
struct TimerBridge::Impl {
    std::unique_ptr<PeriodicTimerBackend> backend = std::make_unique<TimerfdBackend>();
};

// 构造函数 —— 创建 PIMPL 实现并默认使用 TimerfdBackend
TimerBridge::TimerBridge() : impl_(std::make_unique<Impl>()) {}

// 析构函数 —— 自动停止定时器
TimerBridge::~TimerBridge() {
    Stop();
}

// 启动周期定时器 —— 委托给后端实现
bool TimerBridge::Start(uint32_t period_ms, std::function<void()> callback, std::function<void()> on_failure) {
    return impl_->backend->Start(period_ms, std::move(callback), std::move(on_failure));
}

// 停止定时器 —— 委托给后端实现
void TimerBridge::Stop() {
    impl_->backend->Stop();
}

// 检查定时器是否正在运行 —— 委托给后端实现
bool TimerBridge::Running() const {
    return impl_->backend->Running();
}

}  // namespace ls2k::platform::true_ls2k0300
