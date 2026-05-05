#include "platform/assistant_protocol.hpp"

// 辅助协议实现 —— 外部助手（如远程控制台）的 JSON 命令解析和状态编码。
// 支持命令：start/stop、tuning mode、turn suppression、target speed override。

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

#include <opencv2/core/persistence.hpp>

namespace ls2k::platform {
namespace {

bool ParseJsonObject(const std::string& text, cv::FileStorage& storage) {
    try {
        if (!storage.open(text,
                          cv::FileStorage::READ | cv::FileStorage::MEMORY |
                              cv::FileStorage::FORMAT_JSON)) {
            return false;
        }
    } catch (...) {
        return false;
    }
    const cv::FileNode root = storage.root();
    return !root.empty() && root.isMap();
}

bool ReadStringValue(const cv::FileNode& node, std::string& value) {
    if (node.empty() || !node.isString()) {
        return false;
    }
    value = static_cast<std::string>(node);
    return true;
}

bool ReadFiniteNumber(const cv::FileNode& node, double& value) {
    if (node.empty() || (!node.isInt() && !node.isReal())) {
        return false;
    }
    value = static_cast<double>(node.real());
    return std::isfinite(value);
}

bool ReadNonNegativeInteger(const cv::FileNode& node, std::uint64_t& value) {
    double numeric = 0.0;
    if (!ReadFiniteNumber(node, numeric) || numeric < 0.0) {
        return false;
    }
    const double rounded = std::round(numeric);
    if (std::fabs(numeric - rounded) > 1e-6) {
        return false;
    }
    if (rounded > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        return false;
    }
    value = static_cast<std::uint64_t>(rounded);
    return true;
}

bool ReadPositiveInt(const cv::FileNode& node, int& value) {
    std::uint64_t parsed = 0;
    if (!ReadNonNegativeInteger(node, parsed) || parsed == 0 ||
        parsed > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool ReadBoolValue(const cv::FileNode& node, bool& value) {
    if (node.empty() || !node.isInt()) {
        return false;
    }
    const int numeric = static_cast<int>(node.real());
    if (numeric == 0 || numeric == 1) {
        value = numeric != 0;
        return true;
    }
    return false;
}

AssistantInboundMessage MakeInputRejected(std::string reason) {
    AssistantInboundMessage message{};
    message.type = AssistantInboundMessageType::kInputRejected;
    message.reason = std::move(reason);
    return message;
}

AssistantInboundMessage MakeAckRejected(std::uint64_t seq, std::string reason) {
    AssistantInboundMessage message{};
    message.type = AssistantInboundMessageType::kAckRejected;
    message.seq = seq;
    message.reason = std::move(reason);
    return message;
}

AssistantInboundMessage MakeCommand(AssistantCommand command) {
    AssistantInboundMessage message{};
    message.type = AssistantInboundMessageType::kCommand;
    message.command = std::move(command);
    return message;
}

void AppendJsonString(std::ostringstream& stream, const std::string& value) {
    stream << '"';
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                stream << "\\\\";
                break;
            case '"':
                stream << "\\\"";
                break;
            case '\n':
                stream << "\\n";
                break;
            case '\r':
                stream << "\\r";
                break;
            case '\t':
                stream << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20U) {
                    stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(static_cast<unsigned char>(ch)) << std::dec
                           << std::setfill(' ');
                } else {
                    stream << ch;
                }
                break;
        }
    }
    stream << '"';
}

void AppendJsonNumber(std::ostringstream& stream, double value) {
    stream << std::setprecision(12) << value;
}

void AppendJsonBool(std::ostringstream& stream, bool value) {
    stream << (value ? "true" : "false");
}

}  // namespace

