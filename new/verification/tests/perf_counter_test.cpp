#include <cstdlib>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "port/perf_counter.hpp"

namespace {

struct TestFailure {
    std::string message;
};

class CollectingDiagnostics final : public ls2k::port::DiagnosticSink {
public:
    void Emit(const ls2k::port::DiagnosticEvent& event) override {
        events.push_back(event);
    }

    std::vector<ls2k::port::DiagnosticEvent> events{};
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure{message};
    }
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void TestExplicitInitializationAndWindowReset() {
    Expect(ls2k::port::InitializePerfCounter(), "perf counter must initialize when enabled");
    Expect(ls2k::port::PerfCounterEnabled(), "perf counter must report enabled state after initialization");
    {
        LS2K_PERF_SCOPE(ls2k::port::PerfStage::kControlTick);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    {
        LS2K_PERF_SCOPE(ls2k::port::PerfStage::kPerceptionElementRaster);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    CollectingDiagnostics diagnostics;
    ls2k::port::EmitPerfWindowDiagnostics(diagnostics, 1000);
    Expect(!diagnostics.events.empty(), "perf window emit must report recorded stages");
    bool saw_control_tick = false;
    bool saw_element_raster = false;
    for (const ls2k::port::DiagnosticEvent& event : diagnostics.events) {
        saw_control_tick = saw_control_tick || Contains(event.message, "stage=control.tick");
        saw_element_raster =
            saw_element_raster || Contains(event.message, "stage=perception.element_raster");
        if (Contains(event.message, "stage=control.tick")) {
            Expect(Contains(event.message, "max_us="), "perf report must include window max");
            Expect(Contains(event.message, "last_us="), "perf report must include last duration");
        }
        if (Contains(event.message, "stage=perception.element_raster")) {
            Expect(Contains(event.message, "avg_us="), "raster perf report must include average");
            Expect(Contains(event.message, "last_us="), "raster perf report must include last duration");
        }
    }
    Expect(saw_control_tick, "perf report must include fixed control.tick stage name");
    Expect(saw_element_raster, "perf report must include element raster stage name");

    diagnostics.events.clear();
    ls2k::port::EmitPerfWindowDiagnostics(diagnostics, 2000);
    Expect(diagnostics.events.empty(), "perf report must reset one-second window counters");
}

}  // namespace

int main() {
    try {
        TestExplicitInitializationAndWindowReset();
    } catch (const TestFailure& failure) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
