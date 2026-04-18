#include "platform/true_ls2k0300/bridge.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <optional>
#include <sys/stat.h>
#include <string>
#include <vector>

#include "zf_device_imu_core.h"

namespace ls2k::platform::true_ls2k0300 {
namespace {

constexpr std::size_t kSensorPathCount = 9;

struct ResolvedImu {
    uint8_t imu_type = DEV_NO_FIND;
    std::string source;
    std::string device_dir;
    std::array<std::string, kSensorPathCount> data_paths{};
};

ImuInitResult g_imu_init{};
std::array<std::string, kSensorPathCount> g_imu_paths{};

std::optional<std::string> ReadTokenFile(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::nullopt;
    }

    std::string value;
    input >> value;
    if (input.fail()) {
        return std::nullopt;
    }
    return value;
}

std::optional<int> ReadIntFile(const std::string& path) {
    const std::optional<std::string> token = ReadTokenFile(path);
    if (!token.has_value()) {
        return std::nullopt;
    }

    try {
        return std::stoi(*token);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<uint8_t> ParseImuType(const std::string& name) {
    if (name == "IMU660RA") {
        return DEV_IMU660RA;
    }
    if (name == "IMU660RB") {
        return DEV_IMU660RB;
    }
    if (name == "IMU963RA") {
        return DEV_IMU963RA;
    }
    return std::nullopt;
}

std::array<std::string, kSensorPathCount> BuildImuPaths(const std::string& device_dir) {
    return {{
        device_dir + "/in_accel_x_raw",
        device_dir + "/in_accel_y_raw",
        device_dir + "/in_accel_z_raw",
        device_dir + "/in_anglvel_x_raw",
        device_dir + "/in_anglvel_y_raw",
        device_dir + "/in_anglvel_z_raw",
        device_dir + "/in_magn_x_raw",
        device_dir + "/in_magn_y_raw",
        device_dir + "/in_magn_z_raw",
    }};
}

bool ProbePathSet(const std::array<std::string, kSensorPathCount>& paths, uint8_t imu_type, std::string& detail) {
    const std::size_t required_count = (imu_type == DEV_IMU963RA) ? kSensorPathCount : 6;
    for (std::size_t i = 0; i < required_count; ++i) {
        std::ifstream input(paths[i]);
        if (!input.is_open()) {
            detail = "imu resource unavailable: " + paths[i];
            return false;
        }
    }
    return true;
}

std::optional<ResolvedImu> ResolveImuDevice(std::string& detail) {
    std::vector<std::string> name_paths;
    bool used_override = false;
    if (const char* override_name = std::getenv("LS2K_IMU_NAME_PATH");
        override_name != nullptr && override_name[0] != '\0') {
        name_paths.emplace_back(override_name);
        used_override = true;
    } else if (const char* override_dir = std::getenv("LS2K_IMU_DEVICE_DIR");
               override_dir != nullptr && override_dir[0] != '\0') {
        name_paths.emplace_back(std::string(override_dir) + "/name");
        used_override = true;
    } else {
        const char devices_root[] = "/sys/bus/iio/devices";
        struct stat root_stat {};
        if (stat(devices_root, &root_stat) != 0 || !S_ISDIR(root_stat.st_mode)) {
            detail = "IIO devices root unavailable: /sys/bus/iio/devices";
            return std::nullopt;
        }

        DIR* dir = opendir(devices_root);
        if (dir == nullptr) {
            detail = "IIO devices root could not be opened: /sys/bus/iio/devices";
            return std::nullopt;
        }

        while (const dirent* entry = readdir(dir)) {
            const std::string dir_name = entry->d_name;
            if (dir_name.rfind("iio:device", 0) != 0) {
                continue;
            }
            const std::string device_dir = std::string(devices_root) + "/" + dir_name;
            struct stat device_stat {};
            if (stat(device_dir.c_str(), &device_stat) != 0 || !S_ISDIR(device_stat.st_mode)) {
                continue;
            }
            name_paths.push_back(device_dir + "/name");
        }
        closedir(dir);
        std::sort(name_paths.begin(), name_paths.end());
    }

    for (const std::string& name_path : name_paths) {
        const std::optional<std::string> imu_name = ReadTokenFile(name_path);
        if (!imu_name.has_value()) {
            if (used_override) {
                detail = "configured IMU override path could not be read: " + name_path;
                return std::nullopt;
            }
            continue;
        }
        const std::optional<uint8_t> imu_kind = ParseImuType(*imu_name);
        if (!imu_kind.has_value()) {
            if (used_override) {
                detail = "configured IMU override path did not expose a supported IMU name: " + name_path +
                         " value=" + *imu_name;
                return std::nullopt;
            }
            continue;
        }

        ResolvedImu resolved{};
        resolved.imu_type = *imu_kind;
        resolved.source = name_path;
        const std::size_t slash = name_path.find_last_of('/');
        resolved.device_dir = (slash == std::string::npos) ? name_path : name_path.substr(0, slash);
        resolved.data_paths = BuildImuPaths(resolved.device_dir);
        return resolved;
    }

    if (used_override && !name_paths.empty()) {
        detail = "configured IMU override path could not resolve a supported IMU: " + name_paths.front();
        return std::nullopt;
    }
    detail = "no supported IMU name file found under /sys/bus/iio/devices";
    return std::nullopt;
}

}  // namespace

ImuInitResult InitializeImu() {
    g_imu_init = {};
    g_imu_paths = {};

    std::string resolve_detail;
    const std::optional<ResolvedImu> resolved = ResolveImuDevice(resolve_detail);
    if (!resolved.has_value()) {
        imu_type = DEV_NO_FIND;
        g_imu_init.detail = resolve_detail;
        return g_imu_init;
    }

    g_imu_init.imu_type = resolved->imu_type;
    g_imu_init.source = resolved->source;
    g_imu_paths = resolved->data_paths;

    std::string detail;
    if (!ProbePathSet(g_imu_paths, g_imu_init.imu_type, detail)) {
        imu_type = DEV_NO_FIND;
        g_imu_init.detail = detail;
        return g_imu_init;
    }

    imu_type = g_imu_init.imu_type;
    g_imu_init.ready = true;
    g_imu_init.detail = "resolved IMU resource at " + resolved->device_dir;
    return g_imu_init;
}

ImuBridgeSample ReadImuSample() {
    ImuBridgeSample sample{};
    sample.imu_type = g_imu_init.imu_type;
    sample.source = g_imu_init.source;
    if (!g_imu_init.ready) {
        sample.detail = g_imu_init.detail.empty() ? "imu bridge not initialized" : g_imu_init.detail;
        return sample;
    }

    auto read_required = [&](std::size_t index, int16_t& out) -> bool {
        const std::optional<int> value = ReadIntFile(g_imu_paths[index]);
        if (!value.has_value()) {
            sample.detail = "imu sample read failed: " + g_imu_paths[index];
            return false;
        }
        out = static_cast<int16_t>(*value);
        return true;
    };

    if (!read_required(ACC_X_RAW, sample.acc_x) || !read_required(ACC_Y_RAW, sample.acc_y) ||
        !read_required(ACC_Z_RAW, sample.acc_z) || !read_required(GYRO_X_RAW, sample.gyro_x) ||
        !read_required(GYRO_Y_RAW, sample.gyro_y) || !read_required(GYRO_Z_RAW, sample.gyro_z)) {
        return sample;
    }

    if (sample.imu_type == DEV_IMU963RA) {
        if (!read_required(MAG_X_RAW, sample.mag_x) || !read_required(MAG_Y_RAW, sample.mag_y) ||
            !read_required(MAG_Z_RAW, sample.mag_z)) {
            return sample;
        }
    }

    sample.valid = true;
    return sample;
}

}  // namespace ls2k::platform::true_ls2k0300
