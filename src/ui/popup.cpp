#include "popup.hpp"

#include "canvas.hpp"
#include "format.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace watcher {
namespace {

constexpr CGFloat kPad = 10;  // popup edge to cards
constexpr CGFloat kCardWidth = kPopupWidth - 2 * kPad;
constexpr CGFloat kInset = 12;  // card edge to content
constexpr CGFloat kGap = 8;     // between cards
constexpr CGFloat kRadius = 12;
constexpr CGFloat kTabsHeight = 50;
constexpr CGFloat kMoreSize = 30;
constexpr CGFloat kTitleBand = 30;  // a card's title
constexpr CGFloat kRow = 22;
constexpr CGFloat kProcessRow = 26;
constexpr CGFloat kChartHeight = 64;

double clamp01(double v) {
    return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0;
}

// Load (or temperature as a share of 100 °C) high enough to call out in red.
bool critical(double fraction) {
    return fraction >= 0.9;
}

struct Headline {
    std::string number, unit, title, subtitle;
    bool critical = false;
};

class Painter {
public:
    Painter(CGContextRef g, const Theme& t, const PopupHover& hover, CGFloat scale)
        : g_(g), t_(t), hover_(hover), hairline_(1 / std::max<CGFloat>(scale, 1)) {}

    CGFloat y = 0;
    PopupLayout layout;

    // A segmented control (icon and name over the live value), the menu button to the right.
    void tabs(Tab selected, const Engine& e, bool fahrenheit) {
        const CGRect bar = CGRectMake(kPad, kPad, kCardWidth - kMoreSize - 6, kTabsHeight);
        if (g_) fillRounded(g_, bar, kRadius, t_.segmentTrack.get());
        const CGFloat width = bar.size.width / kTabCount;
        for (int i = 0; i < kTabCount; ++i) {
            const CGRect r = CGRectMake(bar.origin.x + i * width, bar.origin.y, width, bar.size.height);
            layout.tabs[size_t(i)] = r;
            layout.hoverAreas.push_back(r);
            if (!g_) continue;
            const bool on = int(selected) == i;
            const CGRect pill = CGRectInset(r, 3, 3);
            if (on) {
                CGContextSaveGState(g_);
                if (!t_.dark) {  // a light segment lifts off the track, as in AppKit
                    CFRef<CGColorRef> shadow(CGColorCreateSRGB(0, 0, 0, 0.12));
                    CGContextSetShadowWithColor(g_, CGSizeMake(0, 1), 3, shadow.get());
                }
                fillRounded(g_, pill, kRadius - 3, t_.segmentSelected.get());
                CGContextRestoreGState(g_);
            }

            const TextStyle nameStyle{t_.tabNameFont.get(), on ? t_.text.get() : t_.secondary.get()};
            CFRef<CTLineRef> name = makeLine(tabName(Tab(i)), nameStyle);
            const CGFloat icon = 13, gap = 3, top = r.origin.y + 8;
            const CGFloat x = std::round(CGRectGetMidX(r) - (icon + gap + lineWidth(name.get())) / 2);
            const auto& icons = on ? t_.selectedTabIcons : t_.tabIcons;
            drawImageFlipped(g_, icons[size_t(i)].get(), CGRectMake(x, top, icon, icon));
            drawLine(g_, name.get(), x + icon + gap, top + icon / 2 + std::round(CTFontGetCapHeight(nameStyle.font) / 2),
                     true);
            drawText(g_, tabValue(Tab(i), e, fahrenheit), {t_.tabValueFont.get(), on ? t_.text.get() : t_.secondary.get()},
                     CGRectMake(r.origin.x, r.origin.y + 25, r.size.width, 18), Align::Center);
        }

        layout.menuButton =
            CGRectMake(kPopupWidth - kPad - kMoreSize, kPad + (kTabsHeight - kMoreSize) / 2, kMoreSize, kMoreSize);
        layout.hoverAreas.push_back(layout.menuButton);
        if (g_) {
            if (hovering(layout.menuButton)) fillRounded(g_, layout.menuButton, kMoreSize / 2, t_.segmentTrack.get());
            drawImageFlipped(g_, t_.moreIcon.get(), CGRectInset(layout.menuButton, 6, 6));
        }
        y = kPad + kTabsHeight + 4;
    }

