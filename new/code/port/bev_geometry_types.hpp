#ifndef LS2K_PORT_BEV_GEOMETRY_TYPES_HPP
#define LS2K_PORT_BEV_GEOMETRY_TYPES_HPP

#include <array>
#include <cstddef>
#include <string>

namespace ls2k::port {

struct ImagePoint {
    float row_px = 0.0F;
    float col_px = 0.0F;
};

struct BEVPoint {
    float forward_m = 0.0F;
    float lateral_m = 0.0F;
};

constexpr std::size_t kBevCalibrationPointCount = 4;
constexpr std::size_t kBevReferenceSampleCount = 24;

struct BEVProjectorCalibration {
    bool valid = true;
    std::array<ImagePoint, kBevCalibrationPointCount> source_points{
        {ImagePoint{220.0F, 19.0F},
         ImagePoint{220.0F, 305.0F},
         ImagePoint{68.0F, 108.0F},
         ImagePoint{68.0F, 220.0F}}};
    std::array<BEVPoint, kBevCalibrationPointCount> target_points{
        {BEVPoint{0.061F, -0.21F},
         BEVPoint{0.061F, 0.21F},
         BEVPoint{0.610F, -0.21F},
         BEVPoint{0.610F, 0.21F}}};
    int debug_grid_width = 160;
    int debug_grid_height = 128;
    std::string projector_id = "bev_projector_true_bev_manual_forward_scale_v5";
    std::string projector_hash = "bev-projector-true-bev-manual-forward-scale-20260428";
};

struct BEVGeometryParameters {
    std::array<float, kBevReferenceSampleCount> forward_samples_m{
        {0.061000F,
         0.123565F,
         0.186130F,
         0.248696F,
         0.311261F,
         0.373826F,
         0.436391F,
         0.498957F,
         0.561522F,
         0.624087F,
         0.686652F,
         0.749217F,
         0.811783F,
         0.874348F,
         0.936913F,
         0.999478F,
         1.062043F,
         1.124609F,
         1.187174F,
         1.249739F,
         1.312304F,
         1.374870F,
         1.437435F,
         1.500000F}};
    float search_lateral_limit_m = 0.65F;
    float lateral_step_m = 0.02F;
};

struct BEVClassificationParameters {
    float white_confidence_min = 0.55F;
    float unknown_confidence_min = 0.25F;
    int hold_last_max_cycles = 8;
};

struct BEVControlModelParameters {
    double lookahead_visible_range_ratio = 0.35;
    double lookahead_min_m = 0.160043;
    double lookahead_max_m = 0.259087;
    double pure_pursuit_gain = 1.0;
    double curvature_command_limit = 0.12;
    double curvature_to_yaw_rate_target_gain = 12000.0;
    int min_leading_reference_samples = 3;
};

}  // namespace ls2k::port

#endif  // LS2K_PORT_BEV_GEOMETRY_TYPES_HPP
