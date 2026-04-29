#include "legacy/steering_bev_projector.hpp"

// BEV 投影器实现。通过 4 对对应点（图像↔BEV）解算 3x3 单应矩阵，
// 实现图像坐标与车辆坐标系之间的双向投影。

#include <algorithm>
#include <array>
#include <cmath>

namespace ls2k::legacy {
namespace {

// 二维点（双精度，用于单应矩阵计算）
struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

// 用高斯消元法（部分主元）解 8 元线性方程组
// 矩阵大小为 8x9（最后一列为增广列）。用于从 4 组对应点解单应矩阵的 8 个自由度
bool SolveLinearSystem8(std::array<std::array<double, 9>, 8>& matrix) {
    for (int col = 0; col < 8; ++col) {
        // 列主元选择：寻找当前列绝对值最大的行
        int pivot = col;
        double pivot_abs = std::abs(matrix[pivot][col]);
        for (int row = col + 1; row < 8; ++row) {
            const double candidate_abs = std::abs(matrix[row][col]);
            if (candidate_abs > pivot_abs) {
                pivot = row;
                pivot_abs = candidate_abs;
            }
        }
        // 奇异性检查：主元接近零意味着矩阵奇异
        if (pivot_abs < 1e-9) {
            return false;
        }
        // 交换当前行与主元行
        if (pivot != col) {
            std::swap(matrix[pivot], matrix[col]);
        }

        // 归一化主元行
        const double pivot_value = matrix[col][col];
        for (int current = col; current < 9; ++current) {
            matrix[col][current] /= pivot_value;
        }
        // 消去其他行的当前列
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

// 从 4 组对应点构建 3x3 单应矩阵
// src: 源坐标（图像或 BEV），dst: 目标坐标（BEV 或图像）
// homography: 输出 3x3 单应矩阵（以 row-major array 形式存储，最后一项 h33=1.0）
// 构造经典 DLT（Direct Linear Transform）方程组，每组对应点生成 2 个方程
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

        // 第一行: -u = (h11*x + h12*y + h13 - h31*x*u - h32*y*u) / h33
        // 整理成 Ax = b 形式：h11*x + h12*y + h13 + 0 + 0 + 0 - h31*x*u - h32*y*u = u
        matrix[row][0] = x;
        matrix[row][1] = y;
        matrix[row][2] = 1.0;
        matrix[row][3] = 0.0;
        matrix[row][4] = 0.0;
        matrix[row][5] = 0.0;
        matrix[row][6] = -u * x;
        matrix[row][7] = -u * y;
        matrix[row][8] = u;

        // 第二行: -v = (h21*x + h22*y + h23 - h31*x*v - h32*y*v) / h33
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

    // 解 8x8 线性方程组（h33 固定为 1.0）
    if (!SolveLinearSystem8(matrix)) {
        return false;
    }

    // 从消元结果中提取 8 个自由参数
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

// 应用单应矩阵，将点 (x,y) 从源平面投影到目标平面
bool ApplyHomography(const std::array<double, 9>& homography, double x, double y, Point2D& out) {
    // 分母为 h31*x + h32*y + h33，用于透视除法和齐次坐标归一化
    const double denom = homography[6] * x + homography[7] * y + homography[8];
    if (std::abs(denom) < 1e-9) {
        return false;
    }
    out.x = (homography[0] * x + homography[1] * y + homography[2]) / denom;
    out.y = (homography[3] * x + homography[4] * y + homography[5]) / denom;
    return std::isfinite(out.x) && std::isfinite(out.y);
}

// 从标定参数中提取图像坐标点（列, 行 → x, y）
std::array<Point2D, port::kBevCalibrationPointCount> BuildImagePoints(
    const port::BEVProjectorCalibration& calibration) {
    std::array<Point2D, port::kBevCalibrationPointCount> points{};
    for (std::size_t i = 0; i < port::kBevCalibrationPointCount; ++i) {
        points[i].x = calibration.source_points[i].col_px;
        points[i].y = calibration.source_points[i].row_px;
    }
    return points;
}

// 从标定参数中提取 BEV 坐标点（横向, 前向 → x, y）
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

// 用标定参数配置投影器，计算正向和反向两个单应矩阵
bool BEVProjector::Configure(const port::BEVProjectorCalibration& calibration) {
    calibration_ = calibration;
    configured_ = false;
    if (!calibration.valid) {
        return false;
    }

    // 构造图像→BEV 和 BEV→图像的两个单应矩阵
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

// 图像坐标 → BEV 坐标（通过图像→BEV 单应矩阵）
bool BEVProjector::ProjectImageToVehicle(const port::ImagePoint& image_point,
                                         port::BEVPoint& vehicle_point) const {
    if (!configured_) {
        return false;
    }
    Point2D point{};
    if (!ApplyHomography(image_to_bev_, image_point.col_px, image_point.row_px, point)) {
        return false;
    }
    // 注意：单应矩阵的输出 x=横向(lateral), y=前向(forward)
    vehicle_point.lateral_m = static_cast<float>(point.x);
    vehicle_point.forward_m = static_cast<float>(point.y);
    return true;
}

// BEV 坐标 → 图像坐标（通过 BEV→图像 单应矩阵）
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
