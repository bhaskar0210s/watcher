#include "cpu.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <sys/sysctl.h>

namespace watcher {
namespace {

int intProperty(CFTypeRef value) {
    int result = -1;
    if (!value) return result;
    if (CFGetTypeID(value) == CFNumberGetTypeID()) {
        CFNumberGetValue(CFNumberRef(value), kCFNumberIntType, &result);
    } else if (CFGetTypeID(value) == CFDataGetTypeID() && CFDataGetLength(CFDataRef(value)) >= 4) {
        const UInt8* b = CFDataGetBytePtr(CFDataRef(value));
        result = int(b[0] | b[1] << 8 | b[2] << 16 | uint32_t(b[3]) << 24);
    }
    return result;
}

// Each core's cluster type ("E" or "P") from the device tree. Falls back to
// the convention that efficiency cores are numbered first.
std::vector<bool> efficiencyMap(size_t count) {
    std::vector<bool> map(count, false);
    bool found = false;
    io_registry_entry_t cpus = IORegistryEntryFromPath(kIOMainPortDefault, "IODeviceTree:/cpus");
    io_iterator_t it = 0;
    if (cpus && IORegistryEntryGetChildIterator(cpus, kIODeviceTreePlane, &it) == KERN_SUCCESS) {
        while (io_object_t cpu = IOIteratorNext(it)) {
            CFTypeRef id = IORegistryEntryCreateCFProperty(cpu, CFSTR("logical-cpu-id"), kCFAllocatorDefault, 0);
            CFTypeRef type = IORegistryEntryCreateCFProperty(cpu, CFSTR("cluster-type"), kCFAllocatorDefault, 0);
            const int index = intProperty(id);
            if (type && CFGetTypeID(type) == CFDataGetTypeID() && CFDataGetLength(CFDataRef(type)) > 0 &&
                index >= 0 && size_t(index) < count) {
                map[size_t(index)] = CFDataGetBytePtr(CFDataRef(type))[0] == 'E';
                found = true;
            }
            if (id) CFRelease(id);
            if (type) CFRelease(type);
            IOObjectRelease(cpu);
        }
        IOObjectRelease(it);
    }
    if (cpus) IOObjectRelease(cpus);

    if (!found) {
        int eCores = 0;
        size_t len = sizeof(eCores);
        if (sysctlbyname("hw.perflevel1.logicalcpu", &eCores, &len, nullptr, 0) == 0) {
            for (size_t i = 0; i < count && i < size_t(eCores); ++i) map[i] = true;
        }
    }
    return map;
}

}  // namespace

CpuMonitor::CpuMonitor() : host_(mach_host_self()) {
    sample();  // prime the counters so the next sample is a real delta
    efficiency_ = efficiencyMap(usage_.cores.size());
}

const CpuUsage& CpuMonitor::sample() {
    natural_t count = 0;
    processor_info_array_t info = nullptr;
    mach_msg_type_number_t infoCount = 0;
    if (host_processor_info(host_, PROCESSOR_CPU_LOAD_INFO, &count, &info, &infoCount) != KERN_SUCCESS) return usage_;

    const auto* load = reinterpret_cast<const processor_cpu_load_info*>(info);
    if (previous_.size() != size_t(count) * CPU_STATE_MAX) previous_.assign(size_t(count) * CPU_STATE_MAX, 0);
    usage_.cores.resize(count);

    uint64_t busyAll = 0, totalAll = 0;
    for (natural_t i = 0; i < count; ++i) {
        uint32_t delta[CPU_STATE_MAX];
        uint64_t total = 0;
        for (int s = 0; s < CPU_STATE_MAX; ++s) {
            uint32_t& prev = previous_[i * CPU_STATE_MAX + s];
            delta[s] = load[i].cpu_ticks[s] - prev;  // unsigned math survives counter wrap
            prev = load[i].cpu_ticks[s];
            total += delta[s];
        }
        const uint64_t busy = uint64_t(delta[CPU_STATE_USER]) + delta[CPU_STATE_SYSTEM] + delta[CPU_STATE_NICE];
        usage_.cores[i] = total ? double(busy) / double(total) : 0;
        busyAll += busy;
        totalAll += total;
    }
    usage_.total = totalAll ? double(busyAll) / double(totalAll) : 0;
    vm_deallocate(mach_task_self(), vm_address_t(info), vm_size_t(infoCount) * sizeof(integer_t));
    return usage_;
}

}  // namespace watcher
