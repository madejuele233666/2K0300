#include "platform/camera_frame_source.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#include "platform/true_ls2k0300/bridge.hpp"
#include "port/perf_counter.hpp"

namespace ls2k::platform {
namespace {

uint64_t NowUs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

uint64_t TimevalToMs(const timeval& value) {
    if (value.tv_sec <= 0 && value.tv_usec <= 0) {
        return 0;
    }
    return static_cast<uint64_t>(value.tv_sec) * 1000U +
           static_cast<uint64_t>(value.tv_usec) / 1000U;
}

bool IoctlRetry(int fd, unsigned long request, void* arg) {
    int rc = 0;
    do {
        rc = ioctl(fd, request, arg);
    } while (rc == -1 && errno == EINTR);
    return rc == 0;
}

std::string ErrnoText(const char* operation) {
    return std::string(operation) + " failed: " + std::strerror(errno);
}

struct MappedBuffer {
    void* start = nullptr;
    std::size_t length = 0;
};

class V4l2YuyvFrameSource final : public port::ICameraFrameSource {
public:
    bool Start(const port::CameraSourceParameters& config,
               port::DiagnosticSink& diagnostics) override {
        Stop(diagnostics);
        config_ = config;
        if (config.width <= 0 || config.height <= 0 ||
            config.width > port::kCompiledCameraFrameWidth ||
            config.height > port::kCompiledCameraFrameHeight) {
            diagnostics.Emit({port::DiagnosticLevel::kFailSafe,
                              "camera_source.v4l2.geometry_invalid",
                              "configured V4L2 YUYV geometry exceeds compiled frame storage",
                              port::NowMs()});
            return false;
        }

        fd_ = open(config.device.c_str(), O_RDWR | O_NONBLOCK);
        if (fd_ < 0) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "camera_source.v4l2.open",
                              ErrnoText(("open " + config.device).c_str()),
                              port::NowMs()});
            return false;
        }

