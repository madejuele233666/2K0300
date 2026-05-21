#ifndef LS2K_LEGACY_STEERING_OTSU_THRESHOLD_HPP
#define LS2K_LEGACY_STEERING_OTSU_THRESHOLD_HPP

#include "port/camera_frame_types.hpp"

namespace ls2k::legacy {

/// 计算Otsu二值化阈值
/// 使用Otsu算法（最大类间方差法）从图像灰度直方图中自动计算最优二值化阈值
/// @param frame 相机帧视图
/// @return 计算得到的Otsu阈值（0-255），若帧无效则返回0
int ComputeOtsuThreshold(const port::LegacyCameraFrameView& frame);

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_OTSU_THRESHOLD_HPP
