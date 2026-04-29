#include "runtime/control_loop.hpp"

// 主控制循环实现。
// 负责运动控制的核心循环：构建控制门输入 → 运动监督 → PID 转向 →
// 速度计算 → 执行器输出。管理各阶段状态机（arming/disarmed/active）和紧急停止。

#include <algorithm>
#include <cmath>
#include <string>

namespace ls2k::runtime {
namespace {

// 构建控制门输入 —— 汇总感知、IMU、编码器、低电压、时间戳等状态
ControlGateInputs BuildControlGateInputs(const port::PerceptionResult& perception,
                                         const port::ImuSample& imu,
                                         const port::EncoderDelta& encoder,
                                         bool low_voltage_emergency,
                                         uint64_t now_ms,
                                         const port::RuntimeParameters& params) {
    ControlGateInputs inputs{};
    inputs.perception_published = perception.published;
    inputs.perception_fresh = perception.fresh;
    inputs.perception_capture_time_ms = perception.capture_time_ms;
    inputs.perception_publish_time_ms = perception.publish_time_ms;
    inputs.perception_emergency_veto = perception.emergency_veto;
    inputs.low_voltage_emergency = low_voltage_emergency;
    inputs.imu_valid = imu.valid || perception.imu_grace_active;
    inputs.encoder_valid = encoder.valid;
    inputs.now_ms = now_ms;
    inputs.perception_stale_ms = params.perception_stale_ms;
    return inputs;
}

// 构建运动监督器输入 —— 组合门控决策、运动意图、编码器状态等
MotionSupervisorInputs BuildMotionSupervisorInputs(bool startup_complete,
                                                   const MotionSupervisorState& motion_state,
                                                   const MotionIntent& motion_intent,
                                                   const RuntimeTuningSnapshot& tuning_snapshot,
                                                   const ControlGateDecision& gate,
                                                   const port::EncoderDelta& encoder,
                                                   uint64_t now_ms,
                                                   const port::RuntimeParameters& params) {
    MotionSupervisorInputs inputs{};
    inputs.state = motion_state;
    inputs.intent = motion_intent;
    inputs.startup_complete = startup_complete;
    inputs.gate_clear = !gate.veto_active;
    inputs.now_ms = now_ms;
    inputs.running_speed_target = ResolveRuntimeSpeedTarget(tuning_snapshot, params.Speed_base, now_ms);
    inputs.encoder_mean_abs = (std::abs(encoder.left) + std::abs(encoder.right)) / 2;
    inputs.motion_unveto_confirm_cycles = params.motion_unveto_confirm_cycles;
    inputs.motion_spinup_ms = params.motion_spinup_ms;
    inputs.motion_turn_limit_spinup = params.motion_turn_limit_spinup;
    inputs.motion_pwm_step_limit = params.motion_pwm_step_limit;
    inputs.motion_stop_ms = params.motion_stop_ms;
    inputs.motion_stop_encoder_threshold = params.motion_stop_encoder_threshold;
    inputs.motion_fault_rearm_hold_ms = params.motion_fault_rearm_hold_ms;
    inputs.shaped_command_zero = motion_state.last_shaped_command_zero;
    return inputs;
}

// 输出门控 veto 诊断 —— 仅在有 veto 时限频发射
void EmitVetoDiagnostics(port::DiagnosticSink& diagnostics,
                         const ControlGateDecision& decision,
                         uint64_t now_ms) {
    if (!decision.veto_active) {
        return;
    }

    const std::string reason = ToString(decision.veto_reason);
    port::EmitRateLimited(diagnostics,
                          {port::DiagnosticLevel::kFailSafe,
                           "control.veto",
                           "vetoing actuator update due to " + reason,
                           now_ms},
                          1000);
    port::EmitRateLimited(diagnostics,
                          {port::DiagnosticLevel::kFailSafe,
                           ToDiagnosticCode(decision.veto_reason),
                           "dominant control veto reason: " + reason,
                           now_ms},
                          1000);
}

// 跟踪门控状态区间并报告持续 veto/非 veto 时长
void EmitGateIntervalDiagnostics(port::DiagnosticSink& diagnostics,
                                 bool& have_gate_interval,
                                 bool& last_gate_veto,
                                 ControlVetoReason& last_gate_reason,
                                 uint64_t& gate_interval_start_ms,
                                 bool& gate_interval_reported,
                                 const ControlGateDecision& current,
                                 uint64_t now_ms) {
    if (!have_gate_interval) {
        have_gate_interval = true;
        last_gate_veto = current.veto_active;
        last_gate_reason = current.veto_reason;
        gate_interval_start_ms = now_ms;
        gate_interval_reported = false;
        return;
    }

    const bool state_changed = current.veto_active != last_gate_veto ||
                               (current.veto_active && current.veto_reason != last_gate_reason);
    if (state_changed) {
        const uint64_t duration_ms = now_ms >= gate_interval_start_ms ? now_ms - gate_interval_start_ms : 0;
        const std::string previous_reason = last_gate_veto ? ToString(last_gate_reason) : "none";
        diagnostics.Emit({last_gate_veto ? port::DiagnosticLevel::kInfo : port::DiagnosticLevel::kWarning,
                          last_gate_veto ? "control.veto.interval.closed" : "control.unveto.interval.closed",
                          std::string("previous ") + (last_gate_veto ? "veto" : "non-veto") +
                              " interval closed after " + std::to_string(duration_ms) +
                              " ms (reason=" + previous_reason + ")",
                          now_ms});
        last_gate_veto = current.veto_active;
        last_gate_reason = current.veto_reason;
        gate_interval_start_ms = now_ms;
        gate_interval_reported = false;
    }

    if (!gate_interval_reported) {
        const uint64_t duration_ms = now_ms >= gate_interval_start_ms ? now_ms - gate_interval_start_ms : 0;
        if (duration_ms >= 200) {
            diagnostics.Emit({current.veto_active ? port::DiagnosticLevel::kWarning
                                                  : port::DiagnosticLevel::kInfo,
                              current.veto_active ? "control.veto.sustained" : "control.unveto.sustained",
                              current.veto_active
                                  ? "control remained vetoed for " + std::to_string(duration_ms) +
                                        " ms with dominant reason=" + ToString(current.veto_reason)
                                  : "control remained non-veto for " + std::to_string(duration_ms) +
                                        " ms; actuator path stayed eligible for lifecycle evaluation",
                              now_ms});
            gate_interval_reported = true;
        }
    }
}

// 将转向输出限制在 [-turn_limit, +turn_limit] 范围内
int ClampTurnOutput(float turn_output, double turn_limit_scale, int pwm_limit) {
    const float turn_limit = static_cast<float>(std::max(0.0, turn_limit_scale) * pwm_limit);
    return static_cast<int>(std::round(std::clamp(turn_output, -turn_limit, turn_limit)));
}

// 构建快照轮速目标（停止/禁止行驶时返回零值）
legacy::WheelSpeedTargets BuildSnapshotWheelTargets(const MotionDecision& decision,
                                                    int applied_turn_output,
                                                    const legacy::WheelTargetMixer& mixer,
                                                    int pwm_limit) {
    if (decision.require_emergency_stop || decision.hold_disarmed || !decision.allow_drive) {
        return {};
    }

    const int snapshot_turn_pwm = decision.state.phase == MotionPhase::kStopping ? 0 : applied_turn_output;
    return mixer.Compute(decision.effective_speed_target, snapshot_turn_pwm, pwm_limit);
}

struct ControlDebugSnapshotInputs {
    const port::PerceptionResult& perception;
    const port::EncoderDelta& encoder;
    const port::ActuatorCommand& command;
    const ControlGateDecision& gate;
    const MotionDecision& final_motion;
    const RuntimeTuningSnapshot& tuning_snapshot;
    const legacy::WheelSpeedTargets& snapshot_wheel_targets;
    const legacy::CameraTurnComputation& camera_turn;
    const legacy::GyroTurnComputation& gyro_turn;
    uint64_t now_ms = 0;
    uint64_t cycle_count = 0;
    int raw_turn_output = 0;
    int applied_turn_output = 0;
    bool steering_terms_valid = false;
};

ControlDebugSnapshot BuildControlDebugSnapshot(const ControlDebugSnapshotInputs& inputs) {
    const port::PerceptionResult& perception = inputs.perception;
    ControlDebugSnapshot debug_snapshot{};
    const bool override_active = RuntimeTuningOverrideActiveAt(inputs.tuning_snapshot, inputs.now_ms);
    debug_snapshot.valid = true;
    debug_snapshot.cycle_count = inputs.cycle_count;
    debug_snapshot.timestamp_ms = inputs.now_ms;
    debug_snapshot.motion_phase = inputs.final_motion.state.phase;
    debug_snapshot.veto_active = inputs.gate.veto_active;
    debug_snapshot.veto_reason = inputs.gate.veto_reason;
    debug_snapshot.tuning_mode_enabled = inputs.tuning_snapshot.tuning_mode_enabled;
    debug_snapshot.turn_suppressed =
        inputs.tuning_snapshot.tuning_mode_enabled && inputs.tuning_snapshot.turn_suppressed;
    debug_snapshot.target_speed_override_enabled = override_active;
    debug_snapshot.target_speed_override_value =
        override_active ? inputs.tuning_snapshot.target_speed_override_value : 0.0;
    debug_snapshot.effective_speed_target =
        inputs.final_motion.effective_speed_target *
        std::clamp(perception.control_model.valid ? perception.control_model.speed_limit_scale : 1.0,
                   0.0,
                   1.0);
    debug_snapshot.left_speed_target = inputs.snapshot_wheel_targets.left;
    debug_snapshot.right_speed_target = inputs.snapshot_wheel_targets.right;
    debug_snapshot.left_measured_speed = static_cast<double>(inputs.encoder.left);
    debug_snapshot.right_measured_speed = static_cast<double>(inputs.encoder.right);
    debug_snapshot.raw_turn_output = inputs.raw_turn_output;
    debug_snapshot.applied_turn_output = inputs.applied_turn_output;
    debug_snapshot.turn_pwm_command = inputs.applied_turn_output;
    debug_snapshot.left_pwm_command = inputs.command.left_pwm;
    debug_snapshot.right_pwm_command = inputs.command.right_pwm;
    debug_snapshot.emergency_stop = inputs.command.emergency_stop;
    debug_snapshot.steering.valid = inputs.steering_terms_valid;
    debug_snapshot.steering.frame_id = perception.frame_id;
    debug_snapshot.steering.capture_time_ms = perception.capture_time_ms;
    debug_snapshot.steering.near_lateral_error = perception.near_lateral_error;
    debug_snapshot.steering.far_heading_error = perception.far_heading_error;
    debug_snapshot.steering.preview_curvature = perception.preview_curvature;
    debug_snapshot.steering.raw_near_lateral_error = perception.control_model.raw_near_lateral_error;
    debug_snapshot.steering.raw_far_heading_error = perception.control_model.raw_far_heading_error;
    debug_snapshot.steering.raw_preview_curvature = perception.control_model.raw_preview_curvature;
    debug_snapshot.steering.lookahead_distance_m = perception.control_model.lookahead_distance_m;
    debug_snapshot.steering.lookahead_lateral_error = perception.control_model.lookahead_lateral_error;
    debug_snapshot.steering.lookahead_heading_error = perception.control_model.lookahead_heading_error;
    debug_snapshot.steering.reference_curvature = perception.control_model.reference_curvature;
    debug_snapshot.steering.raw_lookahead_lateral_error =
        perception.control_model.raw_lookahead_lateral_error;
    debug_snapshot.steering.raw_lookahead_heading_error =
        perception.control_model.raw_lookahead_heading_error;
    debug_snapshot.steering.raw_reference_curvature = perception.control_model.raw_reference_curvature;
    debug_snapshot.steering.trusted_error_active = perception.control_model.trusted_error_active;
    debug_snapshot.steering.trusted_error_weight_near =
        perception.control_model.trusted_error_weight_near;
    debug_snapshot.steering.trusted_error_weight_far =
        perception.control_model.trusted_error_weight_far;
    debug_snapshot.steering.trusted_error_weight_lookahead =
        perception.control_model.trusted_error_weight_lookahead;
    debug_snapshot.steering.trusted_error_weight_curvature =
        perception.control_model.trusted_error_weight_curvature;
    debug_snapshot.steering.curvature_command = perception.control_model.curvature_command;
    debug_snapshot.steering.yaw_rate_target = perception.control_model.yaw_rate_target;
    debug_snapshot.steering.visible_range_m = perception.visible_range_m;
    debug_snapshot.steering.scene_width_expand_ratio = perception.scene_observation.width_expand_ratio;
    debug_snapshot.steering.scene_cross_bilateral_open_score_m =
        perception.scene_observation.cross_bilateral_open_score_m;
    debug_snapshot.steering.scene_cross_bilateral_open =
        perception.scene_observation.cross_bilateral_open;
    debug_snapshot.steering.scene_cross_candidate = perception.scene_observation.cross_candidate;
    debug_snapshot.steering.scene_zebra_candidate = perception.scene_observation.zebra_candidate;
    debug_snapshot.steering.scene_circle_left_candidate =
        perception.scene_observation.circle_left_candidate;
    debug_snapshot.steering.scene_circle_right_candidate =
        perception.scene_observation.circle_right_candidate;
    debug_snapshot.steering.scene_left_open_score = perception.scene_observation.left_open_score;
    debug_snapshot.steering.scene_right_open_score = perception.scene_observation.right_open_score;
    debug_snapshot.steering.scene_left_contract_score =
        perception.scene_observation.left_contract_score;
    debug_snapshot.steering.scene_right_contract_score =
        perception.scene_observation.right_contract_score;
    debug_snapshot.steering.scene_left_boundary_heading_abs_rad =
        perception.scene_observation.left_boundary_heading_abs_rad;
    debug_snapshot.steering.scene_right_boundary_heading_abs_rad =
        perception.scene_observation.right_boundary_heading_abs_rad;
    debug_snapshot.steering.scene_circle_left_opposite_straight =
        perception.scene_observation.circle_left_opposite_straight;
    debug_snapshot.steering.scene_circle_right_opposite_straight =
        perception.scene_observation.circle_right_opposite_straight;
    debug_snapshot.steering.lateral_error = perception.lateral_error;
    debug_snapshot.steering.heading_error = perception.heading_error;
    debug_snapshot.steering.curvature = perception.curvature;
    debug_snapshot.steering.track_confidence = perception.track_confidence;
    debug_snapshot.steering.track_valid = perception.track_valid;
    debug_snapshot.steering.sign_flip_blocked = perception.sign_flip_blocked;
    debug_snapshot.steering.imu_grace_active = perception.imu_grace_active;
    debug_snapshot.steering.gyro_heading_delta_deg = perception.gyro_heading_delta_deg;
    debug_snapshot.steering.gyro_consistency_score = perception.gyro_consistency_score;
    debug_snapshot.steering.threshold = perception.threshold;
    debug_snapshot.steering.threshold_veto = perception.threshold_veto;
    debug_snapshot.steering.active_module = perception.active_module;
    debug_snapshot.steering.scene_phase = perception.scene_phase;
    debug_snapshot.steering.scene_override_source = perception.scene_override_source;
    debug_snapshot.steering.reference_mode = perception.reference_mode;
    debug_snapshot.steering.roadblock_interface_state = perception.roadblock_interface_state;
    debug_snapshot.steering.circle_direction = perception.circle_direction;
    debug_snapshot.steering.circle_reference_mode = perception.circle_reference_mode;
    debug_snapshot.steering.circle_heading_delta_deg = perception.circle_heading_delta_deg;
    debug_snapshot.steering.circle_yaw_accum_deg = perception.circle_yaw_accum_deg;
    debug_snapshot.steering.circle_path_phase = perception.circle_path_phase;
    debug_snapshot.steering.reference_compatibility_error_m =
        perception.reference_compatibility_error_m;
    debug_snapshot.steering.reference_source = perception.reference_source;
    debug_snapshot.steering.circle_entry_signal_active = perception.circle_entry_signal_active;
    debug_snapshot.steering.roadblock_active = perception.roadblock_active;
    debug_snapshot.steering.resolved_fuzzy_p = inputs.camera_turn.resolved_fuzzy_p;
    debug_snapshot.steering.camera_p_term = inputs.camera_turn.camera_p_term;
    debug_snapshot.steering.camera_d_term = inputs.camera_turn.camera_d_term;
    debug_snapshot.steering.w_target = inputs.camera_turn.w_target;
    debug_snapshot.steering.gyro_z = inputs.gyro_turn.gyro_z;
    debug_snapshot.steering.gyro_error = inputs.gyro_turn.gyro_error;
    debug_snapshot.steering.gyro_p_term = inputs.gyro_turn.gyro_p_term;
    debug_snapshot.steering.gyro_d_term = inputs.gyro_turn.gyro_d_term;
    debug_snapshot.steering.raw_turn_output = inputs.raw_turn_output;
    debug_snapshot.steering.applied_turn_output = inputs.applied_turn_output;
    return debug_snapshot;
}

// 限制每周期 PWM 变化量（平滑控制输出，防止跳变）
port::ActuatorCommand ApplyPwmStepLimit(const port::ActuatorCommand& previous,
                                        port::ActuatorCommand command,
                                        int pwm_step_limit) {
    if (command.emergency_stop || pwm_step_limit <= 0) {
        return command;
    }
    command.left_pwm =
        std::clamp(command.left_pwm, previous.left_pwm - pwm_step_limit, previous.left_pwm + pwm_step_limit);
    command.right_pwm =
        std::clamp(command.right_pwm, previous.right_pwm - pwm_step_limit, previous.right_pwm + pwm_step_limit);
    return command;
}

// 单通道 PWM 地板值限制 —— 确保非零 PWM 不小于 pwm_floor
int ApplySinglePwmFloor(int pwm, int pwm_limit, int pwm_floor) {
    if (pwm == 0 || pwm_floor <= 0) {
        return pwm;
    }
    const int clamped_floor = std::min(std::max(0, pwm_floor), std::max(0, pwm_limit));
    if (clamped_floor == 0) {
        return pwm;
    }
    if (pwm > 0) {
        return std::clamp(std::max(pwm, clamped_floor), 0, pwm_limit);
    }
    return std::clamp(std::min(pwm, -clamped_floor), -pwm_limit, 0);
}

// 对左右通道分别施加 PWM 地板值限制
port::ActuatorCommand ApplyPwmFloor(port::ActuatorCommand command, int pwm_limit, int pwm_floor) {
    if (command.emergency_stop) {
        return command;
    }
    command.left_pwm = ApplySinglePwmFloor(command.left_pwm, pwm_limit, pwm_floor);
    command.right_pwm = ApplySinglePwmFloor(command.right_pwm, pwm_limit, pwm_floor);
    return command;
}

// 单通道禁止反转保护 —— 防止正向行驶中 PWM 意外跳负
int ApplySingleProhibitReverse(int previous_pwm, int requested_pwm, double target_speed, int pwm_step_limit) {
    if (requested_pwm >= 0) {
        return requested_pwm;
    }
    if (target_speed <= 0.0) {
        return 0;
    }
    if (pwm_step_limit <= 0) {
        return 0;
    }
    return std::max(0, previous_pwm - pwm_step_limit);
}

// 对左右通道施加禁止反转保护（仅在 prohibit_reverse_pwm 启用时）
port::ActuatorCommand ApplyProhibitReverse(const port::ActuatorCommand& previous_command,
                                           port::ActuatorCommand command,
                                           const legacy::WheelSpeedTargets& wheel_targets,
                                           bool prohibit_reverse_pwm,
                                           int pwm_step_limit) {
    if (command.emergency_stop || !prohibit_reverse_pwm) {
        return command;
    }
    command.left_pwm =
        ApplySingleProhibitReverse(previous_command.left_pwm, command.left_pwm, wheel_targets.left, pwm_step_limit);
    command.right_pwm =
        ApplySingleProhibitReverse(previous_command.right_pwm, command.right_pwm, wheel_targets.right, pwm_step_limit);
    return command;
}

// 全局重置所有控制器和运行时转向状态
void ResetControllerState(legacy::LegacyPidControl& pid,
                          legacy::LegacyAttitudeLogic& attitude,
                          legacy::WheelPidController& left_wheel_pid,
                          legacy::WheelPidController& right_wheel_pid,
                          RuntimeState& state) {
    pid.Reset();
    attitude.Reset();
    left_wheel_pid.Reset();
    right_wheel_pid.Reset();
    ResetSteeringRuntimeState(state.steering_state);
}

// 发射运动阶段诊断 —— 阶段转换、启动阻塞、故障复位准备状态
void EmitMotionDiagnostics(port::DiagnosticSink& diagnostics,
                           const MotionDecision& decision,
                           const ControlGateDecision& gate,
                           bool& motion_reset_ready_reported,
                           uint64_t now_ms) {
    if (decision.phase_changed) {
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "motion.phase.transition",
                          std::string("motion phase transitioned from ") + ToString(decision.previous_phase) +
                              " to " + ToString(decision.state.phase),
                          now_ms});
        switch (decision.state.phase) {
            case MotionPhase::kSpinup:
                diagnostics.Emit({port::DiagnosticLevel::kInfo,
                                  "motion.spinup.enter",
                                  "motion start request cleared the gate-confirmation boundary and entered spinup",
                                  now_ms});
                break;
            case MotionPhase::kRunning:
                diagnostics.Emit({port::DiagnosticLevel::kInfo,
                                  "motion.spinup.complete",
                                  "spinup timer completed and running authority is now released",
                                  now_ms});
                break;
            case MotionPhase::kDisarmed:
                if (decision.previous_phase == MotionPhase::kStopping) {
                    diagnostics.Emit({port::DiagnosticLevel::kInfo,
                                      "motion.stop.complete",
                                      "controlled stop completed and runtime returned to DISARMED",
                                      now_ms});
                } else if (decision.previous_phase == MotionPhase::kFailSafeLatched) {
                    diagnostics.Emit({port::DiagnosticLevel::kInfo,
                                      "motion.failsafe.rearmed",
                                      "fail-safe latch cleared after explicit reset intent and hold time",
                                      now_ms});
                }
                break;
            case MotionPhase::kFailSafeLatched:
                diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                                  "motion.failsafe.latched",
                                  "runtime fault interrupted motion and entered FAIL_SAFE_LATCHED (reason=" +
                                      std::string(ToString(gate.veto_reason)) + ")",
                                  now_ms});
                break;
            case MotionPhase::kStartRequested:
            case MotionPhase::kStopping:
                break;
        }
    }

    if (decision.blocked_start) {
        port::EmitRateLimited(diagnostics,
                              {port::DiagnosticLevel::kWarning,
                               "motion.start.blocked",
                               std::string("start request remains blocked while gate reason=") +
                                   ToString(gate.veto_reason),
                               now_ms},
                              1000);
    }

    if (decision.reset_ready && !motion_reset_ready_reported) {
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "motion.failsafe.reset_ready",
                          "fail-safe latch hold completed; explicit reset intent may now re-arm the runtime",
                          now_ms});
        motion_reset_ready_reported = true;
    } else if (!decision.reset_ready) {
        motion_reset_ready_reported = false;
    }
}