        v4l2_capability cap{};
        if (!IoctlRetry(fd_, VIDIOC_QUERYCAP, &cap)) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "camera_source.v4l2.querycap",
                              ErrnoText("VIDIOC_QUERYCAP"),
                              port::NowMs()});
            Stop(diagnostics);
            return false;
        }

        v4l2_format format{};
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.width = static_cast<__u32>(config.width);
        format.fmt.pix.height = static_cast<__u32>(config.height);
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        format.fmt.pix.field = V4L2_FIELD_ANY;
        if (!IoctlRetry(fd_, VIDIOC_S_FMT, &format)) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "camera_source.v4l2.set_format",
                              ErrnoText("VIDIOC_S_FMT YUYV"),
                              port::NowMs()});
            Stop(diagnostics);
            return false;
        }
        if (format.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV ||
            static_cast<int>(format.fmt.pix.width) != config.width ||
            static_cast<int>(format.fmt.pix.height) != config.height ||
            static_cast<int>(format.fmt.pix.bytesperline) < config.width * 2) {
            std::ostringstream message;
            message << "V4L2 device did not accept exact YUYV geometry: width="
                    << format.fmt.pix.width << " height=" << format.fmt.pix.height
                    << " bytesperline=" << format.fmt.pix.bytesperline;
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "camera_source.v4l2.unsupported_format",
                              message.str(),
                              port::NowMs()});
            Stop(diagnostics);
            return false;
        }
        bytesperline_ = static_cast<int>(format.fmt.pix.bytesperline);

        v4l2_streamparm parm{};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator =
            static_cast<__u32>(std::max(1, config.fps));
        (void)IoctlRetry(fd_, VIDIOC_S_PARM, &parm);

        v4l2_requestbuffers request{};
        request.count = static_cast<__u32>(std::max(2, config.buffer_count));
        request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        request.memory = V4L2_MEMORY_MMAP;
        if (!IoctlRetry(fd_, VIDIOC_REQBUFS, &request) || request.count < 2) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "camera_source.v4l2.reqbufs",
                              ErrnoText("VIDIOC_REQBUFS"),
                              port::NowMs()});
            Stop(diagnostics);
            return false;
        }

        buffers_.clear();
        buffers_.resize(request.count);
        for (std::size_t index = 0; index < buffers_.size(); ++index) {
            v4l2_buffer buffer{};
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            buffer.index = static_cast<__u32>(index);
            if (!IoctlRetry(fd_, VIDIOC_QUERYBUF, &buffer)) {
                diagnostics.Emit({port::DiagnosticLevel::kWarning,
                                  "camera_source.v4l2.querybuf",
                                  ErrnoText("VIDIOC_QUERYBUF"),
                                  port::NowMs()});
                Stop(diagnostics);
                return false;
            }
            void* mapped = mmap(nullptr,
                                buffer.length,
                                PROT_READ | PROT_WRITE,
                                MAP_SHARED,
                                fd_,
                                static_cast<off_t>(buffer.m.offset));
            if (mapped == MAP_FAILED) {
                diagnostics.Emit({port::DiagnosticLevel::kWarning,
                                  "camera_source.v4l2.mmap",
                                  ErrnoText("mmap"),
                                  port::NowMs()});
                Stop(diagnostics);
                return false;
            }
            buffers_[index] = {mapped, buffer.length};
            if (!IoctlRetry(fd_, VIDIOC_QBUF, &buffer)) {
                diagnostics.Emit({port::DiagnosticLevel::kWarning,
                                  "camera_source.v4l2.qbuf",
                                  ErrnoText("VIDIOC_QBUF"),
                                  port::NowMs()});
                Stop(diagnostics);
                return false;
            }
        }

        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (!IoctlRetry(fd_, VIDIOC_STREAMON, &type)) {
            diagnostics.Emit({port::DiagnosticLevel::kWarning,
                              "camera_source.v4l2.streamon",
                              ErrnoText("VIDIOC_STREAMON"),
                              port::NowMs()});
            Stop(diagnostics);
            return false;
        }

        ready_ = true;
        diagnostics.Emit({port::DiagnosticLevel::kInfo,
                          "camera_source.v4l2.start",
                          "V4L2 YUYV camera source started on " + config.device,
                          port::NowMs()});
        return true;
    }

    void Stop(port::DiagnosticSink& diagnostics) override {
        if (fd_ >= 0) {
            if (ready_) {
                v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                (void)IoctlRetry(fd_, VIDIOC_STREAMOFF, &type);
            }
            for (MappedBuffer& buffer : buffers_) {
                if (buffer.start != nullptr && buffer.start != MAP_FAILED) {
                    munmap(buffer.start, buffer.length);
                }
            }
            buffers_.clear();
            close(fd_);
            fd_ = -1;
        }
        if (ready_) {
            diagnostics.Emit({port::DiagnosticLevel::kInfo,
                              "camera_source.v4l2.stop",
                              "V4L2 YUYV camera source stopped",
                              port::NowMs()});
        }
        ready_ = false;
        bytesperline_ = 0;
    }

    port::CameraRawFrame WaitRawFrame(int timeout_ms,
                                      port::DiagnosticSink& diagnostics) override {
        port::CameraRawFrame out{};
        out.metadata.source = Name();
        if (!ready_ || fd_ < 0) {
            return out;
        }

        pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLIN;
        const uint64_t poll_begin_us = NowUs();
        int poll_rc = 0;
        {
            LS2K_PERF_SCOPE(port::PerfStage::kCameraV4l2Poll);
            poll_rc = poll(&pfd, 1, std::max(1, timeout_ms));
        }
        out.metadata.poll_wait_us = NowUs() - poll_begin_us;
        if (poll_rc <= 0) {
            if (poll_rc < 0 && errno != EINTR) {
                port::EmitRateLimited(diagnostics,
                                      {port::DiagnosticLevel::kWarning,
                                       "camera_source.v4l2.poll",
                                       ErrnoText("poll"),
                                       port::NowMs()},
                                      1000);
            }
            return out;
        }

        const uint64_t dequeue_begin_us = NowUs();
        int drained = 0;
        port::CameraRawFrame last{};
        last.metadata.source = Name();
        {
            LS2K_PERF_SCOPE(port::PerfStage::kCameraV4l2Dequeue);
        while (true) {
            v4l2_buffer buffer{};
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            if (!IoctlRetry(fd_, VIDIOC_DQBUF, &buffer)) {
                if (errno != EAGAIN) {
                    port::EmitRateLimited(diagnostics,
                                          {port::DiagnosticLevel::kWarning,
                                           "camera_source.v4l2.dqbuf",
                                           ErrnoText("VIDIOC_DQBUF"),
                                           port::NowMs()},
                                          1000);
                }
                break;
            }
            if (buffer.index < buffers_.size()) {
                const MappedBuffer& mapped = buffers_[buffer.index];
                const std::size_t needed =
                    static_cast<std::size_t>(config_.height) * static_cast<std::size_t>(bytesperline_);
                const std::size_t copy_size = std::min(mapped.length, needed);
                last.valid = copy_size >= static_cast<std::size_t>(config_.height * config_.width * 2);
                last.format = port::CameraFrameFormat::kYuyv;
                last.width = config_.width;
                last.height = config_.height;
                last.stride = bytesperline_;
                last.data.assign(static_cast<const uint8_t*>(mapped.start),
                                 static_cast<const uint8_t*>(mapped.start) + copy_size);
                last.metadata.source = Name();
                last.metadata.frame_id = ++frame_id_;
                last.metadata.dequeue_time_ms = port::NowMs();
                last.metadata.capture_time_ms = TimevalToMs(buffer.timestamp);
                last.metadata.v4l2_timestamp_valid = last.metadata.capture_time_ms != 0;
                if (!last.metadata.v4l2_timestamp_valid) {
                    last.metadata.capture_time_ms = last.metadata.dequeue_time_ms;
                }
                last.metadata.v4l2_sequence = buffer.sequence;
            }
            (void)IoctlRetry(fd_, VIDIOC_QBUF, &buffer);
            ++drained;
            if (!config_.drain_ready_buffers) {
                break;
            }
        }
        }
        last.metadata.drained_buffer_count = drained;
        last.metadata.poll_wait_us = out.metadata.poll_wait_us;
        last.metadata.dequeue_us = NowUs() - dequeue_begin_us;
        return last;
    }

    bool Ready() const override { return ready_; }
    const char* Name() const override { return "v4l2_yuyv"; }

