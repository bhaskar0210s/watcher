#include "sensors.hpp"

#include <sys/sysctl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace watcher {
namespace {

// SMC temperature keys per Apple Silicon generation, taken from Stats
// (github.com/exelban/stats, Modules/Sensors/values.swift, MIT licensed).
const char* const kNone[] = {nullptr};

const char* const kM1Cpu[] = {"Tp09", "Tp0T", "Tp01", "Tp05", "Tp0D", "Tp0H", "Tp0L", "Tp0P", "Tp0X", "Tp0b", nullptr};
const char* const kM1Gpu[] = {"Tg05", "Tg0D", "Tg0L", "Tg0T", nullptr};
const char* const kM1Memory[] = {"Tm02", "Tm06", "Tm08", "Tm09", nullptr};

const char* const kM2Cpu[] = {"Tp1h", "Tp1t", "Tp1p", "Tp1l", "Tp01", "Tp05",
                              "Tp09", "Tp0D", "Tp0X", "Tp0b", "Tp0f", "Tp0j", nullptr};
const char* const kM2Gpu[] = {"Tg0f", "Tg0j", nullptr};

const char* const kM3Cpu[] = {"Te05", "Te0L", "Te0P", "Te0S", "Tf04", "Tf09", "Tf0A", "Tf0B", "Tf0D",
                              "Tf0E", "Tf44", "Tf49", "Tf4A", "Tf4B", "Tf4D", "Tf4E", nullptr};
const char* const kM3Gpu[] = {"Tf14", "Tf18", "Tf19", "Tf1A", "Tf24", "Tf28", "Tf29", "Tf2A", nullptr};

const char* const kM4Cpu[] = {"Te05", "Te0S", "Te09", "Te0H", "Tp01", "Tp05",
                              "Tp09", "Tp0D", "Tp0V", "Tp0Y", "Tp0b", "Tp0e", nullptr};
const char* const kM4Gpu[] = {"Tg0G", "Tg0H", "Tg0K", "Tg0L", "Tg0d", "Tg0e", "Tg0j", "Tg0k", nullptr};
const char* const kM4ProGpu[] = {"Tg1U", "Tg1k", "Tg0K", "Tg0L", "Tg0d", "Tg0e", "Tg0j", "Tg0k", nullptr};
const char* const kM4Memory[] = {"Tm0p", "Tm1p", "Tm2p", nullptr};

const char* const kM5Cpu[] = {"Tp00", "Tp04", "Tp08", "Tp0C", "Tp0G", "Tp0K", "Tp0O", "Tp0R", "Tp0U",
                              "Tp0X", "Tp0a", "Tp0d", "Tp0g", "Tp0j", "Tp0m", "Tp0p", "Tp0u", "Tp0y", nullptr};
const char* const kM5Gpu[] = {"Tg0U", "Tg0X", "Tg0d", "Tg0g", "Tg0j", "Tg1Y", "Tg1c", "Tg1g", nullptr};

const char* const kA18Cpu[] = {"Te05", "Te0S", "Tp05", "Tp0D", nullptr};
const char* const kA18Gpu[] = {"Tg05", "Tg0D", "Tg0L", "Tg0e", nullptr};

const char* const kBattery[] = {"TB1T", "TB2T", nullptr};
const char* const kAirflow[] = {"TaLP", "TaRF", nullptr};

struct ChipKeys {
    const char* const* cpu;
    const char* const* gpu;
    const char* const* memory;
};

ChipKeys chipKeys() {
    char brand[128] = {};
    size_t len = sizeof(brand) - 1;
    sysctlbyname("machdep.cpu.brand_string", brand, &len, nullptr, 0);  // e.g. "Apple M1 Pro"
    if (strstr(brand, "A18")) return {kA18Cpu, kA18Gpu, kNone};
    const char* m = strstr(brand, "Apple M");
    const bool bigChip = strstr(brand, "Pro") || strstr(brand, "Max") || strstr(brand, "Ultra");
    switch (m ? atoi(m + 7) : 0) {
        case 1: return {kM1Cpu, kM1Gpu, kM1Memory};
        case 2: return {kM2Cpu, kM2Gpu, kNone};
        case 3: return {kM3Cpu, kM3Gpu, kNone};
        case 4: return {kM4Cpu, bigChip ? kM4ProGpu : kM4Gpu, kM4Memory};
        case 5: return {kM5Cpu, kM5Gpu, kNone};
        default: return {kNone, kNone, kNone};
    }
}

}  // namespace

