// `Watcher --snapshot <dir> [seconds]`: samples for a few seconds, then renders
// every popup and the menu bar strip to PNGs and prints the raw readings.
// Used for the README screenshots and for checking numbers from a terminal.
// The process lists only ever show macOS system processes, so the images never
// reveal which apps the person taking them uses.

#include "platform.hpp"

#import <ImageIO/ImageIO.h>

#include "../core/engine.hpp"
#include "../ui/format.hpp"
#include "../ui/widgets.hpp"

#include <libproc.h>
#include <sys/resource.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>

namespace watcher {
namespace {

constexpr CGFloat kScale = 2;

CFRef<CGContextRef> makeBitmap(CGSize points) {
    CFRef<CGColorSpaceRef> space(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    return CFRef<CGContextRef>(CGBitmapContextCreate(
        nullptr, size_t(points.width * kScale), size_t(points.height * kScale), 8, 0, space.get(),
        uint32_t(kCGImageAlphaPremultipliedFirst) | uint32_t(kCGBitmapByteOrder32Host)));
}

void writePNG(CGContextRef context, NSString* path) {
    CFRef<CGImageRef> image(CGBitmapContextCreateImage(context));
    CFRef<CGImageDestinationRef> destination(CGImageDestinationCreateWithURL(
        (__bridge CFURLRef)[NSURL fileURLWithPath:path], CFSTR("public.png"), 1, nullptr));
    if (!image || !destination) return;
    CGImageDestinationAddImage(destination.get(), image.get(), nullptr);
    if (CGImageDestinationFinalize(destination.get())) printf("wrote %s\n", path.UTF8String);
}

// All four looks the menu bar item can take (it follows the last tab hovered).
void renderMenuBar(const Engine& e, const Theme& t, NSString* path) {
    const CGFloat gap = 18, pad = 12, height = 24;
    std::vector<WidgetData> widgets;
    CGFloat width = 2 * pad - gap;
    for (int k = 0; k < kTabCount; ++k) {
        widgets.push_back(widgetFor(Tab(k), e, false));
        width += widgetWidth(widgets.back(), t) + gap;
    }
    CFRef<CGContextRef> g = makeBitmap(CGSizeMake(width, height));
    CGContextScaleCTM(g.get(), kScale, kScale);
    const CGFloat gray = t.dark ? 0.13 : 0.93;
    CFRef<CGColorRef> bar(CGColorCreateSRGB(gray, gray, gray, 1));
    CGContextSetFillColorWithColor(g.get(), bar.get());
    CGContextFillRect(g.get(), CGRectMake(0, 0, width, height));
    CGFloat x = pad;
    for (const WidgetData& w : widgets) {
        const CGFloat ww = widgetWidth(w, t);
        CGContextSaveGState(g.get());
        CGContextTranslateCTM(g.get(), x, 0);
        drawWidget(g.get(), CGSizeMake(ww, height), w, t);
        CGContextRestoreGState(g.get());
        x += ww + gap;
    }
    writePNG(g.get(), path);
}

// Processes we may not inspect belong to other users: macOS's own services.
bool isSystemProcess(pid_t pid) {
    rusage_info_v0 info;
    return pid > 0 && proc_pid_rusage(pid, RUSAGE_INFO_V0, reinterpret_cast<rusage_info_t*>(&info)) != 0;
}

std::vector<ProcessUsage> systemOnly(std::vector<ProcessUsage> rows, size_t count) {
    std::vector<ProcessUsage> kept;
    for (ProcessUsage& row : rows) {
        if (kept.size() < count && isSystemProcess(row.pid)) kept.push_back(std::move(row));
    }
    return kept;
}

void printReadings(const Engine& e) {
    const CpuUsage& cpu = e.cpu.usage();
    printf("CPU   %s  cores:", formatPercent(cpu.total).c_str());
    for (size_t i = 0; i < cpu.cores.size(); ++i) {
        printf(" %s%s", e.cpu.efficiencyCores()[i] ? "E" : "P", formatPercent(cpu.cores[i]).c_str());
    }
    const MemoryUsage& m = e.memory.usage();
    printf("\nRAM   used %s of %s (app %s, wired %s, compressed %s), free %s, swap %s\n", formatBytes(m.used).c_str(),
           formatBytes(m.total).c_str(), formatBytes(m.app).c_str(), formatBytes(m.wired).c_str(),
           formatBytes(m.compressed).c_str(), formatBytes(m.free).c_str(), formatBytes(m.swap).c_str());
    const GpuUsage& g = e.gpu.usage();
    printf("GPU   %s (render %s, tiler %s)\n", formatPercent(g.utilization).c_str(), formatPercent(g.renderer).c_str(),
           formatPercent(g.tiler).c_str());
    const SensorReadings& s = e.sensors.readings();
    for (const Fan& fan : s.fans) {
        printf("Fan   %s: %s (%d%%, max %.0f)\n", fan.name.c_str(), formatRpm(fan.rpm).c_str(), fan.percent(), fan.maxRpm);
    }
    printf("Temp  CPU %.1f  GPU %.1f  Memory %.1f  Battery %.1f  Airflow %.1f (hottest, C)\n", s.cpu, s.gpu, s.memory,
           s.battery, s.airflow);
    printf("Power in %s  system %s  TB left %s  TB right %s\n", formatWatts(s.powerIn).c_str(),
           formatWatts(s.systemTotal).c_str(), formatWatts(s.thunderboltLeft).c_str(),
           formatWatts(s.thunderboltRight).c_str());
}

}  // namespace

int runSnapshot(const char* directory, int seconds) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSString* out = [NSString stringWithUTF8String:directory];
        [NSFileManager.defaultManager createDirectoryAtPath:out withIntermediateDirectories:YES attributes:nil error:nil];

        Engine engine;
        const int ticks = std::max(2, seconds);
        for (int i = 0; i < ticks; ++i) {
            if (i) usleep(1000000);
            engine.tick(true, true);
        }
        const auto cpuTop = systemOnly(topCpuProcesses(200), processSlots(Tab::Cpu));
        const auto memoryTop = systemOnly(foreignMemorySnapshot(400), processSlots(Tab::Ram));
        printReadings(engine);

        ProcessDirectory directoryCache;
        static const char* const names[] = {"cpu", "ram", "gpu", "sensors"};
        for (bool dark : {true, false}) {
            const Theme& t = gThemes.get(dark);
            const char* mode = dark ? "dark" : "light";
            for (int k = 0; k < kTabCount; ++k) {
                PopupContent content;
                content.tab = Tab(k);
                content.engine = &engine;
                content.scale = kScale;
                if (processSlots(content.tab)) {
                    for (const ProcessUsage& p : content.tab == Tab::Cpu ? cpuTop : memoryTop) {
                        content.processes.push_back(directoryCache.row(p, content.tab, kScale));
                    }
                }
                const CGSize size = CGSizeMake(kPopupWidth, std::ceil(renderPopup(nullptr, content, {}, t).height));
                CFRef<CGContextRef> g = makeBitmap(size);
                CGContextTranslateCTM(g.get(), 0, size.height * kScale);
                CGContextScaleCTM(g.get(), kScale, -kScale);
                drawPopupBackground(g.get(), size, t);
                renderPopup(g.get(), content, {}, t);
                writePNG(g.get(), [out stringByAppendingFormat:@"/popup-%s-%s.png", names[k], mode]);
            }
            renderMenuBar(engine, t, [out stringByAppendingFormat:@"/menubar-%s.png", mode]);
        }
    }
    return 0;
}

}  // namespace watcher
