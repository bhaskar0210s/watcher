#pragma once

#include <string>

namespace watcher {

std::string formatBytes(double bytes);              // "12.9 GB", "4.65 GB", "569.8 MB"
std::string formatPercent(double fraction);         // 0.33 -> "33%"
std::string formatProcessCpu(double percent);       // 65.43 -> "65.4%"
std::string formatTemperature(double celsius, bool fahrenheit);  // "73°C"
std::string formatDegrees(double celsius, bool fahrenheit);      // "73°"
std::string formatWatts(double watts);
std::string formatRpm(double rpm);  // "2,301 RPM"

}  // namespace watcher
