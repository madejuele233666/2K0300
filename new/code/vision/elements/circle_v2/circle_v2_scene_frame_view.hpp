#ifndef LS2K_VISION_ELEMENTS_CIRCLE_V2_SCENE_FRAME_VIEW_HPP
#define LS2K_VISION_ELEMENTS_CIRCLE_V2_SCENE_FRAME_VIEW_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "vision/bev/bev_simple_perception.hpp"
#include "port/bev_reference_types.hpp"

namespace ls2k::vision {

template <typename T>
class ConstArrayView {
public:
    ConstArrayView() = default;
    ConstArrayView(const T* data, std::size_t size) : data_(data), size_(size) {}

    const T* data() const { return data_; }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0U; }
    const T& operator[](std::size_t index) const { return data_[index]; }

private:
    const T* data_ = nullptr;
    std::size_t size_ = 0;
};

struct BevRowsView {
    ConstArrayView<vision::BEVSimpleRowScan> rows{};
};

struct RoadHalfWidth {
    float value_m = 0.0F;
};

struct OrdinaryRoadModel {
    port::BEVReferencePath center_path{};
    RoadHalfWidth half_width{};
};

using YawDeltaQuery = bool (*)(void* context,
                               uint64_t from_ms,
                               uint64_t to_ms,
                               float& out_delta_rad);

class MotionArcView {
public:
    MotionArcView() = default;
    MotionArcView(void* context, YawDeltaQuery query) : context_(context), query_(query) {}

    bool CanQuery() const { return query_ != nullptr; }

    bool TryYawDeltaRad(uint64_t from_capture_time_ms,
                        uint64_t to_capture_time_ms,
                        float& out_delta_rad) const {
        return query_ != nullptr &&
               query_(context_, from_capture_time_ms, to_capture_time_ms, out_delta_rad);
    }

    float YawDeltaRad(uint64_t from_capture_time_ms,
                      uint64_t to_capture_time_ms) const {
        float delta = 0.0F;
        if (!TryYawDeltaRad(from_capture_time_ms, to_capture_time_ms, delta)) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return delta;
    }

private:
    void* context_ = nullptr;
    YawDeltaQuery query_ = nullptr;
};

struct CaptureStamp {
    uint64_t capture_time_ms = 0;
};

struct SceneFrameView {
    BevRowsView rows{};
    std::optional<OrdinaryRoadModel> ordinary_road{};
    MotionArcView motion_arc{};
    CaptureStamp stamp{};
};

}  // namespace ls2k::vision

#endif  // LS2K_VISION_ELEMENTS_CIRCLE_V2_SCENE_FRAME_VIEW_HPP
