#include "legacy/fuzzy_pid_ucas.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>

namespace ls2k::legacy {
namespace {

constexpr std::array<std::array<uint8_t, 4>, 4> kDuojiMap{{
    {{0, 1, 2, 3}},
    {{1, 2, 3, 4}},
    {{3, 4, 5, 6}},
    {{5, 6, 6, 6}},
}};

float Clamp(float value, float low, float high) {
    return std::max(low, std::min(value, high));
}

float ApproxIndex(float view_h, float size_h) {
    return Clamp((view_h * 3.0F) / size_h, 0.0F, 3.0F);
}

float ApproxError(float view_e, float size_e) {
    return Clamp((std::abs(view_e) * 3.0F) / size_e, 0.0F, 3.0F);
}

}  // namespace

void FuzzyPidUcas::InitMH(int p_mode) {
    static constexpr std::array<float, 7> kMode1{8.0F, 8.98F, 10.08F, 11.31F, 12.69F, 14.25F, 16.0F};
    static constexpr std::array<float, 7> kMode2{10.0F, 11.23F, 12.6F, 14.11F, 15.87F, 17.818F, 20.0F};
    static constexpr std::array<float, 7> kMode3{10.0F, 11.15F, 12.83F, 14.75F, 16.96F, 19.51F, 22.4F};
    static constexpr std::array<float, 7> kMode4{10.0F, 11.61F, 13.479F, 15.659F, 18.18F, 21.107F, 25.0F};

    switch (p_mode) {
        case 1:
            p_table_left_ = kMode1;
            p_table_right_ = kMode1;
            break;
        case 2:
            p_table_left_ = kMode2;
            p_table_right_ = kMode2;
            break;
        case 3:
            p_table_left_ = kMode3;
            p_table_right_ = kMode3;
            break;
        case 4:
            p_table_left_ = kMode4;
            p_table_right_ = kMode4;
            break;
        default:
            p_table_left_ = kMode3;
            p_table_right_ = kMode3;
            break;
    }
}

float FuzzyPidUcas::DuoJiGetP(int view_h, int view_e) const {
    const float vh = ApproxIndex(static_cast<float>(view_h - 2), size_of_view_h_);
    const float ve = ApproxError(static_cast<float>(view_e), size_of_view_e_);

    const int vh1 = static_cast<int>(std::floor(vh));
    const int vh2 = std::min(vh1 + 1, 3);
    const int ve1 = static_cast<int>(std::floor(ve));
    const int ve2 = std::min(ve1 + 1, 3);

    const float x2y =
        (kDuojiMap[vh1][ve2] - kDuojiMap[vh1][ve1]) * (ve - static_cast<float>(ve1)) + kDuojiMap[vh1][ve1];
    const float x1y =
        (kDuojiMap[vh2][ve2] - kDuojiMap[vh2][ve1]) * (ve - static_cast<float>(ve1)) + kDuojiMap[vh2][ve1];
    const float y2x =
        (kDuojiMap[vh2][ve1] - kDuojiMap[vh1][ve1]) * (vh - static_cast<float>(vh1)) + kDuojiMap[vh1][ve1];
    const float y1x =
        (kDuojiMap[vh2][ve2] - kDuojiMap[vh1][ve2]) * (vh - static_cast<float>(vh1)) + kDuojiMap[vh1][ve2];

    const float index = Clamp((x2y + x1y + y2x + y1x) * 0.25F, 0.0F, 6.0F);
    const int i1 = static_cast<int>(std::floor(index));
    const int i2 = std::min(i1 + 1, 6);
    const float blend = index - static_cast<float>(i1);

    if (view_e < 0) {
        return p_table_left_[i1] + (p_table_left_[i2] - p_table_left_[i1]) * blend;
    }
    return p_table_right_[i1] + (p_table_right_[i2] - p_table_right_[i1]) * blend;
}

}  // namespace ls2k::legacy