// 发射控制周期观测诊断 —— 输出应用结果、arming 转换等信息
void EmitObservationDiagnostics(port::DiagnosticSink& diagnostics,
                                const ControlCycleObservation& previous,
                                const ControlCycleObservation& current,
                                uint64_t now_ms) {
    if (current.requested_nonzero_output) {
        port::EmitRateLimited(diagnostics,
                              {port::DiagnosticLevel::kInfo,
                               "control.command.requested_nonzero",
                               "non-zero differential-drive command requested",
                               now_ms},
                              1000);
    }

    switch (current.apply_outcome) {
        case ControlApplyOutcome::kSuppressedByProfile:
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kWarning,
                                   "control.apply.suppressed",
                                   "drive command suppressed because the active motor profile is diagnostics-only",
                                   now_ms},
                                  1000);
            break;
        case ControlApplyOutcome::kHeldDisarmedApplied:
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kInfo,
                                   "control.apply.hold_disarmed",
                                   std::string("gate is clear but runtime is intentionally held disarmed in phase ") +
                                       ToString(current.motion_phase),
                                   now_ms},
                                  1000);
            break;
        case ControlApplyOutcome::kDriveCommandApplied:
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kInfo,
                                   "control.apply.drive",
                                   "drive command applied to motor adapter",
                                   now_ms},
                                  1000);
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kInfo,
                                   "control.apply.command",
                                   "logical drive command left_pwm=" + std::to_string(current.applied_left_pwm) +
                                       " right_pwm=" + std::to_string(current.applied_right_pwm),
                                   now_ms},
                                  1000);
            break;
        case ControlApplyOutcome::kEmergencyStopApplied:
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kFailSafe,
                                   "control.apply.emergency_stop",
                                   "emergency-stop command applied to keep actuators fail-safe",
                                   now_ms},
                                  1000);
            break;
        case ControlApplyOutcome::kApplyFailed:
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kFailSafe,
                                   "control.apply.failed",
                                   "motor adapter rejected the current control command",
                                   now_ms},
                                  1000);
            break;
        case ControlApplyOutcome::kZeroCommandApplied:
        case ControlApplyOutcome::kNotRequested:
            break;
    }

    if (current.arming_transition) {
        const std::string message =
            std::string("actuator armed state transitioned from ") +
            (previous.actuators_armed ? "armed" : "disarmed") + " to " +
            (current.actuators_armed ? "armed" : "disarmed") +
            " (apply_outcome=" + ToString(current.apply_outcome) +
            ", phase=" + ToString(current.motion_phase) + ")";
        diagnostics.Emit({current.actuators_armed ? port::DiagnosticLevel::kInfo
                                                  : port::DiagnosticLevel::kFailSafe,
                          "control.arm.transition",
                          message,
                          now_ms});
    }
}

}  // namespace