    // The tab's big number with a title and a one-line summary beside it.
    void headline(const Headline& h) {
        if (g_) {
            CFRef<CTLineRef> number = makeLine(h.number, {t_.heroFont.get(), h.critical ? t_.critical.get() : t_.text.get()});
            // A degree sign reads best at full size, right against the number.
            const bool degrees = h.unit == "\u00b0";
            CFRef<CTLineRef> unit = makeLine(h.unit, {degrees ? t_.heroFont.get() : t_.heroUnitFont.get(),
                                                      h.critical ? t_.critical.get() : t_.secondary.get()});
            const CGFloat baseline = y + 41;
            CGFloat x = kPad + 4;
            drawLine(g_, number.get(), x, baseline, true);
            x += lineWidth(number.get()) + (degrees ? 0 : 1);
            drawLine(g_, unit.get(), x, baseline, true);
            x += lineWidth(unit.get()) + 14;
            const CGFloat width = kPopupWidth - kPad - 4 - x;
            drawText(g_, h.title, {t_.heroTitleFont.get(), t_.text.get()}, CGRectMake(x, y + 12, width, 18), Align::Left,
                     true);
            drawText(g_, h.subtitle, {t_.captionFont.get(), t_.secondary.get()}, CGRectMake(x, y + 31, width, 16),
                     Align::Left, true);
        }
        y += 58;
    }

    // A rounded card with a small title; returns the rectangle for its content.
    CGRect card(const char* title, CGFloat contentHeight) {
        const CGRect r = CGRectMake(kPad, y, kCardWidth, kTitleBand + contentHeight + kInset);
        if (g_) {
            fillRounded(g_, r, kRadius, t_.platter.get());
            drawText(g_, title, {t_.cardTitleFont.get(), t_.secondary.get()},
                     CGRectMake(r.origin.x + kInset, r.origin.y + 9, r.size.width - 2 * kInset, 16), Align::Left);
        }
        y += r.size.height + kGap;
        return CGRectMake(r.origin.x + kInset, r.origin.y + kTitleBand, r.size.width - 2 * kInset, contentHeight);
    }

    // Area chart of the last three minutes, newest sample on the right.
    void history(CGRect r, const UsageHistory& h, CGColorRef color) {
        if (!g_) return;
        CGContextSaveGState(g_);
        CGContextSetStrokeColorWithColor(g_, t_.track.get());
        CGContextSetLineWidth(g_, 1);
        const CGFloat dash[] = {2, 3};
        CGContextSetLineDash(g_, 0, dash, 2);
        for (CGFloat f : {0.0, 0.5}) {
            const CGFloat gy = std::round(r.origin.y + r.size.height * f) + 0.5;
            CGContextMoveToPoint(g_, r.origin.x, gy);
            CGContextAddLineToPoint(g_, CGRectGetMaxX(r), gy);
        }
        CGContextStrokePath(g_);
        CGContextRestoreGState(g_);
        fillRect(g_, CGRectMake(r.origin.x, CGRectGetMaxY(r) - 1, r.size.width, 1), t_.track.get());
        const TextStyle axis{t_.smallFont.get(), t_.tertiary.get()};
        drawText(g_, "100%", axis, CGRectMake(r.origin.x, r.origin.y + 2, 40, 12), Align::Left);
        drawText(g_, "50%", axis, CGRectMake(r.origin.x, r.origin.y + r.size.height / 2 + 2, 40, 12), Align::Left);

        const size_t n = h.size();
        if (n < 2) return;
        const CGFloat step = r.size.width / CGFloat(UsageHistory::capacity - 1);
        const CGFloat right = CGRectGetMaxX(r), bottom = CGRectGetMaxY(r) - 1, height = r.size.height - 2;
        auto px = [&](size_t i) { return right - step * CGFloat(n - 1 - i); };
        auto py = [&](size_t i) { return bottom - CGFloat(clamp01(h[i])) * height; };

        CFRef<CGMutablePathRef> line(CGPathCreateMutable());
        CGPathMoveToPoint(line.get(), nullptr, px(0), py(0));
        for (size_t i = 1; i < n; ++i) CGPathAddLineToPoint(line.get(), nullptr, px(i), py(i));
        CFRef<CGMutablePathRef> area(CGPathCreateMutableCopy(line.get()));
        CGPathAddLineToPoint(area.get(), nullptr, px(n - 1), bottom);
        CGPathAddLineToPoint(area.get(), nullptr, px(0), bottom);
        CGPathCloseSubpath(area.get());

        CGContextSaveGState(g_);
        CGContextAddPath(g_, area.get());
        CGContextClip(g_);
        CFRef<CGColorRef> top = withAlpha(color, 0.35), low = withAlpha(color, 0.02);
        fillVerticalGradient(g_, r, top.get(), low.get(), true);
        CGContextRestoreGState(g_);

        CGContextAddPath(g_, line.get());
        CGContextSetStrokeColorWithColor(g_, color);
        CGContextSetLineWidth(g_, 1.5);
        CGContextSetLineJoin(g_, kCGLineJoinRound);
        CGContextSetLineCap(g_, kCGLineCapRound);
        CGContextStrokePath(g_);
        fillRounded(g_, CGRectMake(px(n - 1) - 3, py(n - 1) - 3, 6, 6), 3, color);
    }

