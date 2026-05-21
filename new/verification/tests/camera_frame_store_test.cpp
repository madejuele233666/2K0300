#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "runtime/camera_capture_worker.hpp"
#include "runtime/camera_frame_store.hpp"

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestYuyvToGrayRespectsBytesperline() {
    std::array<std::uint8_t, 24> yuyv = {
        10, 1, 20, 2, 30, 3, 40, 4, 99, 99, 99, 99,
        50, 5, 60, 6, 70, 7, 80, 8, 88, 88, 88, 88,
    };
    ls2k::port::LegacyCameraFrame gray{};
    Expect(ls2k::runtime::YuyvToGray(yuyv.data(), 4, 2, 12, gray), "YUYV conversion failed");
    Expect(gray.width == 4 && gray.height == 2, "gray geometry mismatch");
    const std::array<std::uint8_t, 8> expected = {10, 20, 30, 40, 50, 60, 70, 80};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        Expect(gray.gray[index] == expected[index], "gray sample mismatch");
    }

    ls2k::port::LegacyCameraFrame invalid{};
    Expect(!ls2k::runtime::YuyvToGray(yuyv.data(), 4, 2, 7, invalid),
           "short bytesperline must fail closed");
}

ls2k::port::LegacyCameraFrame MakeGrayFrame(std::uint8_t seed) {
    ls2k::port::LegacyCameraFrame frame{};
    frame.width = 2;
    frame.height = 2;
    frame.gray[0] = seed;
    frame.gray[1] = static_cast<std::uint8_t>(seed + 1U);
    frame.gray[2] = static_cast<std::uint8_t>(seed + 2U);
    frame.gray[3] = static_cast<std::uint8_t>(seed + 3U);
    return frame;
}

void TestFrameStoreLatestHistoryAndOverwrite() {
    ls2k::runtime::RuntimeState state{};
    ls2k::runtime::CameraFrameStore store(state);
    ls2k::port::CameraRawFrameMetadata metadata{};

    ls2k::port::LegacyCameraFrame invalid_frame{};
    const ls2k::runtime::CameraFrameHandle invalid =
        store.Submit(invalid_frame.View(1, 100), metadata);
    Expect(!invalid.valid, "invalid frame must not submit");
    Expect(store.Health().dropped_frame_count == 1, "dropped counter mismatch");

    ls2k::port::LegacyCameraFrame first = MakeGrayFrame(10);
    const ls2k::runtime::CameraFrameHandle h1 = store.Submit(first.View(11, 1000), metadata);
    Expect(h1.valid, "first submit failed");
    Expect(store.LatestHandle().frame_id == 11, "latest handle mismatch");
    Expect(store.TryGetLatestAfter(0).has_value(), "latest after old id missing");
    Expect(!store.TryGetLatestAfter(11).has_value(), "latest after same id must be empty");
    Expect(store.FindExact(11, 1000).has_value(), "history exact lookup missing");

    ls2k::port::LegacyCameraFrame copied{};
    Expect(store.CopyFrame(h1, copied), "copy first frame failed");
    Expect(copied.gray[0] == 10 && copied.gray[3] == 13, "copied frame data mismatch");

    for (std::uint64_t id = 12; id <= 14; ++id) {
        ls2k::port::LegacyCameraFrame frame = MakeGrayFrame(static_cast<std::uint8_t>(id));
        const auto handle = store.Submit(frame.View(id, 1000 + id), metadata);
        Expect(handle.valid, "submit while rotating slots failed");
    }
    Expect(store.Health().submitted_frame_count == 4, "submitted counter mismatch");
    Expect(store.Health().overwritten_frame_count >= 1, "overwrite counter did not advance");
    Expect(store.LatestHandle().frame_id == 14, "latest did not advance");
    Expect(!store.CopyFrame(h1, copied), "overwritten generation must not copy");
}

}  // namespace

int main() {
    try {
        TestYuyvToGrayRespectsBytesperline();
        TestFrameStoreLatestHistoryAndOverwrite();
    } catch (const std::exception& error) {
        std::cerr << "camera_frame_store_test failed: " << error.what() << "\n";
        return 1;
    }
    std::cout << "camera_frame_store_test passed\n";
    return 0;
}
