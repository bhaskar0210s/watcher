#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

namespace watcher {

struct ProcessUsage {
    pid_t pid = 0;
    std::string name;
    double value = 0;  // CPU percent, or bytes of physical footprint
};

// Top CPU consumers as reported by /bin/ps, which is setuid and so sees
// every user's processes. ~20 ms per call.
std::vector<ProcessUsage> topCpuProcesses(size_t count);

// Physical footprint of the biggest processes owned by *other* users
// (WindowServer and friends), which an unprivileged app can't inspect.
// Runs /usr/bin/top: ~0.4 s of CPU, so callers cache the result.
std::vector<ProcessUsage> foreignMemorySnapshot(size_t count);

// Top memory consumers: our own processes read live via libproc (cheap and
// exact), merged with a possibly stale foreign snapshot.
std::vector<ProcessUsage> topMemoryProcesses(size_t count, const std::vector<ProcessUsage>& foreign);

}  // namespace watcher
