/**
 * @file perception_result.hpp
 * @brief 感知结果聚合类型定义
 *
 * 定义运行时转向流水线的完整感知结果快照。
 * 这是一个传输聚合体，只用于输出，各层决策结果通过引用的方式组装于此。
 */

#ifndef LS2K_PORT_PERCEPTION_RESULT_HPP
#define LS2K_PORT_PERCEPTION_RESULT_HPP

#include <cstdint>
#include <string>

#include "port/bev_reference_types.hpp"
#include "port/reference_control_readiness_types.hpp"
#include "port/reference_lateral_error_types.hpp"
#include "port/reference_usability_types.hpp"
#include "port/visual_element_evidence_types.hpp"
#include "port/visual_reference_orchestration_types.hpp"

namespace ls2k::port {

/**
 * @struct PerceptionHealth
 * @brief 感知系统健康状态
 *
 * 跟踪投影器等关键感知组件的健康状态。
 */
struct PerceptionHealth {
    bool projector_ok = false;       ///< 投影器是否正常工作
    std::string reason = "projector_invalid";  ///< 非健康状态的原因描述
};

/**
 * @struct PerceptionResult
 * @brief 感知结果——完整转向流水线的输出快照
 *
 * 包含从原始图像到最终控制就绪状态的完整感知流水线输出。
 * 作为传输聚合体被助理连接和调试记录等上层模块使用。
 */
struct PerceptionResult {
    bool published = false;      ///< 是否已发布
    bool fresh = false;          ///< 是否为最新帧
    uint64_t frame_id = 0;       ///< 帧序号
    uint64_t capture_time_ms = 0;  ///< 图像捕获时间戳
    uint64_t publish_time_ms = 0;  ///< 结果发布时间戳

    int threshold = 0;                       ///< 二值化阈值
    std::string perception_tag = "none";      ///< 感知标记（用于调试）

    std::string reference_source = "none";    ///< 参考路径来源描述
    std::string reference_mode = "none";      ///< 参考路径模式描述
    uint64_t reference_capture_time_ms = 0;   ///< 参考路径对应的图像时间
    BEVReferencePath reference_path{};        ///< selected/held reference path at capture time
    ReferenceTimeAlignmentFacts reference_time_alignment{};  ///< 控制侧时间对齐事实

    PerceptionHealth perception_health{};                         ///< 感知系统健康状态
    VisualElementEvidenceFrame element_evidence{};                 ///< 视觉元素证据帧
    VisualReferenceSelection visual_reference_selection{};         ///< 视觉参考路径选择结果
    ReferenceUsability reference_usability{};                      ///< 参考路径可用性
    ReferenceLateralErrorEstimate reference_lateral_error{};       ///< 参考横向误差估计
    ReferenceControlReadiness reference_control{};                 ///< 参考路径控制就绪状态
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_PERCEPTION_RESULT_HPP