    // Name, bar and value on one line.
    void meter(CGRect r, const std::string& label, double fraction, CGColorRef color, const std::string& value,
               CGFloat labelWidth, CGFloat valueWidth, CTFontRef labelFont, CTFontRef valueFont) {
        if (!g_) return;
        drawText(g_, label, {labelFont, t_.secondary.get()},
                 CGRectMake(r.origin.x, r.origin.y, labelWidth, r.size.height), Align::Left, true);
        drawText(g_, value, {valueFont, t_.text.get()},
                 CGRectMake(CGRectGetMaxX(r) - valueWidth, r.origin.y, valueWidth, r.size.height), Align::Right);
        const CGFloat x = r.origin.x + labelWidth + 6;
        const CGRect track = CGRectMake(x, std::round(CGRectGetMidY(r)) - 3, CGRectGetMaxX(r) - valueWidth - 8 - x, 6);
        fillRounded(g_, track, 3, t_.track.get());
        const CGFloat w = CGFloat(clamp01(fraction)) * track.size.width;
        if (w >= 0.5) fillRounded(g_, CGRectMake(track.origin.x, track.origin.y, w, track.size.height), 3, color);
    }

    // Per-core load in two columns; efficiency cores first, in a lighter shade.
    void cores(CGRect r, const std::vector<double>& values, const std::vector<bool>& efficiency) {
        if (!g_ || values.empty()) return;
        const size_t rows = (values.size() + 1) / 2;
        const CGFloat column = (r.size.width - 16) / 2;
        int eIndex = 0, pIndex = 0;
        for (size_t i = 0; i < values.size(); ++i) {
            const bool e = i < efficiency.size() && efficiency[i];
            const std::string label = (e ? "E" : "P") + std::to_string(e ? ++eIndex : ++pIndex);
            const CGRect cell = CGRectMake(r.origin.x + CGFloat(i / rows) * (column + 16),
                                           r.origin.y + CGFloat(i % rows) * kRow, column, kRow);
            CGColorRef fill = critical(values[i]) ? t_.critical.get() : e ? t_.fillSoft.get() : t_.fill.get();
            meter(cell, label, values[i], fill, formatPercent(values[i]), 20, 32, t_.smallFont.get(),
                  t_.smallValueFont.get());
        }
    }

    void processes(CGRect r, const std::vector<ProcessRow>& rows, size_t slots) {
        if (!g_) return;
        const size_t count = std::min(rows.size(), slots);
        for (size_t i = 0; i < count; ++i) {
            const CGRect row = CGRectMake(r.origin.x, r.origin.y + CGFloat(i) * kProcessRow, r.size.width, kProcessRow);
            drawImageFlipped(g_, rows[i].icon.get(), CGRectMake(row.origin.x, std::round(CGRectGetMidY(row)) - 8, 16, 16));
            const CGFloat nameX = row.origin.x + 24;
            CGRect text = CGRectMake(nameX, row.origin.y, CGRectGetMaxX(row) - nameX, row.size.height);
            const CGFloat valueWidth = drawText(g_, rows[i].value, {t_.rowValueFont.get(), t_.text.get()}, text, Align::Right);
            text.size.width -= valueWidth + 10;
            drawText(g_, rows[i].name, {t_.rowFont.get(), t_.text.get()}, text, Align::Left, true);
            if (i + 1 < count) {
                fillRect(g_, CGRectMake(nameX, CGRectGetMaxY(row) - hairline_, CGRectGetMaxX(row) - nameX, hairline_),
                         t_.separator.get());
            }
        }
    }

