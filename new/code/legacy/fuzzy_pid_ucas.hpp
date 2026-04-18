#ifndef LS2K_LEGACY_FUZZY_PID_UCAS_HPP
#define LS2K_LEGACY_FUZZY_PID_UCAS_HPP

#include <array>

namespace ls2k::legacy {

class FuzzyPidUcas {
public:
    void InitMH(int p_mode);
    float DuoJiGetP(int view_h, int view_e) const;

private:
    std::array<float, 7> p_table_left_{10.0F, 11.15F, 12.83F, 14.75F, 16.96F, 19.51F, 22.4F};
    std::array<float, 7> p_table_right_{10.0F, 11.15F, 12.83F, 14.75F, 16.96F, 19.51F, 22.4F};
    float size_of_view_h_ = 40.0F;
    float size_of_view_e_ = 65.0F;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_FUZZY_PID_UCAS_HPP
