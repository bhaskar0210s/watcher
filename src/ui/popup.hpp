#pragma once

#include "../core/engine.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include <array>
#include <string>
#include <vector>

namespace watcher {

enum class Tab { Cpu, Ram, Gpu, Sensors };
constexpr int kTabCount = 4;
const char* tabName(Tab tab);

constexpr CGFloat kPopupWidth = 300;
constexpr CGFloat kPopupRadius = 16;

struct ProcessRow {
    std::string name;
    std::string value;
    CFRef<CGImageRef> icon;
};

struct PopupContent {
    Tab tab = Tab::Cpu;
    const Engine* engine = nullptr;
    std::vector<ProcessRow> processes;
    bool fahrenheit = false;
    CGFloat scale = 2;  // backing scale, for hairlines
};

struct PopupHover {
    bool inside = false;
    CGPoint point{};
};

struct PopupLayout {
    CGFloat height = 0;
    std::array<CGRect, kTabCount> tabs{};
    CGRect menuButton{};
    std::vector<CGRect> hoverAreas;  // tabs and the menu button react to the pointer
};

// The headline number of a tab ("37%", "73°"), shown on the tab and in the menu bar.
std::string tabValue(Tab tab, const Engine& engine, bool fahrenheit);
// The menu bar widget for a tab (the menu bar follows the last tab hovered).
WidgetData widgetFor(Tab tab, const Engine& engine, bool fahrenheit);

// Number of process rows a tab reserves (5 for CPU, 8 for RAM).
size_t processSlots(Tab tab);

// Lays out the popup (g == nullptr) or draws it into a flipped, y-down context.
PopupLayout renderPopup(CGContextRef g, const PopupContent& content, const PopupHover& hover, const Theme& theme);
void drawPopupBackground(CGContextRef g, CGSize size, const Theme& theme);

}  // namespace watcher
