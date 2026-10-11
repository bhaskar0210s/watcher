#include "smc.hpp"

#include <IOKit/IOKitLib.h>

#include <cstring>

namespace watcher {
namespace {

// The AppleSMC user-client ABI: one 80-byte struct in, one out.
struct SMCVersion {
    uint8_t major, minor, build, reserved;
    uint16_t release;
};
struct SMCPLimitData {
    uint16_t version, length;
    uint32_t cpuPLimit, gpuPLimit, memPLimit;
};
struct SMCKeyInfoData {
    uint32_t dataSize, dataType;
    uint8_t dataAttributes;
};
struct SMCParam {
    uint32_t key;
    SMCVersion vers;
    SMCPLimitData pLimitData;
    SMCKeyInfoData keyInfo;
    uint8_t result, status, data8;
    uint32_t data32;
    uint8_t bytes[32];
};
static_assert(sizeof(SMCParam) == 80, "SMCParam must match the AppleSMC ABI");

constexpr uint32_t kUserClientMethod = 2;
constexpr uint8_t kReadBytes = 5;
constexpr uint8_t kReadKeyInfo = 9;

bool call(io_connect_t conn, SMCParam& in, SMCParam& out) {
    size_t size = sizeof(out);
    return IOConnectCallStructMethod(conn, kUserClientMethod, &in, sizeof(in), &out, &size) == kIOReturnSuccess &&
           out.result == 0;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool decode(uint32_t type, const uint8_t* b, uint32_t size, double& value) {
    const char t[4] = {char(type >> 24), char(type >> 16), char(type >> 8), char(type)};
    if (!memcmp(t, "flt ", 4) && size == 4) {  // native-endian float on Apple Silicon
        float f;
        memcpy(&f, b, sizeof(f));
        value = f;
        return true;
    }
    if (t[0] == 'u' && t[1] == 'i') {  // ui8 / ui16 / ui32, big-endian
        uint64_t v = 0;
        for (uint32_t i = 0; i < size && i < 8; ++i) v = (v << 8) | b[i];
        value = double(v);
        return true;
    }
    if (!memcmp(t, "si8 ", 4) && size == 1) {
        value = int8_t(b[0]);
        return true;
    }
    if (!memcmp(t, "si16", 4) && size == 2) {
        value = int16_t(uint16_t(b[0] << 8 | b[1]));
        return true;
    }
    if ((t[0] == 'f' || t[0] == 's') && t[1] == 'p' && size == 2) {  // fixed point, e.g. sp78, fpe2
        const int fraction = hexDigit(t[3]);
        if (fraction < 0) return false;
        const uint16_t raw = uint16_t(b[0] << 8 | b[1]);
        value = (t[0] == 's' ? double(int16_t(raw)) : double(raw)) / double(1 << fraction);
        return true;
    }
    return false;
}

}  // namespace

SMC::SMC() {
    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSMC"));
    if (!service) return;
    io_connect_t conn = 0;
    if (IOServiceOpen(service, mach_task_self(), 0, &conn) == kIOReturnSuccess) conn_ = conn;
    IOObjectRelease(service);
}

SMC::~SMC() {
    if (conn_) IOServiceClose(conn_);
}

bool SMC::info(uint32_t key, KeyInfo& out) {
    for (const KeyInfo& cached : cache_) {
        if (cached.key == key) {
            out = cached;
            return cached.exists;
        }
    }
    KeyInfo k;
    k.key = key;
    if (conn_) {
        SMCParam in{}, res{};
        in.key = key;
        in.data8 = kReadKeyInfo;
        if (call(conn_, in, res) && res.keyInfo.dataSize > 0 && res.keyInfo.dataSize <= 32) {
            k.size = res.keyInfo.dataSize;
            k.type = res.keyInfo.dataType;
            k.exists = true;
        }
    }
    cache_.push_back(k);
    out = k;
    return k.exists;
}

bool SMC::readBytes(const KeyInfo& k, uint8_t (&bytes)[32]) {
    SMCParam in{}, res{};
    in.key = k.key;
    in.keyInfo.dataSize = k.size;
    in.data8 = kReadBytes;
    if (!call(conn_, in, res)) return false;
    memcpy(bytes, res.bytes, sizeof(bytes));
    return true;
}

bool SMC::read(uint32_t key, double& value) {
    KeyInfo k;
    uint8_t bytes[32];
    return info(key, k) && readBytes(k, bytes) && decode(k.type, bytes, k.size, value);
}

std::string SMC::readString(const char* key) {
    KeyInfo k;
    uint8_t bytes[32];
    if (!info(fourcc(key), k) || !readBytes(k, bytes)) return {};
    // Intel fan IDs are "{fds" structs with the name after a 4-byte header.
    const uint32_t start = k.type == fourcc("{fds") ? 4 : 0;
    std::string s;
    for (uint32_t i = start; i < k.size; ++i) {
        if (bytes[i] >= 0x20 && bytes[i] < 0x7f) s += char(bytes[i]);
    }
    const size_t first = s.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(' ') - first + 1);
}

}  // namespace watcher
