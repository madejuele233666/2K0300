/**
 * @file reference_tracking_geometry_types.hpp
 * @brief 参考路径跟踪几何事实类型定义
 */

#ifndef LS2K_PORT_REFERENCE_TRACKING_GEOMETRY_TYPES_HPP
#define LS2K_PORT_REFERENCE_TRACKING_GEOMETRY_TYPES_HPP

#include <cstddef>
#include <string>

namespace ls2k::port {

struct ReferenceTrackingGeometry {
    bool computed = false;
    float lateral_offset_m = 0.0F;
    float heading_error_rad = 0.0F;
    float curvature_m_inv = 0.0F;
    std::size_t sample_count = 0;
    std::string reason = "reference_unusable";
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_REFERENCE_TRACKING_GEOMETRY_TYPES_HPP
