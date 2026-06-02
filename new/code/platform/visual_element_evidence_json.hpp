#ifndef LS2K_PLATFORM_VISUAL_ELEMENT_EVIDENCE_JSON_HPP
#define LS2K_PLATFORM_VISUAL_ELEMENT_EVIDENCE_JSON_HPP

#include <cstddef>
#include <iomanip>
#include <ostream>
#include <string>

#include "port/visual_element_evidence_types.hpp"

namespace ls2k::platform {
namespace visual_element_json_detail {

/**
 * 向 JSON 输出流追加转义后的字符串（处理控制字符、引号和反斜杠）。
 * @param stream 输出流
 * @param value 待追加的原始字符串
 */
inline void AppendJsonString(std::ostream& stream, const std::string& value) {
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

/**
 * 向 JSON 输出流追加数值（12 位有效数字精度）。
 * @param stream 输出流
 * @param value 待追加的数值
 */
inline void AppendJsonNumber(std::ostream& stream, double value) {
    stream << std::setprecision(12) << value;
}

/**
 * 向 JSON 输出流追加布尔值（"true" / "false"）。
 * @param stream 输出流
 * @param value 待追加的布尔值
 */
inline void AppendJsonBool(std::ostream& stream, bool value) {
    stream << (value ? "true" : "false");
}

/**
 * 向 JSON 输出流追加视觉元素候选摘要（含构建状态、接管启用标志和仲裁包含状态）。
 * @param stream 输出流
 * @param candidate 视觉元素候选摘要数据
 */
inline void AppendCandidateJson(std::ostream& stream,
                                const port::VisualElementCandidateSummary& candidate) {
    stream << "{\"built\":";
    AppendJsonBool(stream, candidate.built);
    stream << ",\"takeover_enabled\":";
    AppendJsonBool(stream, candidate.takeover_enabled);
    stream << ",\"included_in_arbitration\":";
    AppendJsonBool(stream, candidate.included_in_arbitration);
    stream << ",\"reason\":";
    AppendJsonString(stream, candidate.reason);
    stream << "}";
}

/**
 * 向 JSON 输出流追加路口退出元素证据（含存在标志、置信度、空间范围和候选信息）。
 * @param stream 输出流
 * @param cross_exit 路口退出元素证据数据
 */
inline void AppendCrossExitJson(std::ostream& stream,
                                const port::CrossExitElementEvidence& cross_exit) {
    stream << "{\"present\":";
    AppendJsonBool(stream, cross_exit.present);
    stream << ",\"confidence\":";
    AppendJsonNumber(stream, cross_exit.confidence);
    stream << ",\"forward_min_m\":";
    AppendJsonNumber(stream, cross_exit.forward_min_m);
    stream << ",\"forward_max_m\":";
    AppendJsonNumber(stream, cross_exit.forward_max_m);
    stream << ",\"lateral_min_m\":";
    AppendJsonNumber(stream, cross_exit.lateral_min_m);
    stream << ",\"lateral_max_m\":";
    AppendJsonNumber(stream, cross_exit.lateral_max_m);
    stream << ",\"sampleable_count\":" << cross_exit.sampleable_count;
    stream << ",\"supporting_white_count\":" << cross_exit.supporting_white_count;
    stream << ",\"unknown_count\":" << cross_exit.unknown_count;
    stream << ",\"reason\":";
    AppendJsonString(stream, cross_exit.reason);
    stream << ",\"candidate\":";
    AppendCandidateJson(stream, cross_exit.candidate);
    stream << "}";
}

/**
 * 向 JSON 输出流追加视觉元素证据记录（含 ID、存在标志、置信度、空间边界、支撑统计和候选）。
 * @param stream 输出流
 * @param record 视觉元素证据记录数据
 */
inline void AppendRecordJson(std::ostream& stream,
                             const port::VisualElementEvidenceRecord& record) {
    stream << "{\"id\":";
    AppendJsonString(stream, record.id);
    stream << ",\"present\":";
    AppendJsonBool(stream, record.present);
    stream << ",\"confidence\":";
    AppendJsonNumber(stream, record.confidence);
    stream << ",\"reason\":";
    AppendJsonString(stream, record.reason);
    stream << ",\"bounds\":{\"forward_min_m\":";
    AppendJsonNumber(stream, record.bounds.forward_min_m);
    stream << ",\"forward_max_m\":";
    AppendJsonNumber(stream, record.bounds.forward_max_m);
    stream << ",\"lateral_min_m\":";
    AppendJsonNumber(stream, record.bounds.lateral_min_m);
    stream << ",\"lateral_max_m\":";
    AppendJsonNumber(stream, record.bounds.lateral_max_m);
    stream << "},\"support\":{\"sampleable_count\":" << record.support.sampleable_count;
    stream << ",\"supporting_white_count\":" << record.support.supporting_white_count;
    stream << ",\"supporting_black_count\":" << record.support.supporting_black_count;
    stream << ",\"unknown_count\":" << record.support.unknown_count;
    stream << "},\"candidate\":";
    AppendCandidateJson(stream, record.candidate);
    stream << "}";
}

}  // namespace visual_element_json_detail

/**
 * 向 JSON 输出流追加视觉元素证据帧 —— 包含路口退出证据和所有证据记录。
 * @param stream 输出流
 * @param evidence 视觉元素证据帧数据
 */
inline void AppendVisualElementEvidenceJson(std::ostream& stream,
                                            const port::VisualElementEvidenceFrame& evidence) {
    stream << "{\"cross_exit\":";
    visual_element_json_detail::AppendCrossExitJson(stream, evidence.cross_exit);
    stream << ",\"records\":[";
    for (std::size_t index = 0; index < evidence.records.size(); ++index) {
        if (index > 0U) {
            stream << ",";
        }
        visual_element_json_detail::AppendRecordJson(stream, evidence.records[index]);
    }
    stream << "]}";
}

}  // namespace ls2k::platform

#endif  // LS2K_PLATFORM_VISUAL_ELEMENT_EVIDENCE_JSON_HPP
