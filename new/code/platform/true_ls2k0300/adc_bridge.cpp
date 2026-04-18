#include "platform/true_ls2k0300/bridge.hpp"

#include <fstream>
#include <string>

namespace ls2k::platform::true_ls2k0300 {

BatteryRawResult ReadBatteryRaw(const std::string& adc_path) {
    BatteryRawResult result{};
    result.source = adc_path;

    std::ifstream input(adc_path);
    if (!input.is_open()) {
        result.detail = "battery ADC path unavailable: " + adc_path;
        return result;
    }

    input >> result.raw_value;
    if (input.fail()) {
        result.raw_value = -1;
        result.detail = "battery ADC read failed: " + adc_path;
        return result;
    }

    result.valid = true;
    return result;
}

}  // namespace ls2k::platform::true_ls2k0300
