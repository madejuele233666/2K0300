#include "vision/bev/reference_connectivity.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "vision/bev/bev_simple_perception.hpp"

namespace ls2k::vision {
namespace {

struct ConnectedPrefix {
    std::size_t sample_count = 0U;
    bool blocked_by_black = false;
};

bool PixelIsBlack(const ReferenceConnectivityFrameView& frame, int row, int col) {
    if (row < 0 || col < 0 ||
        row >= frame.gray_frame.height ||
        col >= frame.gray_frame.width) {
        return false;
    }
    const std::size_t index =
        static_cast<std::size_t>(row) *
            static_cast<std::size_t>(frame.gray_frame.stride) +
        static_cast<std::size_t>(col);
    return ClassifyBevPixel(frame.gray_frame.gray[index],
                            frame.classification_model,
                            frame.classification) ==
           BEVSimplePixelClass::kBlack;
}

int PixelIndexFromCenteredCoordinate(float value, int limit) {
    return std::clamp(static_cast<int>(std::floor(value + 0.5F)), 0, limit - 1);
}

bool ClipRange(float p, float q, float& t0, float& t1) {
    if (p == 0.0F) {
        return q >= 0.0F;
    }
    const float ratio = q / p;
    if (p < 0.0F) {
        if (ratio > t1) {
            return false;
        }
        if (ratio > t0) {
            t0 = ratio;
        }
        return true;
    }
    if (ratio < t0) {
        return false;
    }
    if (ratio < t1) {
        t1 = ratio;
    }
    return true;
}

bool ClipImageSegmentToFrame(const port::LegacyCameraFrameView& frame,
                             const port::ImagePoint& a,
                             const port::ImagePoint& b,
                             port::ImagePoint& clipped_a,
                             port::ImagePoint& clipped_b) {
    if (!frame.Valid()) {
        return false;
    }
    const float max_col = static_cast<float>(frame.width - 1);
    const float max_row = static_cast<float>(frame.height - 1);
    const float dx = b.col_px - a.col_px;
    const float dy = b.row_px - a.row_px;
    float t0 = 0.0F;
    float t1 = 1.0F;
    if (!ClipRange(-dx, a.col_px, t0, t1) ||
        !ClipRange(dx, max_col - a.col_px, t0, t1) ||
        !ClipRange(-dy, a.row_px, t0, t1) ||
        !ClipRange(dy, max_row - a.row_px, t0, t1)) {
        return false;
    }
    clipped_a.col_px = a.col_px + t0 * dx;
    clipped_a.row_px = a.row_px + t0 * dy;
    clipped_b.col_px = a.col_px + t1 * dx;
    clipped_b.row_px = a.row_px + t1 * dy;
    return true;
}

bool ImageSegmentHasNoBlack(const ReferenceConnectivityFrameView& frame,
                            const port::ImagePoint& a,
                            const port::ImagePoint& b) {
    int col = PixelIndexFromCenteredCoordinate(a.col_px, frame.gray_frame.width);
    int row = PixelIndexFromCenteredCoordinate(a.row_px, frame.gray_frame.height);
    const int end_col = PixelIndexFromCenteredCoordinate(b.col_px, frame.gray_frame.width);
    const int end_row = PixelIndexFromCenteredCoordinate(b.row_px, frame.gray_frame.height);

    const float x0 = a.col_px + 0.5F;
    const float y0 = a.row_px + 0.5F;
    const float x1 = b.col_px + 0.5F;
    const float y1 = b.row_px + 0.5F;
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const int step_col = dx > 0.0F ? 1 : (dx < 0.0F ? -1 : 0);
    const int step_row = dy > 0.0F ? 1 : (dy < 0.0F ? -1 : 0);
    const float inf = 1.0e30F;
    const float t_delta_col =
        step_col == 0 ? inf : std::abs(1.0F / dx);
    const float t_delta_row =
        step_row == 0 ? inf : std::abs(1.0F / dy);
    float t_max_col = inf;
    if (step_col > 0) {
        t_max_col = (static_cast<float>(col + 1) - x0) / dx;
    } else if (step_col < 0) {
        t_max_col = (x0 - static_cast<float>(col)) / -dx;
    }
    float t_max_row = inf;
    if (step_row > 0) {
        t_max_row = (static_cast<float>(row + 1) - y0) / dy;
    } else if (step_row < 0) {
        t_max_row = (y0 - static_cast<float>(row)) / -dy;
    }

    const auto check = [&frame](int check_row, int check_col) {
        return !PixelIsBlack(frame, check_row, check_col);
    };

    constexpr float kTieTolerance = 1.0e-6F;
    while (true) {
        if (!check(row, col)) {
            return false;
        }
        if (col == end_col && row == end_row) {
            return true;
        }

        if (std::fabs(t_max_col - t_max_row) <= kTieTolerance) {
            if (step_col != 0 && !check(row, col + step_col)) {
                return false;
            }
            if (step_row != 0 && !check(row + step_row, col)) {
                return false;
            }
            col += step_col;
            row += step_row;
            t_max_col += t_delta_col;
            t_max_row += t_delta_row;
            continue;
        }
        if (t_max_col < t_max_row) {
            col += step_col;
            t_max_col += t_delta_col;
        } else {
            row += step_row;
            t_max_row += t_delta_row;
        }
    }
}

ConnectedPrefix FindConnectedLeadingPrefix(const ReferenceConnectivityFrameView& frame,
                                           const port::BEVReferencePath& path) {
    bool have_previous = true;
    port::BEVPoint previous{0.0F, 0.0F};
    ConnectedPrefix prefix{};
    for (const port::BEVPathSample& sample : path.sampled_path) {
        if (!sample.present ||
            !std::isfinite(sample.point.forward_m) ||
            !std::isfinite(sample.point.lateral_m)) {
            break;
        }
        if (have_previous &&
            !BEVSegmentHasNoBlackPixels(frame, previous, sample.point)) {
            prefix.blocked_by_black = true;
            return prefix;
        }
        previous = sample.point;
        have_previous = true;
        ++prefix.sample_count;
    }
    return prefix;
}

port::VisualReferenceCandidate ClipCandidateToPrefix(
    const port::VisualReferenceCandidate& candidate,
    std::size_t sample_count) {
    port::VisualReferenceCandidate clipped = candidate;
    for (std::size_t index = sample_count;
         index < clipped.reference_path.sampled_path.size();
         ++index) {
        clipped.reference_path.sampled_path[index] = port::BEVPathSample{};
    }
    clipped.reason = candidate.reason.empty()
                         ? "connectivity_prefix_clipped"
                         : candidate.reason + ":connectivity_prefix_clipped";
    return clipped;
}

}  // namespace

bool BEVSegmentHasNoBlackPixels(const ReferenceConnectivityFrameView& frame,
                                const port::BEVPoint& a,
                                const port::BEVPoint& b) {
    port::ImagePoint image_a{};
    port::ImagePoint image_b{};
    if (!frame.projector.ProjectVehicleToImage(a, image_a) ||
        !frame.projector.ProjectVehicleToImage(b, image_b)) {
        return true;
    }
    port::ImagePoint clipped_a{};
    port::ImagePoint clipped_b{};
    if (!ClipImageSegmentToFrame(frame.gray_frame, image_a, image_b, clipped_a, clipped_b)) {
        return true;
    }
    return ImageSegmentHasNoBlack(frame, clipped_a, clipped_b);
}

bool ReferencePathHasNoBlackSegments(const ReferenceConnectivityFrameView& frame,
                                     const port::BEVReferencePath& path) {
    return !FindConnectedLeadingPrefix(frame, path).blocked_by_black;
}

void AppendConnectedVisualReferenceCandidate(
    const ReferenceConnectivityFrameView& frame,
    const port::VisualReferenceCandidate& candidate,
    std::vector<port::VisualReferenceCandidate>& accepted_candidates) {
    if (!candidate.present) {
        accepted_candidates.push_back(candidate);
        return;
    }

    const ConnectedPrefix prefix =
        FindConnectedLeadingPrefix(frame, candidate.reference_path);
    if (!prefix.blocked_by_black) {
        accepted_candidates.push_back(candidate);
        return;
    }
    accepted_candidates.push_back(ClipCandidateToPrefix(candidate, prefix.sample_count));
}

}  // namespace ls2k::vision
