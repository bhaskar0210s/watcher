#pragma once

#include <cstdint>

namespace watcher {

// Bytes, with the accounting Activity Monitor uses for "Memory Used".
struct MemoryUsage {
    double total = 0;
    double used = 0;
    double app = 0;
    double wired = 0;
    double compressed = 0;
    double free = 0;
    double swap = 0;
};

class MemoryMonitor {
public:
    MemoryMonitor();
    const MemoryUsage& sample();
    const MemoryUsage& usage() const { return usage_; }

private:
    uint32_t host_;
    MemoryUsage usage_;
};

}  // namespace watcher
