/**
 * @file diagnostics.hpp
 * @brief 诊断与日志基础设施
 *
 * 提供等级化诊断事件定义、限速输出的日志抽象层和标准输出的诊断接收器实现。
 * 整个系统中所有模块通过 DiagnosticSink 接口输出诊断信息。
 */

#ifndef LS2K_PORT_DIAGNOSTICS_HPP
#define LS2K_PORT_DIAGNOSTICS_HPP

#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ls2k::port {

/**
 * @enum DiagnosticLevel
 * @brief 诊断事件等级
 *
 * 按严重程度递增排列：Info < Warning < Error < FailSafe。
 */
enum class DiagnosticLevel {
    kInfo,      ///< 一般信息
    kWarning,   ///< 警告（非严重异常）
    kError,     ///< 错误（功能受影响）
    kFailSafe   ///< 紧急安全事件（需立即响应）
};

/**
 * @struct DiagnosticEvent
 * @brief 诊断事件
 *
 * 包含事件等级、代码标识、消息文本和时间戳。
 */
struct DiagnosticEvent {
    DiagnosticLevel level = DiagnosticLevel::kInfo;  ///< 事件等级
    std::string code;       ///< 事件代码（如 "perf.window"、"camera.capture_failed"）
    std::string message;    ///< 事件描述文本
    uint64_t timestamp_ms = 0;  ///< 事件时间戳（毫秒），0表示使用当前时间
};

/**
 * @brief 获取当前单调时钟的时间戳（毫秒）
 * @return 从epoch开始的毫秒数
 */
inline uint64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

/**
 * @class DiagnosticSink
 * @brief 诊断接收器的抽象接口
 *
 * 所有诊断输出的目标需要继承此类并实现 Emit 方法。
 */
class DiagnosticSink {
public:
    virtual ~DiagnosticSink() = default;
    /** @brief 输出一个诊断事件 */
    virtual void Emit(const DiagnosticEvent& event) = 0;
};

/**
 * @class DiagnosticRateLimiter
 * @brief 诊断事件频率限速器
 *
 * 按事件代码（key）跟踪上次输出时间，在指定间隔内抑制重复事件的输出。
 * 线程安全（内部使用互斥锁）。
 */
class DiagnosticRateLimiter final {
public:
    /**
     * @brief 判断是否应该输出该事件
     * @param key 事件唯一键（通常使用事件code）
     * @param now_ms 当前时间戳
     * @param interval_ms 最小输出间隔（毫秒）
     * @return true=应该输出，false=应抑制
     */
    bool ShouldEmit(const std::string& key, uint64_t now_ms, uint64_t interval_ms) {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = last_emit_ms_.find(key);
        if (it != last_emit_ms_.end() && now_ms >= it->second && now_ms - it->second < interval_ms) {
            return false;
        }
        last_emit_ms_[key] = now_ms;
        return true;
    }

private:
    std::mutex mu_{};  ///< 互斥锁
    std::unordered_map<std::string, uint64_t> last_emit_ms_{};  ///< 各事件代码的最后输出时间
};

/**
 * @brief 获取全局诊断限速器实例
 * @return 全局单例的 DiagnosticRateLimiter 引用
 */
inline DiagnosticRateLimiter& GlobalDiagnosticRateLimiter() {
    static DiagnosticRateLimiter limiter;
    return limiter;
}

/**
 * @brief 限速输出诊断事件
 * @param diagnostics 目标诊断接收器
 * @param event 待输出的事件（若timestamp_ms为0则自动填充当前时间）
 * @param interval_ms 限速间隔（毫秒）
 */
inline void EmitRateLimited(DiagnosticSink& diagnostics, DiagnosticEvent event, uint64_t interval_ms) {
    if (event.timestamp_ms == 0) {
        event.timestamp_ms = NowMs();
    }
    if (GlobalDiagnosticRateLimiter().ShouldEmit(event.code, event.timestamp_ms, interval_ms)) {
        diagnostics.Emit(event);
    }
}

/**
 * @class StdoutDiagnostics
 * @brief 标准输出/错误输出的诊断接收器实现
 *
 * 将诊断事件格式化为 "[LEVEL][code][timestamp] message" 的文本行输出。
 * Info/Warning 输出到 stdout，Error/FailSafe 输出到 stderr。
 */
class StdoutDiagnostics final : public DiagnosticSink {
public:
    /** @brief 输出诊断事件到标准输出/错误 */
    void Emit(const DiagnosticEvent& event) override {
        std::lock_guard<std::mutex> lock(mu_);
        std::ostream& stream =
            (event.level == DiagnosticLevel::kError || event.level == DiagnosticLevel::kFailSafe)
                ? std::cerr
                : std::cout;
        stream << "[" << LevelString(event.level) << "]"
               << "[" << event.code << "]"
               << "[" << event.timestamp_ms << "] " << event.message << "\n";
        stream.flush();
    }

    /** @brief 输出 Info 等级诊断信息 */
    void Info(const std::string& code, const std::string& message) {
        Emit({DiagnosticLevel::kInfo, code, message, NowMs()});
    }

    /** @brief 输出 Warning 等级诊断信息 */
    void Warn(const std::string& code, const std::string& message) {
        Emit({DiagnosticLevel::kWarning, code, message, NowMs()});
    }

    /** @brief 输出 Error 等级诊断信息 */
    void Error(const std::string& code, const std::string& message) {
        Emit({DiagnosticLevel::kError, code, message, NowMs()});
    }

    /** @brief 输出 FailSafe 等级诊断信息 */
    void FailSafe(const std::string& code, const std::string& message) {
        Emit({DiagnosticLevel::kFailSafe, code, message, NowMs()});
    }

private:
    /**
     * @brief 诊断等级转字符串
     * @param level 诊断等级
     * @return 对应的字符串表示
     */
    static const char* LevelString(DiagnosticLevel level) {
        switch (level) {
            case DiagnosticLevel::kInfo:
                return "INFO";
            case DiagnosticLevel::kWarning:
                return "WARN";
            case DiagnosticLevel::kError:
                return "ERROR";
            case DiagnosticLevel::kFailSafe:
                return "FAIL_SAFE";
        }
        return "UNKNOWN";
    }

    std::mutex mu_{};  ///< 输出互斥锁
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_DIAGNOSTICS_HPP
