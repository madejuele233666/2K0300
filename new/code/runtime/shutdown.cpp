#include "runtime/shutdown.hpp"

namespace ls2k::runtime {

/// 执行运行时关闭：设置停止/退出标志 → 停止定时器 → 禁用执行器 →
/// 清空运行时状态 → 关闭各硬件适配器 → 发布完成诊断
/// @param platform     平台适配器集合（含 timer/actuator/camera/imu/encoder）
/// @param state        运行时状态（将被清理）
/// @param diagnostics  诊断输出接口
void RunShutdown(port::PlatformBundle& platform, RuntimeState& state, port::DiagnosticSink& diagnostics) {
    state.stop_requested.store(true);
    state.exit_requested.store(true);
    if (platform.timer) {
        platform.timer->Stop(diagnostics);
    }
    state.timer_started = false;

    if (platform.actuator) {
        platform.actuator->Disable(diagnostics);
    }
    state.actuators_armed = false;
    state.perception = {};
    state.last_command = {};
    state.control_observation = {};
    state.control_debug_snapshot = {};
    state.perception_memory_reset_generation.fetch_add(1);

    if (platform.camera) {
        platform.camera->Shutdown(diagnostics);
    }
    if (platform.imu) {
        platform.imu->Shutdown(diagnostics);
    }
    if (platform.encoder) {
        platform.encoder->Shutdown(diagnostics);
    }
    if (platform.actuator) {
        platform.actuator->Shutdown(diagnostics);
    }

    diagnostics.Emit({port::DiagnosticLevel::kInfo,
                      "shutdown.complete",
                      "actuators disabled and resources released",
                      port::NowMs()});
}

}  // namespace ls2k::runtime
