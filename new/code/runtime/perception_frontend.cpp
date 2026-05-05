#include "runtime/perception_frontend.hpp"

// 感知前端实现 —— 运行时感知管线调度。
// 负责故障注入、诊断发布、感知结果缓存和前端线程生命周期管理。

#include <cstdlib>
#include <string>

#include "port/perf_counter.hpp"

namespace ls2k::runtime {
namespace {

// 从环境变量读取正整数值（用于故障注入间隔）
int ReadPositiveIntervalEnv(const char* key, port::DiagnosticSink& diagnostics, uint64_t now_ms) {
    const char* value = std::getenv(key);
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    try {
        const int parsed = std::stoi(value);
        if (parsed > 0) {
            return parsed;
        }
    } catch (...) {
    }
    port::EmitRateLimited(diagnostics,
                          {port::DiagnosticLevel::kWarning,
                           "perception.inject.invalid_env",
                           std::string("ignoring invalid fault-injection interval for ") + key + "=" + value,
                           now_ms},
                          1000);
    return 0;
}

// 构建丢帧回退感知结果（用于故障注入场景）
port::PerceptionResult BuildDroppedFrameFallback(const port::CameraCapture& capture) {
    port::PerceptionResult fallback{};
    fallback.published = true;
    fallback.fresh = false;
    fallback.frame_id = capture.frame_id;
    fallback.capture_time_ms = capture.capture_time_ms;
    fallback.publish_time_ms = capture.capture_time_ms;
    fallback.perception_tag = "injected-drop-frame";
    return fallback;
}

}  // namespace

PerceptionFrontend::PerceptionFrontend(port::ICameraAdapter& camera,
                                       RuntimeState& state,
                                       port::DiagnosticSink& diagnostics)
    : camera_(camera), state_(state), diagnostics_(diagnostics) {}

bool PerceptionFrontend::Configure(const port::RuntimeParameters& params) {
    return frame_pipeline_.Configure(params, diagnostics_);
}

bool PerceptionFrontend::ShouldMaterializeFrame(const port::RuntimeParameters& params) const {
    return params.steering_media_enabled && params.steering_media_publish_interval_ms > 0;
}

CameraFrameHandle PerceptionFrontend::RememberCameraCapture(const port::CameraCapture& capture,
                                                            const port::RuntimeParameters& params) {
    if (!capture.has_frame || !ShouldMaterializeFrame(params)) {
        return {};
    }
    CameraFrameHandle handle =
        MaterializeOwnedCameraFrame(state_.camera_frame_slots, state_.next_camera_frame_slot, capture.view);
    if (handle.valid) {
        state_.latest_camera_frame = handle;
        state_.recent_camera_captures.Push(handle);
    }
    return handle;
}

void PerceptionFrontend::ConsumeMemoryResetRequest() {
    const uint64_t generation = state_.perception_memory_reset_generation.load();
    if (generation == consumed_perception_memory_reset_generation_) {
        return;
    }
    frame_pipeline_.ResetMemory();
    consumed_perception_memory_reset_generation_ = generation;
}

// 处理一帧图像：故障注入 → 空帧处理 → Otsu → sparse BEV 感知
void PerceptionFrontend::ProcessOneFrame(const port::RuntimeParameters& params) {
    LS2K_PERF_SCOPE(port::PerfStage::kPerceptionFrame);
    ++processed_frames_;
    ConsumeMemoryResetRequest();

    // Foreground frame-ready path. Heavy perception stays outside PIT callback.
    const port::CameraCapture capture = camera_.Capture(diagnostics_);
    const int drop_frame_every_n =
        ReadPositiveIntervalEnv("LS2K_FAULT_INJECT_DROP_FRAME_EVERY_N", diagnostics_, port::NowMs());
    if (drop_frame_every_n > 0 && processed_frames_ % static_cast<uint64_t>(drop_frame_every_n) == 0) {
        port::EmitRateLimited(diagnostics_,
                              {port::DiagnosticLevel::kWarning,
                               "perception.inject.drop_frame",
                               "injecting bounded Phase B dropped-frame fault on the accepted runtime entrypoint",
                               port::NowMs()},
                              1000);
        port::PerceptionResult fallback = BuildDroppedFrameFallback(capture);
        fallback.publish_time_ms = port::NowMs();
        std::lock_guard<std::mutex> lock(state_.shared_mutex);
        (void)RememberCameraCapture(capture, params);
        state_.perception = fallback;
        ++state_.perception_publish_count;
        return;
    }

    if (!capture.has_frame) {
        port::PerceptionResult fallback{};
        fallback.published = true;
        fallback.fresh = false;
        fallback.frame_id = capture.frame_id;
        fallback.capture_time_ms = capture.capture_time_ms;
        fallback.publish_time_ms = port::NowMs();

        switch (capture.marker) {
            case port::CameraGeometryMarker::kEmptyFrame:
                fallback.perception_tag = "camera-empty";
                break;
            case port::CameraGeometryMarker::kAdapterNotReady:
                fallback.perception_tag = "camera-not-ready";
                break;
            case port::CameraGeometryMarker::kNonPhase1Geometry:
                fallback.perception_tag = "camera-bad-geometry";
                break;
            case port::CameraGeometryMarker::kAdaptationHookRouted:
                fallback.perception_tag = "camera-hook-routed";
                break;
            default:
                fallback.perception_tag = "camera-marker-unknown";
                break;
        }

        std::lock_guard<std::mutex> lock(state_.shared_mutex);
        (void)RememberCameraCapture(capture, params);
        state_.perception = fallback;
        ++state_.perception_publish_count;
        return;
    }

    port::PerceptionResult perception = frame_pipeline_.ProcessFrame(capture, params);

    std::lock_guard<std::mutex> lock(state_.shared_mutex);
    (void)RememberCameraCapture(capture, params);
    state_.perception = perception;
    ++state_.perception_publish_count;
}

}  // namespace ls2k::runtime