ControlLoop::ControlLoop(port::PlatformBundle& platform,
                         const port::HardwareProfile& profile,
                         RuntimeState& state,
                         port::DiagnosticSink& diagnostics)
    : platform_(platform), profile_(profile), state_(state), diagnostics_(diagnostics) {}

// 启动控制循环：校验状态 → 配置 PID/混合器 → 注册定时器 → 初始化运动状态
bool ControlLoop::Start(const port::RuntimeParameters& params) {
    if (running_) {
        return true;
    }
    if (!state_.startup_complete) {
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "control.startup.incomplete",
                           "control loop refused to start before startup completion",
                           port::NowMs()});
        return false;
    }
    if (!platform_.motor) {
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "control.motor.not_ready",
                           "control loop refused to start because motor adapter is missing",
                           port::NowMs()});
        return false;
    }

    const bool degraded_motor_disabled = state_.degraded_startup &&
                                         profile_.motor.mode == port::SubsystemMode::kDisabled;
    if (!platform_.motor->Ready() && !degraded_motor_disabled) {
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "control.motor.not_ready",
                           "control loop refused to arm because motor adapter is not ready",
                           port::NowMs()});
        return false;
    }
    if (degraded_motor_disabled) {
        diagnostics_.Emit({port::DiagnosticLevel::kWarning,
                           "control.start.degraded.motor_disabled",
                           "degraded startup keeps control loop running with motor disabled; actuators stay disarmed",
                           port::NowMs()});
    }

    params_ = params;
    pid_.Configure(params_);
    pid_.Reset();
    attitude_.Reset();
    wheel_target_mixer_.Configure(params_);
    left_wheel_pid_.Configure(params_.left_wheel_pid);
    right_wheel_pid_.Configure(params_.right_wheel_pid);
    left_wheel_pid_.Reset();
    right_wheel_pid_.Reset();
    debug_reporter_.Configure(params_);
    debug_reporter_.Reset();

    state_.timer_started = false;
    state_.actuators_armed = false;
    state_.control_observation = {};
    state_.control_debug_snapshot = {};
    ResetSteeringRuntimeState(state_.steering_state);
    state_.motion_state.phase = MotionPhase::kDisarmed;
    state_.motion_state.phase_entry_ms = port::NowMs();
    state_.motion_state.fail_safe_latched_at_ms = 0;
    state_.motion_state.clean_gate_cycles = 0;

    motion_reset_ready_reported_ = false;
    running_ = true;
    const bool timer_ok = platform_.timer->Start(
        profile_.timer,
        static_cast<uint32_t>(std::max(1, params_.control_period_ms)),
        [this]() { Tick(); },
        [this]() { HandleTimerFailure(); },
        diagnostics_);
    if (!timer_ok) {
        running_ = false;
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "control.timer.start",
                           "control timer failed to start; actuators remain disarmed",
                           port::NowMs()});
        return false;
    }

    state_.timer_started = true;
    const bool low_voltage_block = state_.low_voltage_emergency.load();
    const bool motor_hook_block = profile_.motor.mode == port::SubsystemMode::kAdaptationHook;
    const bool motor_disabled_block = profile_.motor.mode == port::SubsystemMode::kDisabled;
    if (low_voltage_block) {
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "control.arm.low_voltage",
                           "low-voltage emergency active; actuators stay disarmed until cleared",
                           port::NowMs()});
    }
    if (motor_hook_block) {
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "control.arm.motor_hook",
                           "motor adaptation hook has no phase-1 implementation; actuators stay disarmed",
                           port::NowMs()});
    }
    if (motor_disabled_block) {
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "control.arm.motor_disabled",
                           "motor profile is disabled; control loop stays diagnostics-only with actuators disarmed",
                           port::NowMs()});
    }
    diagnostics_.Emit({port::DiagnosticLevel::kInfo,
                       "control.start",
                       "control loop started with gate -> motion supervisor -> shaping/apply sequencing",
                       port::NowMs()});
    return true;
}

