#include "runtime/camera_capture_worker.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>

#include "platform/camera_frame_source.hpp"
#include "port/perf_counter.hpp"

namespace ls2k::runtime {
namespace {

uint64_t NowUs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace

CameraCaptureWorker::CameraCaptureWorker(CameraFrameStore& frame_store,
                                         port::DiagnosticSink& diagnostics)
    : frame_store_(frame_store), diagnostics_(diagnostics) {}

CameraCaptureWorker::~CameraCaptureWorker() {
    Stop();
}

bool CameraCaptureWorker::Start(const port::RuntimeParameters& params) {
    if (running_) {
        return true;
    }
    params_ = params;
    source_ = platform::MakeStartedCameraFrameSource(params_, diagnostics_);
    if (!source_) {
        diagnostics_.Emit({port::DiagnosticLevel::kFailSafe,
                           "camera_capture_worker.source_unavailable",
                           "camera capture worker could not start any frame source",
                           port::NowMs()});
        return false;
    }
    stop_requested_.store(false);
    running_.store(true);
    thread_ = std::thread([this]() { ThreadMain(); });
    diagnostics_.Emit({port::DiagnosticLevel::kInfo,
                       "camera_capture_worker.start",
                       "camera capture worker started",
                       port::NowMs()});
    return true;
}

void CameraCaptureWorker::Stop() {
    stop_requested_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    if (source_) {
        source_->Stop(diagnostics_);
        source_.reset();
    }
    running_.store(false);
}

bool CameraCaptureWorker::Running() const {
    return running_.load();
}

bool CameraCaptureWorker::ConvertRawFrame(const port::CameraRawFrame& raw,
                                          port::LegacyCameraFrame& out,
                                          port::CameraRawFrameMetadata& metadata) {
    metadata = raw.metadata;
    if (!raw.valid ||
        raw.width <= 0 ||
        raw.height <= 0 ||
        raw.width > port::kCompiledCameraFrameWidth ||
        raw.height > port::kCompiledCameraFrameHeight) {
        return false;
    }

    const uint64_t begin_us = NowUs();
    LS2K_PERF_SCOPE(port::PerfStage::kCameraYuyvToGray);
    bool ok = false;
    if (raw.format == port::CameraFrameFormat::kGray) {
        if (raw.stride >= raw.width &&
            raw.data.size() >= static_cast<std::size_t>(raw.stride) *
                                   static_cast<std::size_t>(raw.height)) {
            out = {};
            out.width = raw.width;
            out.height = raw.height;
            for (int row = 0; row < raw.height; ++row) {
                const std::uint8_t* src =
                    raw.data.data() +
                    static_cast<std::size_t>(row) * static_cast<std::size_t>(raw.stride);
                std::uint8_t* dst =
                    out.gray.data() +
                    static_cast<std::size_t>(row) * static_cast<std::size_t>(raw.width);
                std::copy(src, src + raw.width, dst);
            }
            ok = true;
        }
    } else if (raw.format == port::CameraFrameFormat::kYuyv) {
        ok = YuyvToGray(raw.data.data(), raw.width, raw.height, raw.stride, out);
    }
    metadata.yuyv_to_gray_us = NowUs() - begin_us;
    return ok;
}

void CameraCaptureWorker::ThreadMain() {
    while (!stop_requested_.load()) {
        port::CameraRawFrame raw =
            source_->WaitRawFrame(std::max(1, params_.camera_source.poll_timeout_ms),
                                  diagnostics_);
        if (!raw.valid) {
            continue;
        }
        port::LegacyCameraFrame gray{};
        port::CameraRawFrameMetadata metadata{};
        if (!ConvertRawFrame(raw, gray, metadata)) {
            port::EmitRateLimited(diagnostics_,
                                  {port::DiagnosticLevel::kWarning,
                                   "camera_capture_worker.convert_failed",
                                   "camera capture worker dropped frame because conversion failed",
                                   port::NowMs()},
                                  1000);
            continue;
        }
        const uint64_t frame_id = metadata.frame_id == 0 ? raw.metadata.frame_id : metadata.frame_id;
        const uint64_t capture_time_ms =
            metadata.capture_time_ms == 0 ? port::NowMs() : metadata.capture_time_ms;
        const port::LegacyCameraFrameView view = gray.View(frame_id, capture_time_ms);
        {
            LS2K_PERF_SCOPE(port::PerfStage::kCameraStoreSubmit);
            frame_store_.Submit(view, metadata);
        }
    }
    running_.store(false);
}

}  // namespace ls2k::runtime