private:
    port::CameraSourceParameters config_{};
    std::vector<MappedBuffer> buffers_{};
    int fd_ = -1;
    int bytesperline_ = 0;
    bool ready_ = false;
    uint64_t frame_id_ = 0;
};

class VendorUvcFrameSource final : public port::ICameraFrameSource {
public:
    bool Start(const port::CameraSourceParameters& config,
               port::DiagnosticSink& diagnostics) override {
        config_ = config;
        ready_ = true_ls2k0300::InitializeCamera(config.device);
        diagnostics.Emit({ready_ ? port::DiagnosticLevel::kInfo : port::DiagnosticLevel::kWarning,
                          ready_ ? "camera_source.vendor.start" : "camera_source.vendor.start_failed",
                          ready_ ? "vendor UVC camera source started"
                                 : "vendor UVC camera source failed to start",
                          port::NowMs()});
        return ready_;
    }

    void Stop(port::DiagnosticSink& diagnostics) override {
        if (ready_) {
            true_ls2k0300::ShutdownCamera();
            diagnostics.Emit({port::DiagnosticLevel::kInfo,
                              "camera_source.vendor.stop",
                              "vendor UVC camera source stopped",
                              port::NowMs()});
        }
        ready_ = false;
    }

    port::CameraRawFrame WaitRawFrame(int timeout_ms,
                                      port::DiagnosticSink& diagnostics) override {
        (void)timeout_ms;
        port::CameraRawFrame out{};
        out.metadata.source = Name();
        if (!ready_) {
            return out;
        }
        const uint64_t begin_us = NowUs();
        const true_ls2k0300::CameraFrameView frame = true_ls2k0300::CaptureCameraFrame();
        out.metadata.dequeue_us = NowUs() - begin_us;
        out.metadata.dequeue_time_ms = port::NowMs();
        out.metadata.capture_time_ms = out.metadata.dequeue_time_ms;
        out.metadata.frame_id = ++frame_id_;
        out.metadata.drained_buffer_count = frame.valid ? 1 : 0;
        if (!frame.valid || frame.gray == nullptr) {
            port::EmitRateLimited(diagnostics,
                                  {port::DiagnosticLevel::kWarning,
                                   "camera_source.vendor.empty",
                                   "vendor UVC source returned empty frame",
                                   port::NowMs()},
                                  1000);
            return out;
        }
        out.valid = true;
        out.format = port::CameraFrameFormat::kGray;
        out.width = frame.width;
        out.height = frame.height;
        out.stride = frame.width;
        const std::size_t size =
            static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
        out.data.assign(frame.gray, frame.gray + size);
        return out;
    }

    bool Ready() const override { return ready_; }
    const char* Name() const override { return "vendor_uvc"; }

private:
    port::CameraSourceParameters config_{};
    bool ready_ = false;
    uint64_t frame_id_ = 0;
};

class NullCameraFrameSource final : public port::ICameraFrameSource {
public:
    bool Start(const port::CameraSourceParameters&, port::DiagnosticSink&) override {
        ready_ = true;
        return true;
    }
    void Stop(port::DiagnosticSink&) override { ready_ = false; }
    port::CameraRawFrame WaitRawFrame(int, port::DiagnosticSink&) override { return {}; }
    bool Ready() const override { return ready_; }
    const char* Name() const override { return "null"; }

private:
    bool ready_ = false;
};

std::unique_ptr<port::ICameraFrameSource> MakeSourceByName(const std::string& backend) {
    if (backend == "v4l2_yuyv") {
        return std::make_unique<V4l2YuyvFrameSource>();
    }
    if (backend == "vendor_uvc") {
        return std::make_unique<VendorUvcFrameSource>();
    }
    if (backend == "null" || backend == "disabled") {
        return std::make_unique<NullCameraFrameSource>();
    }
    return nullptr;
}

}  // namespace

std::unique_ptr<port::ICameraFrameSource> MakeStartedCameraFrameSource(
    const port::RuntimeParameters& params,
    port::DiagnosticSink& diagnostics) {
    auto source = MakeSourceByName(params.camera_source.backend);
    if (source && source->Start(params.camera_source, diagnostics)) {
        return source;
    }
    if (params.camera_source.fallback_backend.empty() ||
        params.camera_source.fallback_backend == params.camera_source.backend) {
        return nullptr;
    }

    port::CameraSourceParameters fallback_config = params.camera_source;
    fallback_config.backend = params.camera_source.fallback_backend;
    auto fallback = MakeSourceByName(fallback_config.backend);
    if (fallback && fallback->Start(fallback_config, diagnostics)) {
        diagnostics.Emit({port::DiagnosticLevel::kWarning,
                          "camera_source.fallback",
                          "camera source primary failed; using fallback backend " +
                              fallback_config.backend,
                          port::NowMs()});
        return fallback;
    }
    return nullptr;
}

}  // namespace ls2k::platform
