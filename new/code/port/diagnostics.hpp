#ifndef LS2K_PORT_DIAGNOSTICS_HPP
#define LS2K_PORT_DIAGNOSTICS_HPP

#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ls2k::port {

enum class DiagnosticLevel {
    kInfo,
    kWarning,
    kError,
    kFailSafe
};

struct DiagnosticEvent {
    DiagnosticLevel level = DiagnosticLevel::kInfo;
    std::string code;
    std::string message;
    uint64_t timestamp_ms = 0;
};

inline uint64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

class DiagnosticSink {
public:
    virtual ~DiagnosticSink() = default;
    virtual void Emit(const DiagnosticEvent& event) = 0;
};

class DiagnosticRateLimiter final {
public:
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
    std::mutex mu_{};
    std::unordered_map<std::string, uint64_t> last_emit_ms_{};
};

inline DiagnosticRateLimiter& GlobalDiagnosticRateLimiter() {
    static DiagnosticRateLimiter limiter;
    return limiter;
}

inline void EmitRateLimited(DiagnosticSink& diagnostics, DiagnosticEvent event, uint64_t interval_ms) {
    if (event.timestamp_ms == 0) {
        event.timestamp_ms = NowMs();
    }
    if (GlobalDiagnosticRateLimiter().ShouldEmit(event.code, event.timestamp_ms, interval_ms)) {
        diagnostics.Emit(event);
    }
}

class StdoutDiagnostics final : public DiagnosticSink {
public:
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

    void Info(const std::string& code, const std::string& message) {
        Emit({DiagnosticLevel::kInfo, code, message, NowMs()});
    }

    void Warn(const std::string& code, const std::string& message) {
        Emit({DiagnosticLevel::kWarning, code, message, NowMs()});
    }

    void Error(const std::string& code, const std::string& message) {
        Emit({DiagnosticLevel::kError, code, message, NowMs()});
    }

    void FailSafe(const std::string& code, const std::string& message) {
        Emit({DiagnosticLevel::kFailSafe, code, message, NowMs()});
    }

private:
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

    std::mutex mu_{};
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_DIAGNOSTICS_HPP