int Fan::percent() const {
    if (rpm < 2 || maxRpm < 2) return 0;
    return int(100 * int64_t(rpm) / int64_t(maxRpm));
}

SensorMonitor::SensorMonitor() {
    if (!smc_.isOpen()) return;

    const ChipKeys chip = chipKeys();
    addGroup(&SensorReadings::cpu, true, chip.cpu);
    addGroup(&SensorReadings::gpu, false, chip.gpu);
    addGroup(&SensorReadings::memory, false, chip.memory);
    addGroup(&SensorReadings::battery, false, kBattery);
    addGroup(&SensorReadings::airflow, false, kAirflow);

    const PowerKey power[] = {
        {&SensorReadings::powerIn, fourcc("PDTR")},           // DC in, from the charger
        {&SensorReadings::systemTotal, fourcc("PSTR")},       // whole-system draw
        {&SensorReadings::thunderboltLeft, fourcc("PU1R")},   // left USB-C/Thunderbolt ports
        {&SensorReadings::thunderboltRight, fourcc("PU2R")},  // right USB-C/Thunderbolt port
    };
    for (const PowerKey& p : power) {
        double watts;
        if (smc_.read(p.key, watts)) power_.push_back(p);
    }

    double count = 0;
    if (smc_.read("FNum", count)) {
        const int fans = std::min(int(count), 8);
        for (int i = 0; i < fans; ++i) {
            char key[5];
            Fan fan;
            snprintf(key, sizeof(key), "F%dMn", i);
            smc_.read(key, fan.minRpm);
            snprintf(key, sizeof(key), "F%dMx", i);
            smc_.read(key, fan.maxRpm);
            snprintf(key, sizeof(key), "F%dID", i);
            fan.name = smc_.readString(key);
            if (fan.name.empty()) {
                fan.name = fans == 2 ? (i == 0 ? "Left fan" : "Right fan") : "Fan " + std::to_string(i + 1);
            }
            snprintf(key, sizeof(key), "F%dAc", i);
            fanKeys_.push_back(fourcc(key));
            readings_.fans.push_back(std::move(fan));
        }
    }
    sample();
}

void SensorMonitor::addGroup(Field field, bool cpu, const char* const* keys) {
    TemperatureGroup group{field, cpu, {}, {}};
    for (; *keys; ++keys) {
        double value = 0;
        // Ignore sensors that read 0 or implausibly hot at startup.
        if (smc_.read(*keys, value) && value > 0 && value <= 110) {
            group.keys.push_back(fourcc(*keys));
            group.last.push_back(value);
        }
    }
    if (!group.keys.empty()) groups_.push_back(std::move(group));
}

void SensorMonitor::sampleGroup(TemperatureGroup& group) {
    double hottest = NAN;
    for (size_t i = 0; i < group.keys.size(); ++i) {
        double value = 0;
        const bool ok = smc_.read(group.keys[i], value);
        if (group.cpu) {
            if (!ok || value < 10 || value > 120) value = group.last[i];  // some M2 sensors glitch
        } else if (!ok || value <= 0 || value > 110) {
            continue;
        }
        group.last[i] = value;
        if (std::isnan(hottest) || value > hottest) hottest = value;
    }
    readings_.*group.field = hottest;
}

const SensorReadings& SensorMonitor::sampleHeadline() {
    for (TemperatureGroup& group : groups_) {
        if (group.cpu) sampleGroup(group);
    }
    return readings_;
}

const SensorReadings& SensorMonitor::sample() {
    for (size_t i = 0; i < fanKeys_.size(); ++i) {
        double rpm;
        if (smc_.read(fanKeys_[i], rpm) && rpm >= 0) readings_.fans[i].rpm = rpm;
    }
    for (TemperatureGroup& group : groups_) sampleGroup(group);
    for (const PowerKey& p : power_) {
        double watts = 0;
        readings_.*p.field = smc_.read(p.key, watts) ? std::max(0.0, watts) : NAN;
    }
    return readings_;
}

}  // namespace watcher
