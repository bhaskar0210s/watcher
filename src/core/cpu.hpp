#pragma once

#include <cstdint>
#include <vector>

namespace watcher {

struct CpuUsage {
    double total = 0;           // 0...1 across all cores
    std::vector<double> cores;  // 0...1 per logical core
};

// Per-core load from the kernel's tick counters (host_processor_info).
class CpuMonitor {
public:
    CpuMonitor();
    const CpuUsage& sample();
    const CpuUsage& usage() const { return usage_; }
    // true for efficiency cores, indexed like CpuUsage::cores.
    const std::vector<bool>& efficiencyCores() const { return efficiency_; }

private:
    uint32_t host_;  // host_t, fetched once (mach_host_self() adds a port ref per call)
    std::vector<uint32_t> previous_;
    std::vector<bool> efficiency_;
    CpuUsage usage_;
};

}  // namespace watcher
