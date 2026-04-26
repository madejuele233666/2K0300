#include "legacy/camera_logic.hpp"

#include <array>
#include <cstddef>

#include "legacy/steering_scene_common.hpp"
#include "legacy/steering_gyro_continuity.hpp"
#include "legacy/steering_scene_orchestrator.hpp"

namespace ls2k::legacy {
namespace {

int OtsuThresholdFast(const port::LegacyCameraFrame& frame) {
    std::array<int, 256> hist{};
    if (frame.width <= 0 || frame.height <= 0) {
        return 0;
    }

    int samples = 0;
    for (int r = 0; r < frame.height; r += 2) {
        for (int c = 0; c < frame.width; c += 2) {
            const uint8_t pixel = frame.gray[static_cast<std::size_t>(r) * frame.width + c];
            ++hist[pixel];
            ++samples;
        }
    }
    if (samples == 0) {
        return 0;
    }

    double sum = 0.0;
    for (int i = 0; i < 256; ++i) {
        sum += static_cast<double>(i * hist[i]);
    }

    double sum_b = 0.0;
    int weight_b = 0;
    double max_var = -1.0;
    int threshold = 0;
    for (int i = 0; i < 256; ++i) {
        weight_b += hist[i];
        if (weight_b == 0) {
            continue;
        }
        const int weight_f = samples - weight_b;
        if (weight_f == 0) {
            break;
        }
        sum_b += static_cast<double>(i * hist[i]);
        const double mean_b = sum_b / static_cast<double>(weight_b);
        const double mean_f = (sum - sum_b) / static_cast<double>(weight_f);
        const double between = static_cast<double>(weight_b) * static_cast<double>(weight_f) *
                               (mean_b - mean_f) * (mean_b - mean_f);
        if (between > max_var) {
            max_var = between;
            threshold = i;
        }
    }
    return threshold;
}

}  // namespace

SteeringAnalysisResult AnalyzeFrame(const port::LegacyCameraFrame& frame,
                                    const port::RuntimeParameters& params,
                                    const port::LegacySteeringState& prior_state,
                                    const port::ImuSample& imu,
                                    bool low_voltage_emergency,
                                    uint64_t frame_id,
                                    uint64_t capture_time_ms) {
    SteeringSceneContext context{frame, params, prior_state, imu, capture_time_ms, {}};
    context.metrics =
        ExtractLaneMetrics(frame, OtsuThresholdFast(frame), params, prior_state, imu, capture_time_ms);
    SteeringAnalysisResult analysis =
        OrchestrateSteeringScenes(context, low_voltage_emergency, frame_id, capture_time_ms);
    analysis.track_history_snapshot = BuildTrackHistorySnapshot(context.metrics, prior_state);
    analysis.gyro_continuity_state =
        ComputeGyroContinuityConstraint(prior_state, imu, capture_time_ms).next_state;
    return analysis;
}

}  // namespace ls2k::legacy
