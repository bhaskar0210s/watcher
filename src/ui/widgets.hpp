#pragma once

#include "theme.hpp"

#include <string>
#include <vector>

namespace watcher {

// The menu bar item: a small graphic followed by a readable number, e.g.
// live per-core bars + "37%", or a thermometer + "73°".
struct WidgetData {
    enum class Graphic { CoreBars, Symbol } graphic = Graphic::CoreBars;
    int symbol = 0;              // Theme::menuIcons index for Graphic::Symbol
    std::vector<double> cores;   // Graphic::CoreBars, 0...1 each
    std::string value;           // "37%", "73°"
    double number = 0;           // the value as a number, for change detection
    bool critical = false;       // shown in red
};

CGFloat widgetWidth(const WidgetData& data, const Theme& t);
// Draws in AppKit's default y-up coordinates, centred vertically in `size`.
void drawWidget(CGContextRef g, CGSize size, const WidgetData& data, const Theme& t);

// How different `next` would look from what is on screen: Minor is a small
// change that can wait a moment, Major is a jump a glance would catch.
enum class WidgetChange { None, Minor, Major };
WidgetChange compareWidgets(const WidgetData& shown, const WidgetData& next);

}  // namespace watcher
