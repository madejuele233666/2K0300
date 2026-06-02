#include "reference/reference_tracking_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace ls2k::reference {
namespace {

bool IsReferencePointPresent(const port::BEVPathSample& sample) {
    return sample.present && std::isfinite(sample.point.forward_m) &&
           std::isfinite(sample.point.lateral_m);
}

port::ReferenceTrackingGeometry UncomputedOutput(const std::string& reason) {
    port::ReferenceTrackingGeometry output{};
    output.computed = false;
    output.reason = reason;
    return output;
}

std::size_t FirstPresentSegmentStart(const port::BEVReferencePath& reference_path) {
    for (std::size_t index = 0; index < reference_path.sampled_path.size(); ++index) {
        if (IsReferencePointPresent(reference_path.sampled_path[index])) {
            return index;
        }
    }
    return reference_path.sampled_path.size();
}

std::size_t TrackingFitMinSamples(const port::BEVControlModelParameters& control_model) {
    constexpr int kQuadraticFitMinSamples = 3;
    const int bounded =
        std::clamp(control_model.tracking_fit_min_samples,
                   kQuadraticFitMinSamples,
                   static_cast<int>(port::kBevReferenceSampleCount));
    return static_cast<std::size_t>(bounded);
}

bool Solve3x3(float matrix[3][4], std::array<float, 3>& output) {
    constexpr float kEpsilon = 1.0e-6F;
    for (int pivot = 0; pivot < 3; ++pivot) {
        int best_row = pivot;
        float best_abs = std::abs(matrix[pivot][pivot]);
        for (int row = pivot + 1; row < 3; ++row) {
            const float value_abs = std::abs(matrix[row][pivot]);
            if (value_abs > best_abs) {
                best_abs = value_abs;
                best_row = row;
            }
        }
        if (best_abs <= kEpsilon) {
            return false;
        }
        if (best_row != pivot) {
            for (int col = pivot; col < 4; ++col) {
                std::swap(matrix[pivot][col], matrix[best_row][col]);
            }
        }
        const float divisor = matrix[pivot][pivot];
        for (int col = pivot; col < 4; ++col) {
            matrix[pivot][col] /= divisor;
        }
        for (int row = 0; row < 3; ++row) {
            if (row == pivot) {
                continue;
            }
            const float factor = matrix[row][pivot];
            for (int col = pivot; col < 4; ++col) {
                matrix[row][col] -= factor * matrix[pivot][col];
            }
        }
    }

    output[0] = matrix[0][3];
    output[1] = matrix[1][3];
    output[2] = matrix[2][3];
    return std::isfinite(output[0]) && std::isfinite(output[1]) && std::isfinite(output[2]);
}

}  // namespace

port::ReferenceTrackingGeometry ComputeReferenceTrackingGeometry(
    const port::BEVReferencePath& reference_path,
    const port::ReferenceUsability& usability,
    const port::BEVControlModelParameters& control_model) {
    if (!usability.usable) {
        return UncomputedOutput(usability.reason);
    }

    const std::size_t start_index = FirstPresentSegmentStart(reference_path);
    if (start_index >= reference_path.sampled_path.size()) {
        return UncomputedOutput("tracking_geometry_unavailable");
    }

    const std::size_t bounded_count =
        std::min(usability.leading_usable_samples,
                 reference_path.sampled_path.size() - start_index);
    const std::size_t min_samples = TrackingFitMinSamples(control_model);

    float sum_x0 = 0.0F;
    float sum_x1 = 0.0F;
    float sum_x2 = 0.0F;
    float sum_x3 = 0.0F;
    float sum_x4 = 0.0F;
    float sum_y = 0.0F;
    float sum_xy = 0.0F;
    float sum_x2y = 0.0F;
    float evaluation_forward_m = 0.0F;
    std::size_t used_count = 0;

    for (std::size_t index = 0; index < bounded_count; ++index) {
        const port::BEVPathSample& sample = reference_path.sampled_path[start_index + index];
        if (!IsReferencePointPresent(sample)) {
            break;
        }
        const float x = sample.point.forward_m;
        const float y = sample.point.lateral_m;
        if (used_count == 0U) {
            evaluation_forward_m = x;
        }
        const float x2 = x * x;
        const float x3 = x2 * x;
        const float x4 = x2 * x2;
        sum_x0 += 1.0F;
        sum_x1 += x;
        sum_x2 += x2;
        sum_x3 += x3;
        sum_x4 += x4;
        sum_y += y;
        sum_xy += x * y;
        sum_x2y += x2 * y;
        ++used_count;
    }

    if (used_count < min_samples) {
        return UncomputedOutput("insufficient_tracking_geometry_samples");
    }

    float matrix[3][4] = {
        {sum_x0, sum_x1, sum_x2, sum_y},
        {sum_x1, sum_x2, sum_x3, sum_xy},
        {sum_x2, sum_x3, sum_x4, sum_x2y},
    };
    std::array<float, 3> coeff{};
    if (!Solve3x3(matrix, coeff)) {
        return UncomputedOutput("tracking_geometry_fit_degenerate");
    }

    const float c = coeff[0];
    const float b = coeff[1];
    const float a = coeff[2];
    const float evaluation_forward_m2 = evaluation_forward_m * evaluation_forward_m;
    const float lateral_offset_m =
        a * evaluation_forward_m2 + b * evaluation_forward_m + c;
    const float heading_error_rad = std::atan(b + 2.0F * a * evaluation_forward_m);
    const float curvature_m_inv = (2.0F * a) / std::pow(1.0F + b * b, 1.5F);
    if (!std::isfinite(lateral_offset_m) || !std::isfinite(heading_error_rad) ||
        !std::isfinite(curvature_m_inv)) {
        return UncomputedOutput("tracking_geometry_nonfinite");
    }

    port::ReferenceTrackingGeometry output{};
    output.computed = true;
    output.lateral_offset_m = lateral_offset_m;
    output.heading_error_rad = heading_error_rad;
    output.curvature_m_inv = curvature_m_inv;
    output.sample_count = used_count;
    output.reason = "ok";
    return output;
}

}  // namespace ls2k::reference
