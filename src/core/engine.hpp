#pragma once

#include "cpu.hpp"
#include "gpu.hpp"
#include "history.hpp"
#include "memory.hpp"
#include "sensors.hpp"

namespace watcher {

// Everything the menu bar and the popups show, refreshed once per tick.
struct Engine {
    CpuMonitor cpu;
    MemoryMonitor memory;
    GpuMonitor gpu;
    SensorMonitor sensors;
    UsageHistory cpuHistory, memoryHistory, gpuHistory;

    // CPU, RAM and GPU keep their history even while the popup is closed. The
    // SMC is only read for what is on screen: the hottest CPU temperature for
    // the Sensors headline, everything else while the Sensors tab is open.
    void tick(bool sensorHeadline, bool sensorsVisible) {
        cpuHistory.push(float(cpu.sample().total));
        const MemoryUsage& m = memory.sample();
        memoryHistory.push(m.total > 0 ? float(m.used / m.total) : 0.f);
        gpuHistory.push(float(gpu.sample().utilization));
        if (sensorsVisible) {
            sensors.sample();
        } else if (sensorHeadline) {
            sensors.sampleHeadline();
        }
    }
};

}  // namespace watcher