// 停止控制循环：停止定时器 → 禁用电机 → 复位运行时状态
void ControlLoop::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    platform_.timer->Stop(diagnostics_);
    if (platform_.motor) {
        platform_.motor->Disable(diagnostics_);
    }
    ResetDisarmedControlState();
}

// 处理定时器故障 —— 进入 FAIL_SAFE_LATCHED 并请求退出
void ControlLoop::HandleTimerFailure() {
    if (!running_.exchange(false)) {
        return;
    }

    const uint64_t now_ms = port::NowMs();
    diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                       "control.timer.runtime_failure",
                       "control timer stopped unexpectedly; runtime entered FAIL_SAFE_LATCHED and will shut down",
                       now_ms});
    if (platform_.motor) {
        platform_.motor->Disable(diagnostics_);
    }
    LatchTimerFailureState(now_ms);
    state_.exit_requested.store(true);
    state_.stop_requested.store(true);
}

// 复位到 DISARMED 状态 —— 清理所有运行时状态和指令
void ControlLoop::ResetDisarmedControlState() {
    state_.timer_started = false;
    std::lock_guard<std::mutex> lock(state_.shared_mutex);
    state_.actuators_armed = false;
    state_.last_command = {};
    state_.control_observation = {};
    state_.control_debug_snapshot = {};
    ResetSteeringRuntimeState(state_.steering_state);
    state_.motion_state.phase = MotionPhase::kDisarmed;
    state_.motion_state.phase_entry_ms = port::NowMs();
    state_.motion_state.fail_safe_latched_at_ms = 0;
    state_.motion_state.clean_gate_cycles = 0;
}

