#include "runtime/control_debug_reporter.hpp"

// 控制调试报告器实现 —— 周期性输出调试快照到诊断系统。
// 支持配置化发射间隔，避免过高频率的诊断输出。

#include <algorithm>
#include <sstream>

namespace ls2k::runtime {
namespace {

// 布尔值转字符串 "true"/"false"
const char* BoolToken(bool value) {
    return value ? "true" : "false";
}

}  // namespace

// 配置调试报告器发射间隔
void ControlDebugReporter::Configure(const port::RuntimeParameters& params) {
    interval_ms_ = std::max(1, params.control_snapshot_emit_interval_ms);
}

// 重置报告器发射时间（强制下次立即发射）
void ControlDebugReporter::Reset() {
    last_emit_ms_ = 0;
}

// 周期性发射调试快照 —— 将控制快照格式化为诊断消息输出
void ControlDebugReporter::MaybeEmit(const ControlDebugSnapshot& snapshot, port::DiagnosticSink& diagnostics) {
    if (!snapshot.valid) {
        return;
    }
    const uint64_t now_ms = snapshot.timestamp_ms == 0 ? port::NowMs() : snapshot.timestamp_ms;
    if (last_emit_ms_ != 0 && now_ms >= last_emit_ms_ &&
        now_ms - last_emit_ms_ < static_cast<uint64_t>(interval_ms_)) {
        return;
    }
    last_emit_ms_ = now_ms;

    std::ostringstream message;
    message << "phase=" << ToString(snapshot.motion_phase)
            << " veto=" << (snapshot.veto_active ? "true" : "false")
            << " reason=" << ToString(snapshot.veto_reason)
            << " tuning_mode=" << (snapshot.tuning_mode_enabled ? "true" : "false")
            << " turn_suppressed=" << (snapshot.turn_suppressed ? "true" : "false")
            << " override_enabled=" << (snapshot.target_speed_override_enabled ? "true" : "false")
            << " override_value="
            << (snapshot.target_speed_override_enabled ? std::to_string(snapshot.target_speed_override_value)
                                                       : std::string("null"))
            << " effective_speed_target=" << snapshot.effective_speed_target
            << " left_target=" << snapshot.left_speed_target
            << " right_target=" << snapshot.right_speed_target
            << " left_measured=" << snapshot.left_measured_speed
            << " right_measured=" << snapshot.right_measured_speed
            << " raw_turn=" << snapshot.raw_turn_output
            << " applied_turn=" << snapshot.applied_turn_output
            << " left_pwm=" << snapshot.left_pwm_command
            << " right_pwm=" << snapshot.right_pwm_command
            << " emergency_stop=" << (snapshot.emergency_stop ? "true" : "false");
    diagnostics.Emit({snapshot.veto_active ? port::DiagnosticLevel::kWarning : port::DiagnosticLevel::kInfo,
                      "control.snapshot",
                      message.str(),
                      now_ms});

    if (!snapshot.steering.valid) {
        return;
    }

    std::ostringstream steering_message;
    steering_message << "phase=" << ToString(snapshot.motion_phase)
                     << " frame_id=" << snapshot.steering.frame_id
                     << " capture_time_ms=" << snapshot.steering.capture_time_ms
                     << " perception_health.projector_ok="
                     << BoolToken(snapshot.steering.perception_health.projector_ok)
                     << " perception_health.reason=" << snapshot.steering.perception_health.reason
                     << " visual_reference.present="
                     << BoolToken(snapshot.steering.visual_reference.present)
                     << " visual_reference.source=" << snapshot.steering.visual_reference.source
                     << " visual_reference.reason=" << snapshot.steering.visual_reference.reason
                     << " visual_reference.candidate_count="
                     << snapshot.steering.visual_reference.candidate_count
                     << " visual_reference.rejected_candidate_reason="
                     << snapshot.steering.visual_reference.rejected_candidate_reason
                     << " reference.mode=" << snapshot.steering.reference.mode
                     << " reference.source=" << snapshot.steering.reference.source
                     << " eligibility.usable=" << BoolToken(snapshot.steering.eligibility.usable)
                     << " eligibility.leading_usable_samples="
                     << snapshot.steering.eligibility.leading_usable_samples
                     << " eligibility.leading_min_forward_m="
                     << snapshot.steering.eligibility.leading_min_forward_m
                     << " eligibility.leading_max_forward_m="
                     << snapshot.steering.eligibility.leading_max_forward_m
                     << " eligibility.reason=" << snapshot.steering.eligibility.reason
                     << " lateral_error.computed=" << BoolToken(snapshot.steering.lateral_error.computed)
                     << " lateral_error.weighted_lateral_error_m="
                     << snapshot.steering.lateral_error.weighted_lateral_error_m
                     << " lateral_error.weighted_sample_count="
                     << snapshot.steering.lateral_error.weighted_sample_count
                     << " lateral_error.weight_sum=" << snapshot.steering.lateral_error.weight_sum
                     << " lateral_error.reason=" << snapshot.steering.lateral_error.reason
                     << " reference_control.ready="
                     << BoolToken(snapshot.steering.reference_control.ready)
                     << " reference_control.reason=" << snapshot.steering.reference_control.reason
                     << " safety_gate.veto_active="
                     << BoolToken(snapshot.steering.safety_gate.veto_active)
                     << " safety_gate.reason=" << snapshot.steering.safety_gate.reason
                     << " degraded.active=" << BoolToken(snapshot.steering.degraded.active)
                     << " degraded.reason=" << snapshot.steering.degraded.reason
                     << " yaw_control.turn_output_target="
                     << snapshot.steering.yaw_control.turn_output_target
                     << " threshold=" << snapshot.steering.threshold
                     << " actuator.raw_turn_output=" << snapshot.steering.actuator.raw_turn_output
                     << " actuator.applied_turn_output=" << snapshot.steering.actuator.applied_turn_output;
    diagnostics.Emit({snapshot.veto_active ? port::DiagnosticLevel::kWarning : port::DiagnosticLevel::kInfo,
                      "control.steering_snapshot",
                      steering_message.str(),
                      now_ms});

    if (!snapshot.steering_internal.valid) {
        return;
    }

    std::ostringstream internal_message;
    internal_message << "authority=internal_debug_only"
                     << " phase=" << ToString(snapshot.motion_phase)
                     << " frame_id=" << snapshot.steering_internal.frame_id
                     << " capture_time_ms=" << snapshot.steering_internal.capture_time_ms
                     << " lateral_error_gain=" << snapshot.steering_internal.lateral_error_gain
                     << " speed_scale=" << snapshot.steering_internal.speed_scale
                     << " turn_output_candidate=" << snapshot.steering_internal.turn_output_candidate
                     << " gyro_z=" << snapshot.steering_internal.gyro_z
                     << " gyro_error=" << snapshot.steering_internal.gyro_error
                     << " gyro_p_term=" << snapshot.steering_internal.gyro_p_term
                     << " gyro_d_term=" << snapshot.steering_internal.gyro_d_term;
    diagnostics.Emit({snapshot.veto_active ? port::DiagnosticLevel::kWarning : port::DiagnosticLevel::kInfo,
                      "control.steering_internal",
                      internal_message.str(),
                      now_ms});
}

}  // namespace ls2k::runtime