    void row(CGRect r, const std::string& label, const std::string& value) {
        if (!g_) return;
        drawText(g_, label, {t_.rowFont.get(), t_.secondary.get()}, r, Align::Left);
        drawText(g_, value, {t_.rowValueFont.get(), t_.text.get()}, r, Align::Right);
    }

    // A segmented bar of the memory in use, with its legend below.
    void memoryBreakdown(CGRect r, const MemoryUsage& m) {
        if (!g_) return;
        const double total = m.total > 0 ? m.total : 1;
        const CGRect bar = CGRectMake(r.origin.x, r.origin.y + 2, r.size.width, 10);
        CGContextSaveGState(g_);
        CGContextAddPath(g_, roundedPath(bar, 5).get());
        CGContextClip(g_);
        fillRect(g_, bar, t_.track.get());
        const std::pair<double, CGColorRef> parts[] = {
            {m.app, t_.memory[0].get()}, {m.wired, t_.memory[1].get()}, {m.compressed, t_.memory[2].get()}};
        CGFloat x = bar.origin.x;
        for (const auto& [bytes, color] : parts) {
            const CGFloat w = CGFloat(clamp01(bytes / total)) * bar.size.width;
            fillRect(g_, CGRectMake(x, bar.origin.y, w, bar.size.height), color);
            x += w;
        }
        CGContextRestoreGState(g_);

        const struct {
            const char* name;
            double bytes;
            CGColorRef color;
        } legend[] = {{"App", m.app, t_.memory[0].get()},
                      {"Wired", m.wired, t_.memory[1].get()},
                      {"Compressed", m.compressed, t_.memory[2].get()},
                      {"Free", m.free, t_.track.get()}};
        for (size_t i = 0; i < 4; ++i) {
            const CGRect cell = CGRectMake(r.origin.x, r.origin.y + 20 + CGFloat(i) * kRow, r.size.width, kRow);
            fillRounded(g_, CGRectMake(cell.origin.x, std::round(CGRectGetMidY(cell)) - 4, 8, 8), 4, legend[i].color);
            const CGRect text = CGRectMake(cell.origin.x + 16, cell.origin.y, cell.size.width - 16, cell.size.height);
            drawText(g_, legend[i].name, {t_.rowFont.get(), t_.secondary.get()}, text, Align::Left);
            drawText(g_, formatBytes(legend[i].bytes), {t_.rowValueFont.get(), t_.text.get()}, text, Align::Right);
        }
    }

private:
    bool hovering(CGRect r) const { return hover_.inside && CGRectContainsPoint(r, hover_.point); }

