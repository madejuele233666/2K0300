#include "legacy/steering_bev_projector.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace ls2k::legacy {
namespace {

struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

bool SolveLinearSystem8(std::array<std::array<double, 9>, 8>& matrix) {
    for (int col = 0; col < 8; ++col) {
        int pivot = col;
        double pivot_abs = std::abs(matrix[pivot][col]);
        for (int row = col + 1; row < 8; ++row) {
            const double candidate_abs = std::abs(matrix[row][col]);
            if (candidate_abs > pivot_abs) {
                pivot = row;
                pivot_abs = candidate_abs;
            }
        }
        if (pivot_abs < 1e-9) {
            return false;
        }
        if (pivot != col) {
            std::swap(matrix[pivot], matrix[col]);
        }

        const double pivot_value = matrix[col][col];
        for (int current = col; current < 9; ++current) {
            matrix[col][current] /= pivot_value;
        }
        for (int row = 0; row < 8; ++row) {
            if (row == col) {
                continue;
            }
            const double factor = matrix[row][col];
            if (std::abs(factor) < 1e-9) {
                continue;
            }
            for (int current = col; current < 9; ++current) {
                matrix[row][current] -= factor * matrix[col][current];
            }
        }
    }
    return true;
}

bool BuildHomography(const std::array<Point2D, port::kBevCalibrationPointCount>& src,
                     const std::array<Point2D, port::kBevCalibrationPointCount>& dst,
                     std::array<double, 9>& homography) {
    std::array<std::array<double, 9>, 8> matrix{};
    for (std::size_t i = 0; i < port::kBevCalibrationPointCount; ++i) {
        const double x = src[i].x;
        const double y = src[i].y;
        const double u = dst[i].x;
        const double v = dst[i].y;
        const std::size_t row = i * 2;

        matrix[row][0] = x;
        matrix[row][1] = y;
        matrix[row][2] = 1.0;
        matrix[row][3] = 0.0;
        matrix[row][4] = 0.0;
        matrix[row][5] = 0.0;
        matrix[row][6] = -u * x;
        matrix[row][7] = -u * y;
        matrix[row][8] = u;

        matrix[row + 1][0] = 0.0;
        matrix[row + 1][1] = 0.0;
        matrix[row + 1][2] = 0.0;
        matrix[row + 1][3] = x;
        matrix[row + 1][4] = y;
        matrix[row + 1][5] = 1.0;
        matrix[row + 1][6] = -v * x;
        matrix[row + 1][7] = -v * y;
        matrix[row + 1][8] = v;
    }

    if (!SolveLinearSystem8(matrix)) {
        return false;
    }

    homography[0] = matrix[0][8];
    homography[1] = matrix[1][8];
    homography[2] = matrix[2][8];
    homography[3] = matrix[3][8];
    homography[4] = matrix[4][8];
    homography[5] = matrix[5][8];
    homography[6] = matrix[6][8];
    homography[7] = matrix[7][8];
    homography[8] = 1.0;
    return true;
}

bool ApplyHomography(const std::array<double, 9>& homography, double x, double y, Point2D& out) {
    const double denom = homography[6] * x + homography[7] * y + homography[8];
    if (std::abs(denom) < 1e-9) {
        return false;
    }
    out.x = (homography[0] * x + homography[1] * y + homography[2]) / denom;
    out.y = (homography[3] * x + homography[4] * y + homography[5]) / denom;
    return std::isfinite(out.x) && std::isfinite(out.y);
}

std::array<Point2D, port::kBevCalibrationPointCount> BuildImagePoints(
    const port::BEVProjectorCalibration& calibration) {
    std::array<Point2D, port::kBevCalibrationPointCount> points{};
    for (std::size_t i = 0; i < port::kBevCalibrationPointCount; ++i) {
        points[i].x = calibration.source_points[i].col_px;
        points[i].y = calibration.source_points[i].row_px;
    }
    return points;
}

std::array<Point2D, port::kBevCalibrationPointCount> BuildVehiclePoints(
    const port::BEVProjectorCalibration& calibration) {
    std::array<Point2D, port::kBevCalibrationPointCount> points{};
    for (std::size_t i = 0; i < port::kBevCalibrationPointCount; ++i) {
        points[i].x = calibration.target_points[i].lateral_m;
        points[i].y = calibration.target_points[i].forward_m;
    }
    return points;
}

}  // namespace

bool BEVProjector::Configure(const port::BEVProjectorCalibration& calibration) {
    calibration_ = calibration;
    configured_ = false;
    if (!calibration.valid) {
        return false;
    }

    const std::array<Point2D, port::kBevCalibrationPointCount> image_points =
        BuildImagePoints(calibration);
    const std::array<Point2D, port::kBevCalibrationPointCount> vehicle_points =
        BuildVehiclePoints(calibration);
    if (!BuildHomography(image_points, vehicle_points, image_to_bev_)) {
        return false;
    }
    if (!BuildHomography(vehicle_points, image_points, bev_to_image_)) {
        return false;
    }

    configured_ = true;
    return true;
}

bool BEVProjector::ProjectImageToVehicle(const port::ImagePoint& image_point,
                                         port::BEVPoint& vehicle_point) const {
    if (!configured_) {
        return false;
    }
    Point2D point{};
    if (!ApplyHomography(image_to_bev_, image_point.col_px, image_point.row_px, point)) {
        return false;
    }
    vehicle_point.lateral_m = static_cast<float>(point.x);
    vehicle_point.forward_m = static_cast<float>(point.y);
    return true;
}

bool BEVProjector::ProjectVehicleToImage(const port::BEVPoint& vehicle_point,
                                         port::ImagePoint& image_point) const {
    if (!configured_) {
        return false;
    }
    Point2D point{};
    if (!ApplyHomography(bev_to_image_, vehicle_point.lateral_m, vehicle_point.forward_m, point)) {
        return false;
    }
    image_point.col_px = static_cast<float>(point.x);
    image_point.row_px = static_cast<float>(point.y);
    return true;
}

}  // namespace ls2k::legacy