// 锁存定时器故障状态 —— 设置 FAIL_SAFE_LATCHED 并清零所有输出
void ControlLoop::LatchTimerFailureState(uint64_t now_ms) {
    state_.timer_started = false;
    motion_reset_ready_reported_ = false;
    std::lock_guard<std::mutex> lock(state_.shared_mutex);
    state_.actuators_armed = false;
    state_.last_command = {};
    state_.control_observation = {};
    state_.control_observation.motion_phase = MotionPhase::kFailSafeLatched;
    state_.control_observation.hold_disarmed = true;
    state_.control_observation.motion_reset_ready = false;
    state_.control_debug_snapshot = {};
    ResetSteeringRuntimeState(state_.steering_state);
    state_.control_debug_snapshot.valid = true;
    state_.control_debug_snapshot.timestamp_ms = now_ms;
    state_.control_debug_snapshot.motion_phase = MotionPhase::kFailSafeLatched;
    state_.control_debug_snapshot.emergency_stop = true;
    state_.motion_state.phase = MotionPhase::kFailSafeLatched;
    state_.motion_state.phase_entry_ms = now_ms;
    state_.motion_state.fail_safe_latched_at_ms = now_ms;
    state_.motion_state.clean_gate_cycles = 0;
    state_.motion_intent.start_requested = false;
    state_.motion_intent.stop_requested = false;
    state_.motion_intent.reset_fault_requested = false;
}

