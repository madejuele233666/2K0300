#include "port/hardware_profile.hpp"

namespace ls2k::port {

/// @brief 将子系统模式枚举值转换为可读字符串
/// @param mode 子系统模式枚举
/// @return 模式名称字符串
const char* ToString(SubsystemMode mode) {
    switch (mode) {
        case SubsystemMode::kDirectMatch:
            return "direct-match";
        case SubsystemMode::kAdaptationHook:
            return "adaptation-hook";
        case SubsystemMode::kDisabled:
            return "disabled";
    }
    return "unknown";
}

}  // namespace ls2k::port
