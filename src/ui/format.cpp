#include "format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace watcher {
namespace {

// "%.2f" -> "3.10" -> "3.1"; "370.0" -> "370".
std::string trimmed(double value, int decimals) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

}  // namespace

std::string formatBytes(double bytes) {
    // Binary units with ByteCountFormatter-like precision, as macOS shows memory.
    bytes = std::max(0.0, bytes);
    const double kb = bytes / 1024, mb = kb / 1024, gb = mb / 1024;
    if (mb < 1) return trimmed(kb, 0) + " KB";
    if (gb < 1) return trimmed(mb, 1) + " MB";
    return trimmed(gb, gb < 10 ? 2 : gb < 100 ? 1 : 0) + " GB";
}

std::string formatPercent(double fraction) {
    if (!std::isfinite(fraction)) fraction = 0;
    return std::to_string(int(std::lround(std::clamp(fraction, 0.0, 1.0) * 100))) + "%";
}

std::string formatProcessCpu(double percent) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f%%", std::max(0.0, percent));
    return buf;
}

std::string formatTemperature(double celsius, bool fahrenheit) {
    const double value = fahrenheit ? celsius * 9 / 5 + 32 : celsius;
    return std::to_string(int(std::lround(value))) + (fahrenheit ? "°F" : "°C");
}

std::string formatDegrees(double celsius, bool fahrenheit) {
    const double value = fahrenheit ? celsius * 9 / 5 + 32 : celsius;
    return std::to_string(int(std::lround(value))) + "\u00b0";
}

std::string formatWatts(double watts) {
    if (watts < 0.005) return "0 W";
    char buf[16];
    snprintf(buf, sizeof(buf), watts < 10 ? "%.2f W" : "%.1f W", watts);
    return buf;
}

std::string formatRpm(double rpm) {
    std::string digits = std::to_string(int(std::max(0.0, rpm)));
    for (int i = int(digits.size()) - 3; i > 0; i -= 3) digits.insert(size_t(i), ",");
    return digits + " RPM";
}

}  // namespace watcher
