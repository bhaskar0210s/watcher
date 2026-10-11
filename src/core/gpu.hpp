#pragma once

#include <cstdint>
#include <string>

namespace watcher {

struct GpuUsage {
    double utilization = 0;  // 0...1
    double renderer = 0;
    double tiler = 0;
    bool available = false;
};

// Reads the GPU driver's PerformanceStatistics from the IORegistry.
class GpuMonitor {
public:
    GpuMonitor();
    ~GpuMonitor();
    GpuMonitor(const GpuMonitor&) = delete;
    GpuMonitor& operator=(const GpuMonitor&) = delete;

    const GpuUsage& sample();
    const GpuUsage& usage() const { return usage_; }
    const std::string& model() const { return model_; }  // e.g. "Apple M1 Pro"
    int cores() const { return cores_; }                  // 0 when unknown

private:
    uint32_t service_ = 0;  // io_service_t of the accelerator
    GpuUsage usage_;
    std::string model_;
    int cores_ = 0;
};

}  // namespace watcher
