#ifndef LS2K_LEGACY_WHEEL_TARGET_MIXER_HPP
#define LS2K_LEGACY_WHEEL_TARGET_MIXER_HPP

namespace ls2k::legacy {

/// 车轮速度目标，包含左右轮的速度值
struct WheelSpeedTargets {
    double left = 0.0;   ///< 左轮目标速度
    double right = 0.0;  ///< 右轮目标速度
};

/// 差速轮速目标混合参数
struct WheelTargetMixerParameters {
    double accel_delta_scale = 1.0;  ///< 加速侧 turn delta 缩放系数
    double decel_delta_scale = 1.0;  ///< 减速侧 turn delta 缩放系数
};

/// 车轮目标混合器，将速度目标和转向输出混合为左右轮独立速度目标
class WheelTargetMixer {
public:
    /// 计算左右轮速度目标
    /// 公式：加速侧 = 速度目标 + |转向输出| * accel_delta_scale,
    ///      减速侧 = 速度目标 - |转向输出| * decel_delta_scale
    /// @param effective_speed_target 有效速度目标（被钳制为非负）
    /// @param applied_turn_output 施加的转向输出（正值使左轮增速、右轮减速，向正 lateral_m 侧修正）
    /// @param params 差速 turn delta 缩放参数
    /// @return 左右轮速度目标（均被钳制为非负）
    WheelSpeedTargets Compute(double effective_speed_target,
                              int applied_turn_output,
                              const WheelTargetMixerParameters& params = WheelTargetMixerParameters{}) const;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_WHEEL_TARGET_MIXER_HPP
