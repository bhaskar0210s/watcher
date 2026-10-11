#include "memory.hpp"

#include <mach/mach.h>
#include <sys/sysctl.h>

namespace watcher {

MemoryMonitor::MemoryMonitor() : host_(mach_host_self()) {
    uint64_t bytes = 0;
    size_t len = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &len, nullptr, 0) == 0) usage_.total = double(bytes);
    sample();
}

const MemoryUsage& MemoryMonitor::sample() {
    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host_, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) == KERN_SUCCESS) {
        const double page = double(vm_page_size);
        const double active = vm.active_count * page;
        const double inactive = vm.inactive_count * page;
        const double speculative = vm.speculative_count * page;
        const double wired = vm.wire_count * page;
        const double compressed = vm.compressor_page_count * page;
        const double purgeable = vm.purgeable_count * page;
        const double external = vm.external_page_count * page;

        usage_.used = active + inactive + speculative + wired + compressed - purgeable - external;
        usage_.wired = wired;
        usage_.compressed = compressed;
        usage_.app = usage_.used - wired - compressed;
        usage_.free = usage_.total - usage_.used;
    }

    xsw_usage swap{};
    size_t len = sizeof(swap);
    if (sysctlbyname("vm.swapusage", &swap, &len, nullptr, 0) == 0) usage_.swap = double(swap.xsu_used);
    return usage_;
}

}  // namespace watcher
