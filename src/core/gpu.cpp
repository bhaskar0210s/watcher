#include "gpu.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <algorithm>

namespace watcher {
namespace {

io_service_t findAccelerator() {
    io_iterator_t it = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &it) != KERN_SUCCESS) {
        return 0;
    }
    io_service_t found = 0;
    while (io_service_t service = IOIteratorNext(it)) {
        CFTypeRef stats = IORegistryEntryCreateCFProperty(service, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0);
        const bool usable = stats != nullptr;
        if (stats) CFRelease(stats);
        if (usable && !found) {
            found = service;
        } else {
            IOObjectRelease(service);
        }
    }
    IOObjectRelease(it);
    return found;
}

bool percent(CFDictionaryRef stats, CFStringRef key, double& out) {
    const auto number = CFNumberRef(CFDictionaryGetValue(stats, key));
    double value = 0;
    if (!number || CFGetTypeID(number) != CFNumberGetTypeID() ||
        !CFNumberGetValue(number, kCFNumberDoubleType, &value)) {
        return false;
    }
    out = std::clamp(value / 100.0, 0.0, 1.0);
    return true;
}

}  // namespace

GpuMonitor::GpuMonitor() : service_(findAccelerator()) {
    if (service_) {
        if (CFTypeRef model = IORegistryEntryCreateCFProperty(service_, CFSTR("model"), kCFAllocatorDefault, 0)) {
            char buf[128];
            if (CFGetTypeID(model) == CFStringGetTypeID() &&
                CFStringGetCString(CFStringRef(model), buf, sizeof(buf), kCFStringEncodingUTF8)) {
                model_ = buf;
            }
            CFRelease(model);
        }
        if (CFTypeRef cores = IORegistryEntryCreateCFProperty(service_, CFSTR("gpu-core-count"), kCFAllocatorDefault, 0)) {
            if (CFGetTypeID(cores) == CFNumberGetTypeID()) CFNumberGetValue(CFNumberRef(cores), kCFNumberIntType, &cores_);
            CFRelease(cores);
        }
    }
    sample();
}

GpuMonitor::~GpuMonitor() {
    if (service_) IOObjectRelease(service_);
}

const GpuUsage& GpuMonitor::sample() {
    usage_.available = false;
    if (!service_ && !(service_ = findAccelerator())) return usage_;

    CFTypeRef stats = IORegistryEntryCreateCFProperty(service_, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0);
    if (!stats) {  // driver restarted; look it up again next tick
        IOObjectRelease(service_);
        service_ = 0;
        return usage_;
    }
    if (CFGetTypeID(stats) == CFDictionaryGetTypeID()) {
        const auto dict = CFDictionaryRef(stats);
        usage_.available = percent(dict, CFSTR("Device Utilization %"), usage_.utilization) ||
                           percent(dict, CFSTR("GPU Activity(%)"), usage_.utilization);
        percent(dict, CFSTR("Renderer Utilization %"), usage_.renderer);
        percent(dict, CFSTR("Tiler Utilization %"), usage_.tiler);
    }
    CFRelease(stats);
    return usage_;
}

}  // namespace watcher
