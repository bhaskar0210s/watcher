#pragma once

#include "smc.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace watcher {

struct Fan {
    std::string name;
    double rpm = 0;
    double minRpm = 0;
    double maxRpm = 0;
    int percent() const;  // share of the fan's maximum speed
};

struct SensorReadings {
    std::vector<Fan> fans;
    // Hottest sensor of each group in °C; NaN when this Mac has none.
    double cpu = NAN, gpu = NAN, memory = NAN, battery = NAN, airflow = NAN;
    // Watts; NaN when the SMC doesn't expose the key.
    double powerIn = NAN, systemTotal = NAN, thunderboltLeft = NAN, thunderboltRight = NAN;
};

class SensorMonitor {
public:
    SensorMonitor();
    const SensorReadings& sampleHeadline();  // hottest CPU temperature only
    const SensorReadings& sample();          // fans, temperatures and power
    const SensorReadings& readings() const { return readings_; }

private:
    using Field = double SensorReadings::*;
    struct TemperatureGroup {
        Field field;
        bool cpu;  // CPU sensors keep their last good value through glitches
        std::vector<uint32_t> keys;
        std::vector<double> last;
    };
    struct PowerKey {
        Field field;
        uint32_t key;
    };
    void addGroup(Field field, bool cpu, const char* const* keys);
    void sampleGroup(TemperatureGroup& group);

    SMC smc_;
    std::vector<uint32_t> fanKeys_;
    std::vector<TemperatureGroup> groups_;
    std::vector<PowerKey> power_;
    SensorReadings readings_;
};

}  // namespace watcher
