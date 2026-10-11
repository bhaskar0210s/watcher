#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace watcher {

constexpr uint32_t fourcc(const char* s) {
    return (uint32_t(uint8_t(s[0])) << 24) | (uint32_t(uint8_t(s[1])) << 16) |
           (uint32_t(uint8_t(s[2])) << 8) | uint32_t(uint8_t(s[3]));
}

// Read-only client for the AppleSMC kernel service (fans, temperatures, power).
class SMC {
public:
    SMC();
    ~SMC();
    SMC(const SMC&) = delete;
    SMC& operator=(const SMC&) = delete;

    bool isOpen() const { return conn_ != 0; }

    // Decodes flt, fpXY/spXY, ui* and si* keys. Returns false if the key is missing.
    bool read(uint32_t key, double& value);
    bool read(const char* key, double& value) { return read(fourcc(key), value); }
    std::string readString(const char* key);

private:
    struct KeyInfo {
        uint32_t key = 0;
        uint32_t size = 0;
        uint32_t type = 0;
        bool exists = false;
    };
    bool info(uint32_t key, KeyInfo& out);
    bool readBytes(const KeyInfo& key, uint8_t (&bytes)[32]);

    uint32_t conn_ = 0;  // io_connect_t
    std::vector<KeyInfo> cache_;
};

}  // namespace watcher