    CGContextRef g_;
    const Theme& t_;
    const PopupHover& hover_;
    CGFloat hairline_;
};

CGFloat rows(size_t n) {
    return CGFloat(n) * kRow;
}

void cpuTab(Painter& p, const PopupContent& c, const Theme& t) {
    const Engine& e = *c.engine;
    const CpuUsage& cpu = e.cpu.usage();
    const auto& efficiency = e.cpu.efficiencyCores();
    const size_t eCores = size_t(std::count(efficiency.begin(), efficiency.end(), true));
    const size_t count = cpu.cores.size();
    std::string summary = std::to_string(count) + " cores";
    if (eCores > 0 && eCores < count) {
        summary = std::to_string(count - eCores) + " performance, " + std::to_string(eCores) + " efficiency";
    }
    const std::string total = formatPercent(cpu.total);
    p.headline({total.substr(0, total.size() - 1), "%", "CPU load", summary, critical(cpu.total)});
    p.history(p.card("History", kChartHeight), e.cpuHistory, t.accent.get());
    p.cores(p.card("Cores", rows((count + 1) / 2)), cpu.cores, efficiency);
    p.processes(p.card("Top processes", processSlots(Tab::Cpu) * kProcessRow), c.processes, processSlots(Tab::Cpu));
}

void ramTab(Painter& p, const PopupContent& c, const Theme& t) {
    const Engine& e = *c.engine;
    const MemoryUsage& m = e.memory.usage();
    const double used = m.total > 0 ? m.used / m.total : 0;
    const std::string percent = formatPercent(used);
    p.headline({percent.substr(0, percent.size() - 1), "%", "Memory used",
                formatBytes(m.used) + " of " + formatBytes(m.total) + " \u00b7 swap " + formatBytes(m.swap),
                critical(used)});
    p.history(p.card("History", kChartHeight), e.memoryHistory, t.accent.get());
    p.memoryBreakdown(p.card("Breakdown", 20 + rows(4)), m);
    p.processes(p.card("Top processes", processSlots(Tab::Ram) * kProcessRow), c.processes, processSlots(Tab::Ram));
}

void gpuTab(Painter& p, const PopupContent& c, const Theme& t) {
    const Engine& e = *c.engine;
    const GpuUsage& u = e.gpu.usage();
    std::string summary = e.gpu.model().empty() ? "Graphics" : e.gpu.model();
    if (e.gpu.cores() > 0) summary += " · " + std::to_string(e.gpu.cores()) + " cores";
    const std::string percent = formatPercent(u.utilization);
    p.headline({percent.substr(0, percent.size() - 1), "%", "GPU load", summary, critical(u.utilization)});
    p.history(p.card("History", kChartHeight), e.gpuHistory, t.accent.get());
    const CGRect engines = p.card("Engines", rows(2));
    const std::pair<const char*, double> loads[] = {{"Renderer", u.renderer}, {"Tiler", u.tiler}};
    for (size_t i = 0; i < 2; ++i) {
        p.meter(CGRectOffset(CGRectMake(engines.origin.x, engines.origin.y, engines.size.width, kRow), 0, rows(i)),
                loads[i].first, loads[i].second, critical(loads[i].second) ? t.critical.get() : t.fill.get(),
                formatPercent(loads[i].second), 70, 40, t.rowFont.get(), t.rowValueFont.get());
    }
}

void sensorsTab(Painter& p, const PopupContent& c, const Theme& t) {
    const SensorReadings& s = c.engine->sensors.readings();

    std::string fans;
    for (const Fan& fan : s.fans) {
        fans += (fans.empty() ? "Fans " : " · ") + formatRpm(fan.rpm).substr(0, formatRpm(fan.rpm).size() - 4);
    }
    if (!fans.empty()) fans += " RPM";
    const std::string degrees = std::isnan(s.cpu) ? "–" : formatDegrees(s.cpu, c.fahrenheit);
    p.headline({degrees.substr(0, degrees.find("°")), "°", "CPU temperature",
                fans.empty() ? "Hottest core" : fans, !std::isnan(s.cpu) && s.cpu >= 90});

    const std::pair<const char*, double> temperatures[] = {
        {"CPU", s.cpu}, {"GPU", s.gpu}, {"Memory", s.memory}, {"Battery", s.battery}, {"Airflow", s.airflow}};
    size_t shown = 0;
    for (const auto& item : temperatures) shown += std::isnan(item.second) ? 0 : 1;
    if (shown) {
        const CGRect r = p.card("Temperatures", rows(shown));
        size_t i = 0;
        for (const auto& [name, celsius] : temperatures) {
            if (std::isnan(celsius)) continue;
            p.meter(CGRectMake(r.origin.x, r.origin.y + rows(i++), r.size.width, kRow), name, celsius / 100,
                    celsius >= 90 ? t.critical.get() : t.fill.get(), formatTemperature(celsius, c.fahrenheit), 70, 46,
                    t.rowFont.get(), t.rowValueFont.get());
        }
    }

    if (!s.fans.empty()) {
        const CGRect r = p.card("Fans", rows(s.fans.size()));
        for (size_t i = 0; i < s.fans.size(); ++i) {
            const Fan& fan = s.fans[i];
            std::string name = fan.name;
            if (name.size() > 4 && name.compare(name.size() - 4, 4, " fan") == 0) name.resize(name.size() - 4);
            p.meter(CGRectMake(r.origin.x, r.origin.y + rows(i), r.size.width, kRow), name,
                    fan.maxRpm > 0 ? fan.rpm / fan.maxRpm : 0, t.fill.get(), formatRpm(fan.rpm), 70, 80, t.rowFont.get(),
                    t.rowValueFont.get());
        }
    }

    const std::pair<const char*, double> power[] = {{"Power in", s.powerIn},
                                                    {"System total", s.systemTotal},
                                                    {"Thunderbolt left", s.thunderboltLeft},
                                                    {"Thunderbolt right", s.thunderboltRight}};
    shown = 0;
    for (const auto& item : power) shown += std::isnan(item.second) ? 0 : 1;
    if (shown) {
        const CGRect r = p.card("Power", rows(shown));
        size_t i = 0;
        for (const auto& [name, watts] : power) {
            if (!std::isnan(watts)) p.row(CGRectMake(r.origin.x, r.origin.y + rows(i++), r.size.width, kRow), name, formatWatts(watts));
        }
    }
}

}  // namespace

