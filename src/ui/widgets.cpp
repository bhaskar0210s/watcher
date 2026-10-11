#include "widgets.hpp"

#include "canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace watcher {
namespace {

constexpr CGFloat kGraphicHeight = 14;
constexpr CGFloat kBarWidth = 2, kBarGap = 1;
constexpr CGFloat kSymbolSize = 15;
constexpr CGFloat kSpacing = 4;  // between graphic and number

double clamp01(double v) {
    return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0;
}

CGFloat graphicWidth(const WidgetData& data) {
    if (data.graphic == WidgetData::Graphic::Symbol) return kSymbolSize;
    const CGFloat n = CGFloat(std::max<size_t>(data.cores.size(), 1));
    return n * kBarWidth + (n - 1) * kBarGap;
}

// Wide enough for "100%" so the item never changes width as values move.
CGFloat valueWidth(const Theme& t) {
    CFRef<CTLineRef> widest = makeLine("100%", {t.menuValueFont.get(), t.menuText.get()});
    return std::ceil(lineWidth(widest.get()));
}

}  // namespace

CGFloat widgetWidth(const WidgetData& data, const Theme& t) {
    return graphicWidth(data) + kSpacing + valueWidth(t);
}

void drawWidget(CGContextRef g, CGSize size, const WidgetData& data, const Theme& t) {
    const CGFloat bottom = std::round((size.height - kGraphicHeight) / 2);
    if (data.graphic == WidgetData::Graphic::CoreBars) {
        CGFloat x = 0;
        for (double v : data.cores) {
            const CGFloat h = std::max<CGFloat>(1, std::round(CGFloat(clamp01(v)) * kGraphicHeight * 2) / 2);
            fillRounded(g, CGRectMake(x, bottom, kBarWidth, h), 1, t.menuText.get());
            x += kBarWidth + kBarGap;
        }
    } else if (size_t(data.symbol) < t.menuIcons.size()) {
        CGContextDrawImage(g, CGRectMake(0, std::round((size.height - kSymbolSize) / 2), kSymbolSize, kSymbolSize),
                           t.menuIcons[size_t(data.symbol)].get());
    }
    const CGFloat x = graphicWidth(data) + kSpacing;
    drawTextUp(g, data.value, {t.menuValueFont.get(), data.critical ? t.critical.get() : t.menuText.get()},
               CGRectMake(x, 0, size.width - x, size.height), Align::Left);
}

WidgetChange compareWidgets(const WidgetData& shown, const WidgetData& next) {
    if (shown.graphic != next.graphic || shown.symbol != next.symbol || shown.critical != next.critical ||
        shown.cores.size() != next.cores.size()) {
        return WidgetChange::Major;
    }
    if (std::fabs(shown.number - next.number) >= 3) return WidgetChange::Major;
    WidgetChange change = shown.value != next.value ? WidgetChange::Minor : WidgetChange::None;
    const double pixels = kGraphicHeight * 2;  // bar heights in 2x pixels
    for (size_t i = 0; i < next.cores.size(); ++i) {
        const long delta = std::labs(std::lround(clamp01(shown.cores[i]) * pixels) - std::lround(clamp01(next.cores[i]) * pixels));
        if (delta >= 4) return WidgetChange::Major;
        if (delta > 0) change = WidgetChange::Minor;
    }
    return change;
}

}  // namespace watcher
