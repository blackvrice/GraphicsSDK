#pragma once
#include <cstdint>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bench {
// Opt-in observer. Never creates a graphics device or submits GPU work.
class Telemetry {
public:
    explicit Telemetry(std::string_view selectedDevice, unsigned samples, bool allocation);
    ~Telemetry();
    void begin();
    void checkpoint(bool allocationLive, unsigned sample);
    std::string finish();
private:
    struct State;
    std::unique_ptr<State> state;
};
inline double cpuOneCorePercent(std::uint64_t begin, std::uint64_t end, double wallMs) {
    // Process time sums all threads. 100% means one logical CPU, not the machine.
    if (end < begin || !std::isfinite(wallMs) || !(wallMs > 0)) throw std::runtime_error("Invalid CPU telemetry interval");
    return static_cast<double>(end-begin) / 10000.0 / wallMs * 100.0;
}
}