const char* tabName(Tab tab) {
    static const char* const names[] = {"CPU", "RAM", "GPU", "Sensors"};
    return names[int(tab)];
}

std::string tabValue(Tab tab, const Engine& e, bool fahrenheit) {
    switch (tab) {
        case Tab::Cpu: return formatPercent(e.cpu.usage().total);
        case Tab::Ram: {
            const MemoryUsage& m = e.memory.usage();
            return formatPercent(m.total > 0 ? m.used / m.total : 0);
        }
        case Tab::Gpu: return formatPercent(e.gpu.usage().utilization);
        case Tab::Sensors: {
            const double celsius = e.sensors.readings().cpu;
            return std::isnan(celsius) ? "–" : formatDegrees(celsius, fahrenheit);
        }
    }
    return {};
}

WidgetData widgetFor(Tab tab, const Engine& e, bool fahrenheit) {
    WidgetData w;
    w.value = tabValue(tab, e, fahrenheit);
    switch (tab) {
        case Tab::Cpu:
            w.cores = e.cpu.usage().cores;
            w.number = e.cpu.usage().total * 100;
            w.critical = critical(e.cpu.usage().total);
            break;
        case Tab::Ram: {
            const MemoryUsage& m = e.memory.usage();
            const double used = m.total > 0 ? m.used / m.total : 0;
            w.graphic = WidgetData::Graphic::Symbol;
            w.symbol = 1;
            w.number = used * 100;
            w.critical = critical(used);
            break;
        }
        case Tab::Gpu:
            w.graphic = WidgetData::Graphic::Symbol;
            w.symbol = 2;
            w.number = e.gpu.usage().utilization * 100;
            w.critical = critical(e.gpu.usage().utilization);
            break;
        case Tab::Sensors: {
            const double celsius = e.sensors.readings().cpu;
            w.graphic = WidgetData::Graphic::Symbol;
            w.symbol = 3;
            w.number = std::isnan(celsius) ? 0 : celsius;
            w.critical = !std::isnan(celsius) && celsius >= 90;
            break;
        }
    }
    return w;
}

size_t processSlots(Tab tab) {
    switch (tab) {
        case Tab::Cpu: return 5;
        case Tab::Ram: return 8;
        default: return 0;
    }
}

PopupLayout renderPopup(CGContextRef g, const PopupContent& content, const PopupHover& hover, const Theme& t) {
    Painter p(g, t, hover, content.scale);
    p.tabs(content.tab, *content.engine, content.fahrenheit);
    switch (content.tab) {
        case Tab::Cpu: cpuTab(p, content, t); break;
        case Tab::Ram: ramTab(p, content, t); break;
        case Tab::Gpu: gpuTab(p, content, t); break;
        case Tab::Sensors: sensorsTab(p, content, t); break;
    }
    p.layout.height = p.y - kGap + kPad;
    return p.layout;
}

// Stands in for the system popover material where there is none (snapshots).
void drawPopupBackground(CGContextRef g, CGSize size, const Theme& t) {
    CFRef<CGPathRef> path = roundedPath(CGRectMake(0, 0, size.width, size.height), kPopupRadius);
    CGContextAddPath(g, path.get());
    CGContextSetFillColorWithColor(g, t.background.get());
    CGContextFillPath(g);
}

}  // namespace watcher
