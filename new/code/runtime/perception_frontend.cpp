#include "runtime/perception_frontend.hpp"

/// 感知前端实现 —— 运行时感知管线调度。
/// 负责故障注入、诊断发布、感知结果缓存和前端线程生命周期管理。

#include <cstdlib>
#include <optional>
#include <string>

#include "port/perf_counter.hpp"

namespace ls2k::runtime {
namespace {

/// 从环境变量读取正整数值（用于故障注入间隔）
/// @param key         环境变量名
/// @param diagnostics 诊断输出接口
/// @param now_ms      当前时间戳
/// @return            正整数值，无效或不存在时返回 0
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

/// 构建丢帧回退感知结果（用于故障注入场景 —— 模拟帧丢失，保持帧 ID 但标记为不新鲜）
/// @param capture  相机捕获信息
/// @return         构建的降级感知结果
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

/// 构造感知前端：保存相机适配器、运行时状态和诊断接口引用
/// @param camera      相机适配器
/// @param state       运行时状态
/// @param diagnostics 诊断输出接口
PerceptionFrontend::PerceptionFrontend(CameraFrameStore& frame_store,
                                       RuntimeState& state,
                                       port::DiagnosticSink& diagnostics)
    : frame_store_(frame_store), state_(state), diagnostics_(diagnostics) {}

/// 配置感知前端：委派到帧感知管线的配置
/// @param params  运行时参数
/// @return        配置是否成功
bool PerceptionFrontend::Configure(const port::RuntimeParameters& params) {
    return frame_pipeline_.Configure(params, diagnostics_);
}

/// 消费普通参考连续性复位请求：检查复位代数，若有新请求则清空 reference hold
void PerceptionFrontend::ConsumeMemoryResetRequest() {
    const uint64_t generation = state_.perception_memory_reset_generation.load();
    if (generation == consumed_perception_memory_reset_generation_) {
        return;
    }
    frame_pipeline_.ResetReferenceMemory();
    consumed_perception_memory_reset_generation_ = generation;
}

/// 处理一帧图像：故障注入 → 空帧处理 → Otsu → sparse BEV 感知 → 结果缓存。
/// 支持通过环境变量 LS2K_FAULT_INJECT_DROP_FRAME_EVERY_N 模拟帧丢失。
/// @param params  运行时参数
void PerceptionFrontend::ProcessOneFrame(const port::RuntimeParameters& params) {
    LS2K_PERF_SCOPE(port::PerfStage::kPerceptionFrame);
    ConsumeMemoryResetRequest();

    const std::optional<CameraFrameHandle> latest =
        frame_store_.TryGetLatestAfter(last_processed_frame_id_);
    if (!latest.has_value()) {
        return;
    }
    port::LegacyCameraFrame frame{};
    if (!frame_store_.CopyFrame(*latest, frame)) {
        port::EmitRateLimited(diagnostics_,
                              {port::DiagnosticLevel::kWarning,
                               "perception.frame_store.copy_miss",
                               "latest camera frame was overwritten before perception could copy it",
                               port::NowMs()},
                              1000);
        last_processed_frame_id_ = latest->frame_id;
        return;
    }

    ++processed_frames_;
    last_processed_frame_id_ = latest->frame_id;
    port::CameraCapture capture{};
    capture.has_frame = true;
    capture.marker = port::CameraGeometryMarker::kPhase1Adapted;
    capture.frame_id = latest->frame_id;
    capture.capture_time_ms = latest->capture_time_ms;
    capture.source_width = latest->width;
    capture.source_height = latest->height;
    capture.view = frame.View(latest->frame_id, latest->capture_time_ms);
    {
        LS2K_PERF_SCOPE(port::PerfStage::kCameraFrameAge);
        (void)capture.capture_time_ms;
    }
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
        {
            LS2K_PERF_SCOPE(port::PerfStage::kPerceptionPublish);
            std::lock_guard<std::mutex> lock(state_.shared_mutex);
            state_.perception = fallback;
            ++state_.perception_publish_count;
        }
        return;
    }

    port::MotionHistory motion_history{};
    {
        std::lock_guard<std::mutex> lock(state_.shared_mutex);
        motion_history = state_.motion_history;
    }
    port::PerceptionResult perception =
        frame_pipeline_.ProcessFrame(capture, params, motion_history);

    {
        LS2K_PERF_SCOPE(port::PerfStage::kPerceptionPublish);
        std::lock_guard<std::mutex> lock(state_.shared_mutex);
        state_.perception = perception;
        ++state_.perception_publish_count;
    }
}

}  // namespace ls2k::runtime