// 控制定时器心跳 —— 主控制循环单次迭代：
// 采样（低电压/IMU/编码器/感知）→ 门控评估 → 运动监督 →
// 转向计算（PID + 陀螺仪）→ 执行器命令组合 → 诊断输出
void ControlLoop::Tick() {
    if (!running_) {
        return;
    }

    // --- 第 1 阶段：传感器采样 ---
    const port::LowVoltageSample low_voltage = platform_.power->SampleLowVoltage(diagnostics_);
    const bool low_voltage_emergency = !low_voltage.valid || low_voltage.emergency;
    const bool previous_low_voltage = state_.low_voltage_emergency.load();
    if (low_voltage_emergency != previous_low_voltage) {
        diagnostics_.Emit({low_voltage_emergency ? port::DiagnosticLevel::kFailSafe
                                                 : port::DiagnosticLevel::kInfo,
                           "power.low_voltage.transition",
                           low_voltage_emergency ? "runtime low-voltage state transitioned to emergency"
                                                 : "runtime low-voltage state cleared",
                           port::NowMs()});
    }
    state_.low_voltage_emergency.store(low_voltage_emergency);

    const port::ImuSample imu = platform_.imu->Read(diagnostics_);
    const port::EncoderDelta encoder = platform_.encoder->ReadDelta(diagnostics_);

    port::PerceptionResult perception{};
    port::ActuatorCommand previous_command{};
    MotionSupervisorState previous_motion_state{};
    MotionIntent motion_intent{};
    RuntimeTuningSnapshot tuning_snapshot{};
    ControlCycleObservation previous_observation{};
    {
        std::lock_guard<std::mutex> lock(state_.shared_mutex);
        state_.imu = imu;
        state_.encoder = encoder;
        perception = state_.perception;
        previous_command = state_.last_command;
        previous_motion_state = state_.motion_state;
        motion_intent = state_.motion_intent;
        tuning_snapshot = SnapshotRuntimeTuningState(state_.tuning_state);
        previous_observation = state_.control_observation;
    }

    const uint64_t now_ms = port::NowMs();
    // --- 第 2 阶段：门控评估 ---
    const ControlGateDecision gate =
        EvaluateControlGate(BuildControlGateInputs(perception, imu, encoder, low_voltage_emergency, now_ms, params_));
    EmitVetoDiagnostics(diagnostics_, gate, now_ms);
    EmitGateIntervalDiagnostics(diagnostics_,
                                have_gate_interval_,
                                last_gate_veto_,
                                last_gate_reason_,
                                gate_interval_start_ms_,
                                gate_interval_reported_,
                                gate,
                                now_ms);

    // --- 第 3 阶段：运动监督 ---
    const MotionDecision motion = motion_supervisor_.Evaluate(BuildMotionSupervisorInputs(
        state_.startup_complete, previous_motion_state, motion_intent, tuning_snapshot, gate, encoder, now_ms, params_));

    if (motion.reset_controllers) {
        ResetControllerState(pid_, attitude_, left_wheel_pid_, right_wheel_pid_, state_);
    }

    port::ActuatorCommand command{};
    legacy::WheelSpeedTargets wheel_targets{};
    bool hold_disarmed = false;
    int raw_turn_output = 0;
    int applied_turn_output = 0;
    legacy::CameraTurnComputation camera_turn{};
    legacy::GyroTurnComputation gyro_turn{};
    bool steering_terms_valid = perception.published && perception.fresh &&
                               perception.frame_id != 0 && perception.capture_time_ms != 0;
    // --- 第 4 阶段：转向计算与执行器命令 ---
    if (gate.veto_active || motion.require_emergency_stop) {
        command = {};
    } else if (motion.hold_disarmed || !motion.allow_drive) {
        hold_disarmed = true;
        command = {0, 0, false};
    } else if (motion.state.phase == MotionPhase::kStopping) {
        // STOPPING owns the actuator ramp-down directly so real motor output converges
        // to zero even if the legacy speed controller still carries residual integral state.
        command = ApplyPwmStepLimit(previous_command, {0, 0, false}, motion.pwm_step_limit);
    } else {
        attitude_.UpdateFromImu(imu, static_cast<float>(params_.control_period_ms) / 1000.0F);
        const double constrained_speed_target =
            motion.effective_speed_target *
            std::clamp(perception.control_model.valid ? perception.control_model.speed_limit_scale : 1.0,
                       0.0,
                       1.0);
        camera_turn =
            pid_.ComputeTurnTarget(perception, constrained_speed_target, state_.steering_state.controller_memory);
        gyro_turn = pid_.ComputeGyroTurn(camera_turn.w_target,
                                        imu.gyro_z,
                                        imu.valid,
                                        state_.steering_state.controller_memory);
        raw_turn_output = ClampTurnOutput(
            gyro_turn.raw_turn_output,
            motion.turn_limit_scale *
                (perception.control_model.valid ? perception.control_model.turn_limit_scale : 1.0),
            params_.raw_turn_output_limit);
        applied_turn_output = raw_turn_output;
        if (tuning_snapshot.tuning_mode_enabled && tuning_snapshot.turn_suppressed) {
            applied_turn_output = 0;
        }
        wheel_targets =
            wheel_target_mixer_.Compute(constrained_speed_target, applied_turn_output, params_.pwm_limit);
        const int left_pwm = left_wheel_pid_.Compute(wheel_targets.left, encoder.left, params_.pwm_limit);
        const int right_pwm = right_wheel_pid_.Compute(wheel_targets.right, encoder.right, params_.pwm_limit);
        command = motor_logic_.Compose(left_pwm, right_pwm, false, params_.pwm_limit);
        if (motion.state.phase == MotionPhase::kSpinup || motion.state.phase == MotionPhase::kStopping) {
            command = ApplyPwmStepLimit(previous_command, command, motion.pwm_step_limit);
        }
        command = ApplyPwmFloor(command, params_.pwm_limit, params_.pwm_floor);
        command = ApplyProhibitReverse(
            previous_command,
            command,
            wheel_targets,
            params_.prohibit_reverse_pwm,
            params_.prohibit_reverse_pwm_step_limit);
    }
    // --- 第 5 阶段：执行器施加 + 诊断输出 ---
    const bool diagnostics_only_motor =
        profile_.motor.mode == port::SubsystemMode::kAdaptationHook ||
        profile_.motor.mode == port::SubsystemMode::kDisabled;
    const bool current_requested_command_zero =
        !command.emergency_stop && command.left_pwm == 0 && command.right_pwm == 0;
    const bool current_effective_command_zero =
        diagnostics_only_motor || hold_disarmed ||
        (!command.emergency_stop && command.left_pwm == 0 && command.right_pwm == 0);

    MotionDecision final_motion = motion;
    if (motion.state.phase == MotionPhase::kStopping) {
        MotionSupervisorInputs stop_completion_inputs = BuildMotionSupervisorInputs(
            state_.startup_complete, motion.state, motion_intent, tuning_snapshot, gate, encoder, now_ms, params_);
        // Lifecycle stop completion is keyed to the command that can still reach the actuator path,
        // not the controller's diagnostic-only requested PWM.
        stop_completion_inputs.shaped_command_zero = current_effective_command_zero;
        final_motion = motion_supervisor_.Evaluate(stop_completion_inputs);
        if (final_motion.reset_controllers && !motion.reset_controllers) {
            ResetControllerState(pid_, attitude_, left_wheel_pid_, right_wheel_pid_, state_);
        }
    }
    EmitMotionDiagnostics(diagnostics_, final_motion, gate, motion_reset_ready_reported_, now_ms);
    const legacy::WheelSpeedTargets snapshot_wheel_targets =
        BuildSnapshotWheelTargets(final_motion, applied_turn_output, wheel_target_mixer_, params_.pwm_limit);

    bool apply_ok = true;
    if (diagnostics_only_motor || hold_disarmed) {
        platform_.motor->Disable(diagnostics_);
    } else {
        apply_ok = platform_.motor->Apply(command, diagnostics_);
    }

    ControlCycleObservation observation = ObserveControlCycle(
        {gate, command, final_motion.state.phase, apply_ok, diagnostics_only_motor, hold_disarmed, previous_observation.actuators_armed});
    observation.motion_reset_ready = final_motion.reset_ready;
    EmitObservationDiagnostics(diagnostics_, previous_observation, observation, now_ms);

    const ControlDebugSnapshot debug_snapshot =
        BuildControlDebugSnapshot({perception,
                                   encoder,
                                   command,
                                   gate,
                                   final_motion,
                                   tuning_snapshot,
                                   snapshot_wheel_targets,
                                   camera_turn,
                                   gyro_turn,
                                   now_ms,
                                   state_.control_cycle_count.load() + 1,
                                   raw_turn_output,
                                   applied_turn_output,
                                   steering_terms_valid});
    debug_reporter_.MaybeEmit(debug_snapshot, diagnostics_);

    {
        std::lock_guard<std::mutex> lock(state_.shared_mutex);
        state_.motion_state = final_motion.state;
        state_.motion_state.last_shaped_command_zero = current_requested_command_zero;
        if (final_motion.consume_reset_request) {
            state_.motion_intent.reset_fault_requested = false;
        }
        state_.last_command = (apply_ok && !diagnostics_only_motor && !hold_disarmed && !command.emergency_stop)
                                  ? command
                                  : port::ActuatorCommand{};
        state_.control_observation = observation;
        state_.control_debug_snapshot = debug_snapshot;
        state_.actuators_armed = observation.actuators_armed;
        if (observation.apply_outcome == ControlApplyOutcome::kDriveCommandApplied) {
            state_.steering_state.drive_cycle_count = std::min(state_.steering_state.drive_cycle_count + 1, 200);
        }
    }
    ++state_.control_cycle_count;
}

}  // namespace ls2k::runtime
