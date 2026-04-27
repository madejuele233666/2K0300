#ifndef LS2K_LEGACY_STEERING_BEV_PROJECTOR_HPP
#define LS2K_LEGACY_STEERING_BEV_PROJECTOR_HPP

#include <array>

#include "port/control_types.hpp"

namespace ls2k::legacy {

class BEVProjector {
public:
    bool Configure(const port::BEVProjectorCalibration& calibration);

    bool Valid() const {
        return configured_;
    }

    const port::BEVProjectorCalibration& Calibration() const {
        return calibration_;
    }

    bool ProjectImageToVehicle(const port::ImagePoint& image_point, port::BEVPoint& vehicle_point) const;
    bool ProjectVehicleToImage(const port::BEVPoint& vehicle_point, port::ImagePoint& image_point) const;

private:
    port::BEVProjectorCalibration calibration_{};
    std::array<double, 9> image_to_bev_{};
    std::array<double, 9> bev_to_image_{};
    bool configured_ = false;
};

}  // namespace ls2k::legacy

#endif  // LS2K_LEGACY_STEERING_BEV_PROJECTOR_HPP