AssistantInboundMessage DecodeAssistantJsonLine(const std::string& line, double max_target_speed) {
    if (line.empty()) {
        return MakeInputRejected("empty command line");
    }

    cv::FileStorage json;
    if (!ParseJsonObject(line, json)) {
        return MakeInputRejected("malformed json");
    }

    const cv::FileNode root = json.root();
    std::string frame_type;
    if (!ReadStringValue(root["type"], frame_type) || frame_type != "command") {
        return MakeInputRejected("unsupported frame type");
    }

    std::string cmd;
    if (!ReadStringValue(root["cmd"], cmd)) {
        return MakeInputRejected("missing command name");
    }

    std::uint64_t seq = 0;
    if (!ReadNonNegativeInteger(root["seq"], seq)) {
        return MakeInputRejected("invalid or missing seq");
    }

    if (cmd == "start") {
        AssistantCommand command{};
        command.type = AssistantCommandType::kStart;
        command.seq = seq;
        return MakeCommand(command);
    }
    if (cmd == "stop") {
        AssistantCommand command{};
        command.type = AssistantCommandType::kStop;
        command.seq = seq;
        return MakeCommand(command);
    }
    if (cmd == "enable_tuning_mode") {
        AssistantCommand command{};
        command.type = AssistantCommandType::kEnableTuningMode;
        command.seq = seq;
        return MakeCommand(command);
    }
    if (cmd == "disable_tuning_mode") {
        AssistantCommand command{};
        command.type = AssistantCommandType::kDisableTuningMode;
        command.seq = seq;
        return MakeCommand(command);
    }
    if (cmd == "set_turn_suppressed") {
        bool value = false;
        if (!ReadBoolValue(root["value"], value)) {
            return MakeAckRejected(seq, "invalid turn suppression value: expected boolean");
        }
        AssistantCommand command{};
        command.type = AssistantCommandType::kSetTurnSuppressed;
        command.seq = seq;
        command.bool_value = value;
        return MakeCommand(command);
    }
    if (cmd == "set_target_speed") {
        double value = 0.0;
        const cv::FileNode value_node = root["value"];
        if (value_node.empty()) {
            return MakeAckRejected(seq, "invalid target speed: missing value");
        }
        if (!ReadFiniteNumber(value_node, value)) {
            return MakeAckRejected(seq, "invalid target speed: value must be a finite number");
        }
        if (value < 0.0) {
            return MakeAckRejected(seq, "invalid target speed: value must be >= 0");
        }
        if (value > max_target_speed) {
            return MakeAckRejected(seq, "invalid target speed: value exceeds running_speed_target");
        }

        int ttl_ms = 0;
        if (root["ttl_ms"].empty()) {
            return MakeAckRejected(seq, "invalid target speed TTL: missing ttl_ms");
        }
        if (!ReadPositiveInt(root["ttl_ms"], ttl_ms)) {
            return MakeAckRejected(seq,
                                   "invalid target speed TTL: ttl_ms must be a positive integer");
        }

        AssistantCommand command{};
        command.type = AssistantCommandType::kSetTargetSpeed;
        command.seq = seq;
        command.target_speed_value = value;
        command.ttl_ms = ttl_ms;
        return MakeCommand(command);
    }

    return MakeInputRejected("unsupported command");
}

std::string EncodeAssistantAck(std::uint64_t seq, bool accepted, const std::string& reason) {
    std::ostringstream stream;
    stream << "{\"type\":\"ack\",\"seq\":" << seq << ",\"outcome\":\""
           << (accepted ? "accepted" : "rejected") << '"';
    if (!accepted) {
        stream << ",\"reason\":";
        AppendJsonString(stream, reason);
    }
    stream << '}';
    return stream.str();
}

std::string EncodeAssistantState(const std::string& event,
                                 const std::string& reason,
                                 const AssistantStatusView& status) {
    std::ostringstream stream;
    stream << "{\"type\":\"state\",\"event\":";
    AppendJsonString(stream, event);
    stream << ",\"reason\":";
    AppendJsonString(stream, reason);
    stream << ",\"tuning_mode_enabled\":";
    AppendJsonBool(stream, status.tuning_mode_enabled);
    stream << ",\"turn_suppressed\":";
    AppendJsonBool(stream, status.turn_suppressed);
    stream << ",\"target_speed_override_enabled\":";
    AppendJsonBool(stream, status.target_speed_override_enabled);
    stream << ",\"target_speed_override_value\":";
    if (status.target_speed_override_enabled) {
        AppendJsonNumber(stream, status.target_speed_override_value);
    } else {
        stream << "null";
    }
    stream << ",\"effective_speed_target\":";
    AppendJsonNumber(stream, status.effective_speed_target);
    stream << '}';
    return stream.str();
}

