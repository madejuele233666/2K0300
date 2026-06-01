#ifndef LS2K_LEGACY_STEERING_BEV_PIXEL_CLASSIFIER_HPP
#define LS2K_LEGACY_STEERING_BEV_PIXEL_CLASSIFIER_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "legacy/steering_otsu_threshold.hpp"
#include "port/bev_geometry_types.hpp"

namespace ls2k::legacy {

/// 简单BEV像素分类枚举（二值化后的分类结果）
enum class BEVSimplePixelClass {
    kInvalid,   ///< 无效
    kUnknown,   ///< 不确定（在决策带内）
    kBlack,     ///< 黑色（路面）
    kWhite,     ///< 白色（车道线等标记）
};

/// 当前帧灰度分类模型。
///
/// threshold 来自 Otsu；black/white decision band 来自 Otsu 阈值两侧的稳健分位点。
/// 该模型只描述灰度分类，不知道 BEV row、边界、参考线或控制语义。
struct BEVPixelClassificationModel {
    bool valid = false;
    int threshold = 0;
    float black_decision_band = 32.0F;
    float white_decision_band = 32.0F;
};

inline bool ValidGrayThreshold(int threshold) {
    return threshold >= 0 && threshold <= 255;
}

inline bool ValidDecisionBand(float band) {
    return std::isfinite(band) && band > 0.0F;
}

inline bool IsValidBEVPixelClassificationModel(
    const BEVPixelClassificationModel& model) {
    return model.valid &&
           ValidGrayThreshold(model.threshold) &&
           ValidDecisionBand(model.black_decision_band) &&
           ValidDecisionBand(model.white_decision_band);
}

inline float DecisionConfidence(float margin, float band) {
    return std::clamp(margin / band, 0.0F, 1.0F);
}

inline BEVPixelClassificationModel MakeBEVPixelClassificationModel(
    const OtsuThresholdResult& otsu,
    const port::BEVClassificationParameters& classification) {
    BEVPixelClassificationModel model{};
    model.threshold = otsu.threshold;
    if (!otsu.valid || !ValidGrayThreshold(model.threshold)) {
        return model;
    }

    const float threshold_f = static_cast<float>(model.threshold);
    model.black_decision_band =
        (threshold_f - otsu.black_upper_decile_gray) /
        classification.unknown_confidence_min;
    model.white_decision_band =
        (otsu.white_lower_decile_gray - threshold_f) /
        classification.white_confidence_min;
    model.valid = true;
    if (!IsValidBEVPixelClassificationModel(model)) {
        model.valid = false;
    }
    return model;
}

inline BEVPixelClassificationModel MakeBEVPixelClassificationModel(
    const OtsuThresholdResult& otsu) {
    return MakeBEVPixelClassificationModel(otsu,
                                           port::BEVClassificationParameters{});
}

inline BEVSimplePixelClass ClassifyBevPixel(
    std::uint8_t gray,
    const BEVPixelClassificationModel& model,
    const port::BEVClassificationParameters& classification) {
    if (!IsValidBEVPixelClassificationModel(model)) {
        return BEVSimplePixelClass::kInvalid;
    }

    const float threshold_f = static_cast<float>(model.threshold);
    const float gray_f = static_cast<float>(gray);
    const float band =
        gray > model.threshold ? model.white_decision_band : model.black_decision_band;
    const float confidence = DecisionConfidence(std::abs(gray_f - threshold_f), band);
    if (confidence < classification.unknown_confidence_min) {
        return BEVSimplePixelClass::kUnknown;
    }
    if (gray > model.threshold) {
        return confidence >= classification.white_confidence_min
                   ? BEVSimplePixelClass::kWhite
                   : BEVSimplePixelClass::kUnknown;
    }
    return BEVSimplePixelClass::kBlack;
}

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BEV_PIXEL_CLASSIFIER_HPP
