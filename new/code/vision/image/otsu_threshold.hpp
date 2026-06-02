#ifndef LS2K_LEGACY_STEERING_OTSU_THRESHOLD_HPP
#define LS2K_LEGACY_STEERING_OTSU_THRESHOLD_HPP

#include "port/camera_frame_types.hpp"

namespace ls2k::vision {

/// Otsu 阈值计算结果。
///
/// threshold 是二值化切分点；其余字段是同一份直方图在阈值两侧的灰度分布事实，
/// 供后续分类层计算本帧的黑/白侧置信 band。Otsu 层不读取 BEV 参数。
struct OtsuThresholdResult {
    bool valid = false;
    int threshold = 0;
    float black_upper_decile_gray = 0.0F;
    float white_lower_decile_gray = 255.0F;
};

/// 计算Otsu二值化阈值
/// 使用Otsu算法（最大类间方差法）从图像灰度直方图中自动计算最优二值化阈值
/// @param frame 相机帧视图
/// @return 计算得到的Otsu阈值（0-255），若帧无效则返回0
int ComputeOtsuThreshold(const port::LegacyCameraFrameView& frame);

/// 计算 Otsu 阈值和阈值两侧灰度分布事实。
/// @param frame 相机帧视图
/// @return Otsu 阈值结果；若帧无效或直方图不能分成两侧，valid=false
OtsuThresholdResult ComputeOtsuThresholdResult(const port::LegacyCameraFrameView& frame);

}  // namespace ls2k::vision

#endif  // LS2K_LEGACY_STEERING_OTSU_THRESHOLD_HPP