std::string EncodeAssistantTelemetry(const AssistantTelemetryView& telemetry) {
    std::ostringstream stream;
    stream << "{\"type\":\"telemetry\",\"motion_phase\":";
    AppendJsonString(stream, telemetry.motion_phase);
    stream << ",\"perception_health\":{\"projector_ok\":";
    AppendJsonBool(stream, telemetry.perception_health.projector_ok);
    stream << ",\"reason\":";
    AppendJsonString(stream, telemetry.perception_health.reason);
    stream << "}";
    stream << ",\"reference\":{\"mode\":";
    AppendJsonString(stream, telemetry.reference.mode);
    stream << ",\"source\":";
    AppendJsonString(stream, telemetry.reference.source);
    stream << "}";
    stream << ",\"eligibility\":{\"usable\":";
    AppendJsonBool(stream, telemetry.eligibility.usable);
    stream << ",\"leading_usable_samples\":" << telemetry.eligibility.leading_usable_samples;
    stream << ",\"leading_min_forward_m\":";
    AppendJsonNumber(stream, telemetry.eligibility.leading_min_forward_m);
    stream << ",\"leading_max_forward_m\":";
    AppendJsonNumber(stream, telemetry.eligibility.leading_max_forward_m);
    stream << ",\"reason\":";
    AppendJsonString(stream, telemetry.eligibility.reason);
    stream << "}";
    stream << ",\"lateral_error\":{\"computed\":";
    AppendJsonBool(stream, telemetry.lateral_error.computed);
    stream << ",\"weighted_lateral_error_m\":";
    AppendJsonNumber(stream, telemetry.lateral_error.weighted_lateral_error_m);
    stream << ",\"weighted_sample_count\":" << telemetry.lateral_error.weighted_sample_count;
    stream << ",\"weight_sum\":";
    AppendJsonNumber(stream, telemetry.lateral_error.weight_sum);
    stream << ",\"reason\":";
    AppendJsonString(stream, telemetry.lateral_error.reason);
    stream << "}";
    stream << ",\"reference_control\":{\"ready\":";
    AppendJsonBool(stream, telemetry.reference_control.ready);
    stream << ",\"reason\":";
    AppendJsonString(stream, telemetry.reference_control.reason);
    stream << "}";
    stream << ",\"safety_gate\":{\"veto_active\":";
    AppendJsonBool(stream, telemetry.safety_gate.veto_active);
    stream << ",\"reason\":";
    AppendJsonString(stream, telemetry.safety_gate.reason);
    stream << "}";
    stream << ",\"degraded\":";
    stream << "{\"active\":";
    AppendJsonBool(stream, telemetry.degraded.active);
    stream << ",\"reason\":";
    AppendJsonString(stream, telemetry.degraded.reason);
    stream << "}";
    stream << ",\"yaw_control\":{\"turn_output_target\":";
    AppendJsonNumber(stream, telemetry.yaw_control.turn_output_target);
    stream << "}";
    stream << ",\"actuator\":{\"raw_turn_output\":" << telemetry.actuator.raw_turn_output;
    stream << ",\"applied_turn_output\":" << telemetry.actuator.applied_turn_output << "}";
    stream << ",\"tuning_mode_enabled\":";
    AppendJsonBool(stream, telemetry.tuning_mode_enabled);
    stream << ",\"turn_suppressed\":";
    AppendJsonBool(stream, telemetry.turn_suppressed);
    stream << ",\"target_speed_override_enabled\":";
    AppendJsonBool(stream, telemetry.target_speed_override_enabled);
    stream << ",\"target_speed_override_value\":";
    if (telemetry.target_speed_override_enabled) {
        AppendJsonNumber(stream, telemetry.target_speed_override_value);
    } else {
        stream << "null";
    }
    stream << ",\"effective_speed_target\":";
    AppendJsonNumber(stream, telemetry.effective_speed_target);
    stream << ",\"left_speed_target\":";
    AppendJsonNumber(stream, telemetry.left_speed_target);
    stream << ",\"right_speed_target\":";
    AppendJsonNumber(stream, telemetry.right_speed_target);
    stream << ",\"left_measured_speed\":";
    AppendJsonNumber(stream, telemetry.left_measured_speed);
    stream << ",\"right_measured_speed\":";
    AppendJsonNumber(stream, telemetry.right_measured_speed);
    stream << ",\"left_pwm_command\":" << telemetry.left_pwm_command;
    stream << ",\"right_pwm_command\":" << telemetry.right_pwm_command;
    stream << '}';
    return stream.str();
}

const char* ToString(AssistantCommandType type) {
    switch (type) {
        case AssistantCommandType::kStart:
            return "start";
        case AssistantCommandType::kStop:
            return "stop";
        case AssistantCommandType::kEnableTuningMode:
            return "enable_tuning_mode";
        case AssistantCommandType::kDisableTuningMode:
            return "disable_tuning_mode";
        case AssistantCommandType::kSetTurnSuppressed:
            return "set_turn_suppressed";
        case AssistantCommandType::kSetTargetSpeed:
            return "set_target_speed";
    }
    return "unknown";
}

}  // namespace ls2k::platform
